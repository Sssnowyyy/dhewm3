/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
A minimal OpenAL implementation for the Wii U, just what dhewm3's sound system
uses, mixing in software on top of SDL2's audio output:

- 16bit mono/stereo buffers at any sample rate, linear resampling (incl. pitch)
- sources with gain, pitch, looping, buffer queues (streaming)
- mono sources are panned by their direction from the listener; there is no
  distance attenuation (dhewm3 sets AL_ROLLOFF_FACTOR 0 and does it itself)
- no extensions, so no EFX reverb (dhewm3 disables it then)

The AL functions are called from the game and the sound thread, the mixer runs
in SDL's audio thread, so everything is done with the audio device locked.
*/

#include "sys/platform.h"
#include "sound/snd_local.h"
#include "sys/sys_sdl.h"

#define MAX_SOURCES			64
#define MAX_BUFFERS			4096
#define MAX_QUEUED			64
#define OUTPUT_FREQUENCY	48000
#define OUTPUT_SAMPLES		1024

struct alBuffer_t {
	bool			used;
	short *			data;
	int				frames;
	int				channels;
	int				frequency;
};

struct alSource_t {
	bool			used;
	ALint			state;
	bool			looping;
	bool			relative;
	float			gain;
	float			pitch;
	float			position[3];
	ALuint			queue[MAX_QUEUED];
	int				queued;
	int				current;		// index into queue
	int				processed;
	uint64_t		offset;			// in the current buffer, 32.32 fixed point frames
};

static struct {
	SDL_AudioDeviceID	device;
	SDL_AudioSpec		spec;
	ALenum				error;
	float				listenerGain;
	float				listenerPosition[3];
	float				listenerAt[3];
	float				listenerUp[3];
	alSource_t			sources[MAX_SOURCES + 1];	// 0 is never a valid name
	alBuffer_t			buffers[MAX_BUFFERS + 1];
	float				mixBuffer[OUTPUT_SAMPLES * 2 * 4];
} al;

static int dummyContext;

idCVar s_wiiuMixDebug( "s_wiiuMixDebug", "0", CVAR_SOUND | CVAR_BOOL, "print Wii U mixer statistics every few seconds" );

// written by the mixer, printed from the game thread
static struct {
	int		callbacks;
	int		maxPlaying;
	float	peak;
} mixStats;

/*
===============================================================================

	Mixing

===============================================================================
*/

static void SetError( ALenum error ) {
	if ( al.error == AL_NO_ERROR ) {
		al.error = error;
	}
}

static alSource_t *Source( ALuint name ) {
	if ( name == 0 || name > MAX_SOURCES || !al.sources[name].used ) {
		SetError( AL_INVALID_NAME );
		return NULL;
	}
	return &al.sources[name];
}

static alBuffer_t *Buffer( ALuint name ) {
	if ( name == 0 || name > MAX_BUFFERS || !al.buffers[name].used ) {
		return NULL;
	}
	return &al.buffers[name];
}

static void Lock( void ) {
	if ( al.device ) {
		SDL_LockAudioDevice( al.device );
	}
}

static void Unlock( void ) {
	if ( al.device ) {
		SDL_UnlockAudioDevice( al.device );
	}
}

// left/right gain of a mono source, equal power panning by its direction from the listener
static void Pan( const alSource_t &src, float &left, float &right ) {
	float rel[3];
	for ( int i = 0; i < 3; i++ ) {
		rel[i] = src.relative ? src.position[i] : src.position[i] - al.listenerPosition[i];
	}
	const float *at = al.listenerAt, *up = al.listenerUp;
	float rightVec[3] = {
		at[1] * up[2] - at[2] * up[1],
		at[2] * up[0] - at[0] * up[2],
		at[0] * up[1] - at[1] * up[0]
	};
	float rightLen = idMath::Sqrt( rightVec[0] * rightVec[0] + rightVec[1] * rightVec[1] + rightVec[2] * rightVec[2] );
	float dist = idMath::Sqrt( rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2] );
	float pan = 0.0f;
	if ( dist > 0.0001f && rightLen > 0.0001f ) {
		pan = ( rel[0] * rightVec[0] + rel[1] * rightVec[1] + rel[2] * rightVec[2] ) / ( dist * rightLen );
	}
	float angle = ( pan + 1.0f ) * ( idMath::PI * 0.25f );
	left = idMath::Cos( angle );
	right = idMath::Sin( angle );
}

static void MixSource( alSource_t &src, float *out, int frames ) {
	float gain = src.gain * al.listenerGain;
	float leftGain = gain, rightGain = gain;
	bool stereo = false;

	alBuffer_t *buf = ( src.current < src.queued ) ? Buffer( src.queue[src.current] ) : NULL;
	if ( buf && buf->channels == 1 ) {
		float l, r;
		Pan( src, l, r );
		leftGain *= l;
		rightGain *= r;
	}

	for ( int i = 0; i < frames; ) {
		buf = ( src.current < src.queued ) ? Buffer( src.queue[src.current] ) : NULL;
		int bufFrames = buf ? buf->frames : 0;
		if ( (int)( src.offset >> 32 ) >= bufFrames ) {
			// on to the next queued buffer
			src.offset = 0;
			src.current++;
			if ( src.current >= src.queued ) {
				if ( src.looping && src.queued > 0 ) {
					src.current = 0;
				} else {
					src.current = src.queued;
					src.processed = src.queued;
					src.state = AL_STOPPED;
					return;
				}
			}
			if ( !src.looping ) {
				src.processed = src.current;
			}
			continue;
		}

		stereo = buf->channels == 2;
		uint64_t step = (uint64_t)( (double)buf->frequency * src.pitch / al.spec.freq * 4294967296.0 );
		const short *data = buf->data;
		for ( ; i < frames; i++ ) {
			int idx = (int)( src.offset >> 32 );
			if ( idx >= bufFrames ) {
				break;
			}
			float frac = (float)( src.offset & 0xffffffff ) * ( 1.0f / 4294967296.0f );
			int next = idx + 1 < bufFrames ? idx + 1 : idx;
			if ( stereo ) {
				float l = data[idx*2+0] + ( data[next*2+0] - data[idx*2+0] ) * frac;
				float r = data[idx*2+1] + ( data[next*2+1] - data[idx*2+1] ) * frac;
				out[i*2+0] += l * gain;
				out[i*2+1] += r * gain;
			} else {
				float s = data[idx] + ( data[next] - data[idx] ) * frac;
				out[i*2+0] += s * leftGain;
				out[i*2+1] += s * rightGain;
			}
			src.offset += step;
		}
	}
}

static void SDLCALL MixCallback( void *userdata, Uint8 *stream, int len ) {
	int bytesPerFrame = ( SDL_AUDIO_BITSIZE( al.spec.format ) / 8 ) * al.spec.channels;
	int frames = Min( len / bytesPerFrame, (int)( sizeof( al.mixBuffer ) / sizeof( al.mixBuffer[0] ) / 2 ) );
	memset( al.mixBuffer, 0, frames * 2 * sizeof( float ) );

	int playing = 0;
	for ( int i = 1; i <= MAX_SOURCES; i++ ) {
		alSource_t &src = al.sources[i];
		if ( src.used && src.state == AL_PLAYING ) {
			MixSource( src, al.mixBuffer, frames );
			playing++;
		}
	}
	mixStats.callbacks++;
	mixStats.maxPlaying = Max( mixStats.maxPlaying, playing );
	for ( int i = 0; i < frames * 2; i++ ) {
		mixStats.peak = Max( mixStats.peak, idMath::Fabs( al.mixBuffer[i] ) );
	}

	if ( SDL_AUDIO_ISFLOAT( al.spec.format ) ) {
		float *out = (float *)stream;
		for ( int i = 0; i < frames * 2; i++ ) {
			out[i] = idMath::ClampFloat( -1.0f, 1.0f, al.mixBuffer[i] * ( 1.0f / 32768.0f ) );
		}
	} else {
		short *out = (short *)stream;
		for ( int i = 0; i < frames * 2; i++ ) {
			float s = al.mixBuffer[i];
			out[i] = s > 32767.0f ? 32767 : ( s < -32768.0f ? -32768 : (short)s );
		}
	}
}

/*
===============================================================================

	AL / ALC API

===============================================================================
*/

extern "C" {

AL_API ALenum AL_APIENTRY alGetError( void ) {
	ALenum e = al.error;
	al.error = AL_NO_ERROR;
	return e;
}

AL_API const ALchar* AL_APIENTRY alGetString( ALenum param ) {
	switch ( param ) {
		case AL_VENDOR:		return "dhewm3";
		case AL_RENDERER:	return "Wii U software mixer (SDL2)";
		case AL_VERSION:	return "1.1";
		default:			return "";
	}
}

AL_API ALint AL_APIENTRY alGetInteger( ALenum param ) {
	return 0;
}

AL_API ALboolean AL_APIENTRY alIsExtensionPresent( const ALchar* fname ) {
	return AL_FALSE;
}

AL_API void* AL_APIENTRY alGetProcAddress( const ALchar* fname ) {
	return NULL;
}

AL_API ALenum AL_APIENTRY alGetEnumValue( const ALchar* ename ) {
	return 0;
}

// listener

AL_API void AL_APIENTRY alListenerf( ALenum pname, ALfloat param ) {
	if ( pname == AL_GAIN ) {
		Lock();
		al.listenerGain = param;
		Unlock();
	}
}

AL_API void AL_APIENTRY alListenerfv( ALenum pname, const ALfloat* param ) {
	Lock();
	// about every 4 seconds of output
	if ( mixStats.callbacks >= al.spec.freq * 4 / Max( 1, (int)al.spec.samples ) ) {
		if ( s_wiiuMixDebug.GetBool() ) {
			common->Printf( "Wii U mixer: %d callbacks, up to %d sources playing, peak %.0f\n",
				mixStats.callbacks, mixStats.maxPlaying, mixStats.peak );
		}
		memset( &mixStats, 0, sizeof( mixStats ) );
	}
	if ( pname == AL_POSITION ) {
		memcpy( al.listenerPosition, param, 3 * sizeof( float ) );
	} else if ( pname == AL_ORIENTATION ) {
		memcpy( al.listenerAt, param, 3 * sizeof( float ) );
		memcpy( al.listenerUp, param + 3, 3 * sizeof( float ) );
	} else if ( pname == AL_GAIN ) {
		al.listenerGain = param[0];
	}
	Unlock();
}

// buffers

AL_API void AL_APIENTRY alGenBuffers( ALsizei n, ALuint* buffers ) {
	Lock();
	for ( int i = 0; i < n; i++ ) {
		buffers[i] = 0;
		for ( ALuint b = 1; b <= MAX_BUFFERS; b++ ) {
			if ( !al.buffers[b].used ) {
				memset( &al.buffers[b], 0, sizeof( al.buffers[b] ) );
				al.buffers[b].used = true;
				buffers[i] = b;
				break;
			}
		}
		if ( !buffers[i] ) {
			SetError( AL_OUT_OF_MEMORY );
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alDeleteBuffers( ALsizei n, const ALuint* buffers ) {
	Lock();
	for ( int i = 0; i < n; i++ ) {
		alBuffer_t *b = Buffer( buffers[i] );
		if ( b ) {
			free( b->data );
			memset( b, 0, sizeof( *b ) );
		}
	}
	Unlock();
}

AL_API ALboolean AL_APIENTRY alIsBuffer( ALuint buffer ) {
	return Buffer( buffer ) != NULL;
}

AL_API void AL_APIENTRY alBufferData( ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq ) {
	int channels;
	if ( format == AL_FORMAT_MONO16 ) {
		channels = 1;
	} else if ( format == AL_FORMAT_STEREO16 ) {
		channels = 2;
	} else {
		SetError( AL_INVALID_ENUM );
		return;
	}
	short *copy = (short *)malloc( size );
	if ( !copy ) {
		SetError( AL_OUT_OF_MEMORY );
		return;
	}
	memcpy( copy, data, size );

	Lock();
	alBuffer_t *b = Buffer( buffer );
	if ( b ) {
		free( b->data );
		b->data = copy;
		b->channels = channels;
		b->frames = size / ( 2 * channels );
		b->frequency = freq;
	} else {
		free( copy );
		SetError( AL_INVALID_NAME );
	}
	Unlock();
}

// sources

AL_API void AL_APIENTRY alGenSources( ALsizei n, ALuint* sources ) {
	Lock();
	for ( int i = 0; i < n; i++ ) {
		sources[i] = 0;
		for ( ALuint s = 1; s <= MAX_SOURCES; s++ ) {
			if ( !al.sources[s].used ) {
				alSource_t &src = al.sources[s];
				memset( &src, 0, sizeof( src ) );
				src.used = true;
				src.state = AL_INITIAL;
				src.gain = 1.0f;
				src.pitch = 1.0f;
				sources[i] = s;
				break;
			}
		}
		if ( !sources[i] ) {
			SetError( AL_OUT_OF_MEMORY );
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alDeleteSources( ALsizei n, const ALuint* sources ) {
	Lock();
	for ( int i = 0; i < n; i++ ) {
		alSource_t *src = Source( sources[i] );
		if ( src ) {
			memset( src, 0, sizeof( *src ) );
		}
	}
	Unlock();
}

AL_API ALboolean AL_APIENTRY alIsSource( ALuint sid ) {
	return sid > 0 && sid <= MAX_SOURCES && al.sources[sid].used;
}

AL_API void AL_APIENTRY alSourcef( ALuint sid, ALenum param, ALfloat value ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		if ( param == AL_GAIN ) {
			src->gain = value;
		} else if ( param == AL_PITCH ) {
			src->pitch = value > 0.0f ? value : 1.0f;
		}
		// distances and rolloff: dhewm3 does the distance attenuation itself
	}
	Unlock();
}

AL_API void AL_APIENTRY alSource3f( ALuint sid, ALenum param, ALfloat v1, ALfloat v2, ALfloat v3 ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src && param == AL_POSITION ) {
		src->position[0] = v1;
		src->position[1] = v2;
		src->position[2] = v3;
	}
	Unlock();
}

AL_API void AL_APIENTRY alSource3i( ALuint sid, ALenum param, ALint v1, ALint v2, ALint v3 ) {
	// only used for EFX sends
}

AL_API void AL_APIENTRY alSourcei( ALuint sid, ALenum param, ALint value ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		switch ( param ) {
			case AL_BUFFER:
				src->queued = 0;
				if ( value ) {
					src->queue[0] = value;
					src->queued = 1;
				}
				src->current = 0;
				src->processed = 0;
				src->offset = 0;
				break;
			case AL_LOOPING:
				src->looping = value != AL_FALSE;
				break;
			case AL_SOURCE_RELATIVE:
				src->relative = value != AL_FALSE;
				break;
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alGetSourcei( ALuint sid, ALenum pname, ALint* value ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		switch ( pname ) {
			case AL_SOURCE_STATE:		*value = src->state; break;
			case AL_BUFFERS_PROCESSED:	*value = src->processed; break;
			case AL_BUFFERS_QUEUED:		*value = src->queued; break;
			case AL_LOOPING:			*value = src->looping; break;
			case AL_SOURCE_RELATIVE:	*value = src->relative; break;
			case AL_BUFFER:				*value = src->queued ? src->queue[src->current < src->queued ? src->current : 0] : 0; break;
			default:					*value = 0; break;
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alSourceQueueBuffers( ALuint sid, ALsizei numEntries, const ALuint *bids ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		for ( int i = 0; i < numEntries; i++ ) {
			if ( src->queued >= MAX_QUEUED ) {
				SetError( AL_OUT_OF_MEMORY );
				break;
			}
			src->queue[src->queued++] = bids[i];
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alSourceUnqueueBuffers( ALuint sid, ALsizei numEntries, ALuint *bids ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		if ( numEntries > src->processed ) {
			SetError( AL_INVALID_VALUE );
		} else {
			memcpy( bids, src->queue, numEntries * sizeof( ALuint ) );
			memmove( src->queue, src->queue + numEntries, ( src->queued - numEntries ) * sizeof( ALuint ) );
			src->queued -= numEntries;
			src->processed -= numEntries;
			src->current = Max( 0, src->current - numEntries );
		}
	}
	Unlock();
}

AL_API void AL_APIENTRY alSourcePlay( ALuint sid ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		if ( src->state != AL_PAUSED ) {
			// (re)start from the beginning of the queue
			src->current = 0;
			src->processed = 0;
			src->offset = 0;
		}
		src->state = src->queued ? AL_PLAYING : AL_STOPPED;
	}
	Unlock();
}

AL_API void AL_APIENTRY alSourceStop( ALuint sid ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src ) {
		src->state = AL_STOPPED;
		src->processed = src->queued;
		src->current = src->queued;
	}
	Unlock();
}

AL_API void AL_APIENTRY alSourcePause( ALuint sid ) {
	Lock();
	alSource_t *src = Source( sid );
	if ( src && src->state == AL_PLAYING ) {
		src->state = AL_PAUSED;
	}
	Unlock();
}

// device / context

ALC_API ALCdevice * ALC_APIENTRY alcOpenDevice( const ALCchar *devicename ) {
	if ( al.device ) {
		return (ALCdevice *)&al;
	}
	if ( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 ) {
		common->Printf( "OpenAL (Wii U): SDL_InitSubSystem( SDL_INIT_AUDIO ) failed: %s\n", SDL_GetError() );
		return NULL;
	}

	SDL_AudioSpec want;
	memset( &want, 0, sizeof( want ) );
	want.freq = OUTPUT_FREQUENCY;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = OUTPUT_SAMPLES;
	want.callback = MixCallback;

	memset( al.sources, 0, sizeof( al.sources ) );
	memset( al.buffers, 0, sizeof( al.buffers ) );
	al.listenerGain = 1.0f;
	al.listenerAt[2] = -1.0f;
	al.listenerUp[1] = 1.0f;

	al.device = SDL_OpenAudioDevice( NULL, 0, &want, &al.spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE );
	if ( !al.device ) {
		common->Printf( "OpenAL (Wii U): SDL_OpenAudioDevice() failed: %s\n", SDL_GetError() );
		return NULL;
	}
	if ( al.spec.channels != 2 || ( SDL_AUDIO_BITSIZE( al.spec.format ) != 16 && !SDL_AUDIO_ISFLOAT( al.spec.format ) ) ) {
		common->Printf( "OpenAL (Wii U): unsupported output format\n" );
		SDL_CloseAudioDevice( al.device );
		al.device = 0;
		return NULL;
	}
	common->Printf( "OpenAL (Wii U): mixing to %d Hz, %d samples per callback\n", al.spec.freq, al.spec.samples );
	SDL_PauseAudioDevice( al.device, 0 );
	return (ALCdevice *)&al;
}

ALC_API ALCboolean ALC_APIENTRY alcCloseDevice( ALCdevice *dev ) {
	if ( al.device ) {
		SDL_CloseAudioDevice( al.device );
		al.device = 0;
	}
	for ( int i = 1; i <= MAX_BUFFERS; i++ ) {
		free( al.buffers[i].data );
	}
	memset( al.buffers, 0, sizeof( al.buffers ) );
	memset( al.sources, 0, sizeof( al.sources ) );
	return ALC_TRUE;
}

ALC_API ALCcontext * ALC_APIENTRY alcCreateContext( ALCdevice *dev, const ALCint* attrlist ) {
	return (ALCcontext *)&dummyContext;
}

ALC_API ALCboolean ALC_APIENTRY alcMakeContextCurrent( ALCcontext *context ) {
	return ALC_TRUE;
}

ALC_API void ALC_APIENTRY alcProcessContext( ALCcontext *context ) {
}

ALC_API void ALC_APIENTRY alcSuspendContext( ALCcontext *context ) {
}

ALC_API void ALC_APIENTRY alcDestroyContext( ALCcontext *context ) {
}

ALC_API ALCenum ALC_APIENTRY alcGetError( ALCdevice *device ) {
	return ALC_NO_ERROR;
}

ALC_API ALCboolean ALC_APIENTRY alcIsExtensionPresent( ALCdevice *device, const ALCchar *extname ) {
	return ALC_FALSE;
}

ALC_API void * ALC_APIENTRY alcGetProcAddress( ALCdevice *device, const ALCchar *funcname ) {
	return NULL;
}

ALC_API const ALCchar * ALC_APIENTRY alcGetString( ALCdevice *device, ALCenum param ) {
	switch ( param ) {
		case ALC_DEFAULT_DEVICE_SPECIFIER:
		case ALC_DEVICE_SPECIFIER:
			// device lists are terminated by an empty string
			return "Wii U\0";
		default:
			return "";
	}
}

ALC_API void ALC_APIENTRY alcGetIntegerv( ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values ) {
	if ( size < 1 || !values ) {
		return;
	}
	switch ( param ) {
		case ALC_CONNECTED:	values[0] = 1; break;
		case ALC_FREQUENCY:	values[0] = al.spec.freq; break;
		default:			values[0] = 0; break;
	}
}

} // extern "C"

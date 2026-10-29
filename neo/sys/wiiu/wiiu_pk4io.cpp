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

// File access for the .pk4s (minizip's ioapi) on the Wii U.
//
// minizip opens the .pk4 again for every file that's read from it, seeks before
// every read and reads 16KB at a time. On the Wii U each of those is a request
// to IOSU and the SD card / USB drive, which takes milliseconds, so a level load
// spent minutes on them. Here every .pk4 is opened once and shared by all
// streams, and reads go through a small cache of large blocks.
//
// The blocks are cache line aligned, so the filesystem DMAs straight into them
// without going through the CPU cache - which has to be flushed before and
// invalidated after, or the CPU sees what was in the block before. Cemu doesn't
// emulate the CPU cache, so that only shows on a real Wii U.

#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>

#include <coreinit/cache.h>
#include <coreinit/mutex.h>

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/FileSystem.h"
#include "framework/minizip/ioapi.h"
#include "sys/wiiu/wiiu_loadprofile.h"

#define BLOCK_SIZE		( 128 * 1024 )
#define NUM_BLOCKS		8
#define MAX_PK4S		64

struct pk4Block_t {
	int64_t		offset;			// -1: empty
	int			length;
	unsigned	lastUse;
	byte *		data;
};

struct pk4Source_t {
	char		path[MAX_OSPATH];
	int			fd;
	int64_t		size;
	OSMutex		mutex;			// the sound thread reads too
	unsigned	useCounter;
	pk4Block_t	blocks[NUM_BLOCKS];
};

struct pk4Stream_t {
	pk4Source_t *	source;
	int64_t			pos;
	bool			error;
};

static OSMutex			sourcesMutex;
static bool				initialized;
static pk4Source_t *	sources[MAX_PK4S];
static int				numSources;

// reads straight from the drive, with the source locked
static int ReadAt( pk4Source_t *src, int64_t offset, void *dest, int length ) {
	double start = Sys_MillisecondsPrecise();
	int done = 0;
	if ( lseek( src->fd, (off_t)offset, SEEK_SET ) == (off_t)offset ) {
		while ( done < length ) {
			ssize_t n = read( src->fd, (byte *)dest + done, length - done );
			if ( n <= 0 ) {
				break;
			}
			done += n;
		}
	}
	wiiuLoadProfile.driveReadMs += Sys_MillisecondsPrecise() - start;
	wiiuLoadProfile.driveReads++;
	wiiuLoadProfile.driveReadBytes += done;
	return done;
}

static pk4Block_t *GetBlock( pk4Source_t *src, int64_t offset ) {
	pk4Block_t *oldest = &src->blocks[0];
	for ( int i = 0; i < NUM_BLOCKS; i++ ) {
		pk4Block_t *b = &src->blocks[i];
		if ( b->offset == offset ) {
			b->lastUse = ++src->useCounter;
			return b;
		}
		if ( b->lastUse < oldest->lastUse ) {
			oldest = b;
		}
	}
	if ( !oldest->data ) {
		oldest->data = (byte *)memalign( 0x40, BLOCK_SIZE );
		if ( !oldest->data ) {
			return NULL;
		}
	}
	int length = (int)Min( (int64_t)BLOCK_SIZE, src->size - offset );
	oldest->offset = offset;
	DCFlushRange( oldest->data, BLOCK_SIZE );
	// whole cache lines, so the filesystem never has to split the read
	oldest->length = ReadAt( src, offset, oldest->data, ( length + 63 ) & ~63 );
	DCInvalidateRange( oldest->data, BLOCK_SIZE );
	oldest->lastUse = ++src->useCounter;
	if ( oldest->length < length ) {
		oldest->offset = -1;
		return NULL;
	}
	return oldest;
}

static voidpf ZCALLBACK pk4_open( voidpf opaque, const void *filename, int mode ) {
	if ( !filename || ( mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER ) != ZLIB_FILEFUNC_MODE_READ ) {
		return NULL;	// the engine never writes .zip files
	}

	OSLockMutex( &sourcesMutex );
	pk4Source_t *src = NULL;
	for ( int i = 0; i < numSources; i++ ) {
		if ( strcmp( sources[i]->path, (const char *)filename ) == 0 ) {
			src = sources[i];
			break;
		}
	}
	if ( !src && numSources < MAX_PK4S && strlen( (const char *)filename ) < MAX_OSPATH ) {
		int fd = open( (const char *)filename, O_RDONLY );
		if ( fd >= 0 ) {
			src = new pk4Source_t;
			memset( src, 0, sizeof( *src ) );
			strcpy( src->path, (const char *)filename );
			src->fd = fd;
			src->size = lseek( fd, 0, SEEK_END );
			OSInitMutex( &src->mutex );
			for ( int i = 0; i < NUM_BLOCKS; i++ ) {
				src->blocks[i].offset = -1;
			}
			sources[numSources++] = src;
		}
	}
	OSUnlockMutex( &sourcesMutex );
	if ( !src ) {
		return NULL;
	}

	wiiuLoadProfile.pk4Opens++;
	pk4Stream_t *stream = new pk4Stream_t;
	stream->source = src;
	stream->pos = 0;
	stream->error = false;
	return stream;
}

static uLong ZCALLBACK pk4_read( voidpf opaque, voidpf _stream, void *buf, uLong size ) {
	pk4Stream_t *stream = (pk4Stream_t *)_stream;
	pk4Source_t *src = stream->source;
	byte *dest = (byte *)buf;
	uLong done = 0;

	OSLockMutex( &src->mutex );
	while ( done < size && stream->pos < src->size ) {
		int64_t blockOffset = stream->pos & ~(int64_t)( BLOCK_SIZE - 1 );
		int inBlock = (int)( stream->pos - blockOffset );
		int want = (int)Min( (int64_t)( size - done ), src->size - stream->pos );

		// everything goes through the blocks, so only they need the cache handling above
		pk4Block_t *b = GetBlock( src, blockOffset );
		if ( !b ) {
			stream->error = true;
			break;
		}
		int n = Min( want, (int)Min( (int64_t)b->length, src->size - blockOffset ) - inBlock );
		memcpy( dest + done, b->data + inBlock, n );
		done += n;
		stream->pos += n;
	}
	OSUnlockMutex( &src->mutex );
	return done;
}

static uLong ZCALLBACK pk4_write( voidpf opaque, voidpf stream, const void *buf, uLong size ) {
	return 0;
}

static ZPOS64_T ZCALLBACK pk4_tell( voidpf opaque, voidpf stream ) {
	return ( (pk4Stream_t *)stream )->pos;
}

static long ZCALLBACK pk4_seek( voidpf opaque, voidpf _stream, ZPOS64_T offset, int origin ) {
	pk4Stream_t *stream = (pk4Stream_t *)_stream;
	int64_t pos;
	switch ( origin ) {
		case ZLIB_FILEFUNC_SEEK_SET: pos = offset; break;
		case ZLIB_FILEFUNC_SEEK_CUR: pos = stream->pos + offset; break;
		case ZLIB_FILEFUNC_SEEK_END: pos = stream->source->size + offset; break;
		default: return -1;
	}
	if ( pos < 0 ) {
		return -1;
	}
	stream->pos = pos;
	return 0;
}

static int ZCALLBACK pk4_close( voidpf opaque, voidpf stream ) {
	// the .pk4 itself stays open, it's needed again soon
	delete (pk4Stream_t *)stream;
	return 0;
}

static int ZCALLBACK pk4_error( voidpf opaque, voidpf stream ) {
	return ( (pk4Stream_t *)stream )->error;
}

extern "C" void Wiiu_FillPk4FileFuncs( zlib_filefunc64_def *def ) {
	if ( !initialized ) {
		OSInitMutex( &sourcesMutex );
		initialized = true;
	}
	def->zopen64_file = pk4_open;
	def->zread_file = pk4_read;
	def->zwrite_file = pk4_write;
	def->ztell64_file = pk4_tell;
	def->zseek64_file = pk4_seek;
	def->zclose_file = pk4_close;
	def->zerror_file = pk4_error;
	def->opaque = NULL;
}

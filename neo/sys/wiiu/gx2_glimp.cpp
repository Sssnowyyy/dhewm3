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

// GLimp_* for the Wii U: sets up GX2, renders everything into one color/depth
// buffer and copies that to the TV and the GamePad at the end of each frame.

#include <malloc.h>

#include <coreinit/cache.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <gx2/clear.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <proc_ui/procui.h>

#include "sys/platform.h"
#include "renderer/tr_local.h"
#include "framework/Common.h"

extern idCVar com_speeds;

#include "sys/wiiu/gx2_local.h"
#include "sys/wiiu/wiiu_loadprofile.h"
#include "sys/wiiu/wiiu_keyboard.h"

GX2ColorBuffer		gx2ColorBuffer;
GX2DepthBuffer		gx2DepthBuffer;
GX2ColorBuffer		gx2DepthColorBuffer;
GX2ContextState *	gx2ContextState;
bool				gx2Foreground;

static void *		commandBufferPool;
static void *		tvScanBuffer;
static void *		drcScanBuffer;
static uint32_t		tvScanBufferSize;
static uint32_t		drcScanBufferSize;
static bool			gx2Initialized;
static bool			displaysEnabled;
static glimpParms_t	curParms;

// the GamePad's own buffer while the software keyboard is open (DrawKeyboardDRC())
static GX2ColorBuffer		drcColorBuffer;
static GX2DepthBuffer		drcDepthBuffer;
static GX2ContextState *	drcContextState;

static const GX2TVRenderMode TV_RENDER_MODE = GX2_TV_RENDER_MODE_WIDE_720P;
static const GX2SurfaceFormat SCAN_FORMAT = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;

idCVar r_wiiuProfile( "r_wiiuProfile", "5", CVAR_RENDERER | CVAR_INTEGER, "log performance statistics every N seconds (0 = never)" );
idCVar r_gx2Test( "r_gx2Test", "0", CVAR_RENDERER | CVAR_INTEGER, "debug: 1: draw a test pattern on top of every frame, 2: show the captured (packed) depth texture, 3: show a white texture instead, 7: open the software keyboard" );
// NOTE: doesn't work in Cemu, which doesn't write GPU rendered surfaces back to emulated memory
idCVar r_gx2DumpFrame( "r_gx2DumpFrame", "0", CVAR_RENDERER | CVAR_INTEGER, "debug: write frame N to dhewm3frame.tga on the SD card" );

GLExtension_t GX2GL_GetProcAddress( const char *name );

/*
===============
ProcUI callbacks

The scan buffers have to live in the foreground bucket, which the system takes
away while the HOME menu is open.
===============
*/
static uint32_t AcquireForeground( void *context ) {
	MEMHeapHandle fg = MEMGetBaseHeapHandle( MEM_BASE_HEAP_FG );
	tvScanBuffer = MEMAllocFromFrmHeapEx( fg, tvScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT );
	drcScanBuffer = MEMAllocFromFrmHeapEx( fg, drcScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT );
	if ( !tvScanBuffer || !drcScanBuffer ) {
		common->Warning( "GX2: couldn't allocate the scan buffers" );
		return -1;
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU, tvScanBuffer, tvScanBufferSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU, drcScanBuffer, drcScanBufferSize );
	GX2SetTVBuffer( tvScanBuffer, tvScanBufferSize, TV_RENDER_MODE, SCAN_FORMAT, GX2_BUFFERING_MODE_DOUBLE );
	GX2SetDRCBuffer( drcScanBuffer, drcScanBufferSize, GX2_DRC_RENDER_MODE_SINGLE, SCAN_FORMAT, GX2_BUFFERING_MODE_DOUBLE );
	gx2Foreground = true;
	return 0;
}

static uint32_t ReleaseForeground( void *context ) {
	if ( gx2Foreground ) {
		GX2DrawDone();
		MEMFreeToFrmHeap( MEMGetBaseHeapHandle( MEM_BASE_HEAP_FG ), MEM_FRM_HEAP_FREE_ALL );
		tvScanBuffer = drcScanBuffer = NULL;
		gx2Foreground = false;
		displaysEnabled = false;
	}
	return 0;
}

static void *AllocSurface( GX2Surface &surface ) {
	GX2CalcSurfaceSizeAndAlignment( &surface );
	surface.image = memalign( surface.alignment, surface.imageSize );
	memset( surface.image, 0, surface.imageSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU, surface.image, surface.imageSize );
	return surface.image;
}

bool GLimp_Init( glimpParms_t parms ) {
	if ( gx2Initialized ) {
		return true;
	}
	common->Printf( "Initializing GX2 (%dx%d)\n", parms.width, parms.height );

	commandBufferPool = memalign( GX2_COMMAND_BUFFER_ALIGNMENT, GX2_COMMAND_BUFFER_SIZE );
	uint32_t initAttribs[] = {
		GX2_INIT_CMD_BUF_BASE, (uint32_t)(uintptr_t)commandBufferPool,
		GX2_INIT_CMD_BUF_POOL_SIZE, GX2_COMMAND_BUFFER_SIZE,
		GX2_INIT_ARGC, 0,
		GX2_INIT_ARGV, 0,
		GX2_INIT_END
	};
	GX2Init( initAttribs );

	uint32_t unused;
	GX2CalcTVSize( TV_RENDER_MODE, SCAN_FORMAT, GX2_BUFFERING_MODE_DOUBLE, &tvScanBufferSize, &unused );
	GX2CalcDRCSize( GX2_DRC_RENDER_MODE_SINGLE, SCAN_FORMAT, GX2_BUFFERING_MODE_DOUBLE, &drcScanBufferSize, &unused );
	ProcUIRegisterCallback( PROCUI_CALLBACK_ACQUIRE, AcquireForeground, NULL, 100 );
	ProcUIRegisterCallback( PROCUI_CALLBACK_RELEASE, ReleaseForeground, NULL, 100 );
	if ( AcquireForeground( NULL ) != 0 ) {
		return false;
	}
	GX2SetTVScale( 1280, 720 );
	GX2SetDRCScale( 854, 480 );

	// the render target, in MEM2 so it survives the HOME menu
	memset( &gx2ColorBuffer, 0, sizeof( gx2ColorBuffer ) );
	gx2ColorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	gx2ColorBuffer.surface.width = parms.width;
	gx2ColorBuffer.surface.height = parms.height;
	gx2ColorBuffer.surface.depth = 1;
	gx2ColorBuffer.surface.mipLevels = 1;
	gx2ColorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	gx2ColorBuffer.surface.aa = GX2_AA_MODE1X;
	gx2ColorBuffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
	gx2ColorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
	gx2ColorBuffer.viewNumSlices = 1;
	AllocSurface( gx2ColorBuffer.surface );
	GX2InitColorBufferRegs( &gx2ColorBuffer );

	memset( &gx2DepthBuffer, 0, sizeof( gx2DepthBuffer ) );
	gx2DepthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	gx2DepthBuffer.surface.width = parms.width;
	gx2DepthBuffer.surface.height = parms.height;
	gx2DepthBuffer.surface.depth = 1;
	gx2DepthBuffer.surface.mipLevels = 1;
	gx2DepthBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
	gx2DepthBuffer.surface.aa = GX2_AA_MODE1X;
	gx2DepthBuffer.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
	gx2DepthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
	gx2DepthBuffer.viewNumSlices = 1;
	gx2DepthBuffer.depthClear = 1.0f;
	gx2DepthBuffer.stencilClear = 0;
	AllocSurface( gx2DepthBuffer.surface );
	GX2InitDepthBufferRegs( &gx2DepthBuffer );

	memset( &gx2DepthColorBuffer, 0, sizeof( gx2DepthColorBuffer ) );
	gx2DepthColorBuffer.surface = gx2ColorBuffer.surface;
	// depth packed into RGBA8 by the shaders, float formats didn't work in Cemu
	gx2DepthColorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	gx2DepthColorBuffer.surface.use = (GX2SurfaceUse)( GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER );
	gx2DepthColorBuffer.surface.image = NULL;
	gx2DepthColorBuffer.viewNumSlices = 1;
	AllocSurface( gx2DepthColorBuffer.surface );
	GX2InitColorBufferRegs( &gx2DepthColorBuffer );

	gx2ContextState = (GX2ContextState *)memalign( GX2_CONTEXT_STATE_ALIGNMENT, sizeof( GX2ContextState ) );
	GX2SetupContextStateEx( gx2ContextState, TRUE );
	GX2SetContextState( gx2ContextState );
	GX2SetColorBuffer( &gx2ColorBuffer, GX2_RENDER_TARGET_0 );
	GX2SetColorBuffer( &gx2DepthColorBuffer, GX2_RENDER_TARGET_1 );
	GX2SetDepthBuffer( &gx2DepthBuffer );
	GX2SetViewport( 0, 0, parms.width, parms.height, 0.0f, 1.0f );
	GX2SetScissor( 0, 0, parms.width, parms.height );
	GX2SetSwapInterval( 1 );

	GX2GL_Init();

	gx2Initialized = true;
	curParms = parms;
	glConfig.isFullscreen = true;
	glConfig.displayFrequency = 60;
	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;
	return true;
}

void GLimp_Shutdown( void ) {
	if ( !gx2Initialized ) {
		return;
	}
	GX2GL_Shutdown();
	WiiU_KeyboardShutdown();
	if ( drcContextState ) {
		free( drcColorBuffer.surface.image );
		free( drcDepthBuffer.surface.image );
		free( drcContextState );
		drcContextState = NULL;
	}
	GX2SetTVEnable( FALSE );
	GX2SetDRCEnable( FALSE );
	ReleaseForeground( NULL );
	GX2Shutdown();
	free( gx2ColorBuffer.surface.image );
	free( gx2DepthBuffer.surface.image );
	free( gx2DepthColorBuffer.surface.image );
	free( gx2ContextState );
	free( commandBufferPool );
	gx2Initialized = false;
}

/*
===============
DumpFrame

debug helper: writes the current color buffer as a .tga to the SD card
===============
*/
static void DumpFrame( void ) {
	GX2Surface lin;
	memset( &lin, 0, sizeof( lin ) );
	lin.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	lin.width = gx2ColorBuffer.surface.width;
	lin.height = gx2ColorBuffer.surface.height;
	lin.depth = 1;
	lin.mipLevels = 1;
	lin.format = gx2ColorBuffer.surface.format;
	lin.aa = GX2_AA_MODE1X;
	lin.use = GX2_SURFACE_USE_TEXTURE;
	lin.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &lin );
	lin.image = memalign( lin.alignment, lin.imageSize );
	if ( !lin.image ) {
		return;
	}
	GX2CopySurface( &gx2ColorBuffer.surface, 0, 0, &lin, 0, 0 );
	GX2DrawDone();
	DCInvalidateRange( lin.image, lin.imageSize );
	GX2GL_RestoreState();

	idStr path;
	Sys_GetPath( PATH_SAVE, path );
	path += "/dhewm3frame.tga";
	FILE *f = fopen( path.c_str(), "wb" );
	if ( f ) {
		byte header[18] = { 0 };
		header[2] = 2;		// uncompressed true color
		header[12] = lin.width & 255;
		header[13] = lin.width >> 8;
		header[14] = lin.height & 255;
		header[15] = lin.height >> 8;
		header[16] = 32;
		header[17] = 0x28;	// top-left origin, 8 bits alpha
		fwrite( header, 1, sizeof( header ), f );
		byte *row = (byte *)malloc( lin.width * 4 );
		for ( uint32_t y = 0; y < lin.height; y++ ) {
			const byte *src = (const byte *)lin.image + y * lin.pitch * 4;
			for ( uint32_t x = 0; x < lin.width; x++ ) {
				row[x*4+0] = src[x*4+2];
				row[x*4+1] = src[x*4+1];
				row[x*4+2] = src[x*4+0];
				row[x*4+3] = src[x*4+3];
			}
			fwrite( row, 1, lin.width * 4, f );
		}
		free( row );
		fclose( f );
		common->Printf( "wrote %s\n", path.c_str() );
	}
	free( lin.image );
}

/*
===============
Software keyboard on the GamePad

While the keyboard is open, the GamePad shows just the keyboard, drawn at its
native resolution into its own buffer, so touching the keys works.
===============
*/
static void DrawKeyboardDRC( void ) {
	if ( !drcContextState ) {
		memset( &drcColorBuffer, 0, sizeof( drcColorBuffer ) );
		drcColorBuffer.surface = gx2ColorBuffer.surface;
		drcColorBuffer.surface.width = 854;
		drcColorBuffer.surface.height = 480;
		drcColorBuffer.surface.image = NULL;
		drcColorBuffer.viewNumSlices = 1;
		AllocSurface( drcColorBuffer.surface );
		GX2InitColorBufferRegs( &drcColorBuffer );

		memset( &drcDepthBuffer, 0, sizeof( drcDepthBuffer ) );
		drcDepthBuffer.surface = gx2DepthBuffer.surface;
		drcDepthBuffer.surface.width = 854;
		drcDepthBuffer.surface.height = 480;
		drcDepthBuffer.surface.image = NULL;
		drcDepthBuffer.viewNumSlices = 1;
		drcDepthBuffer.depthClear = 1.0f;
		AllocSurface( drcDepthBuffer.surface );
		GX2InitDepthBufferRegs( &drcDepthBuffer );

		drcContextState = (GX2ContextState *)memalign( GX2_CONTEXT_STATE_ALIGNMENT, sizeof( GX2ContextState ) );
		GX2SetupContextStateEx( drcContextState, TRUE );
		GX2SetContextState( drcContextState );
		GX2SetColorBuffer( &drcColorBuffer, GX2_RENDER_TARGET_0 );
		GX2SetDepthBuffer( &drcDepthBuffer );
		GX2SetViewport( 0, 0, 854, 480, 0.0f, 1.0f );
		GX2SetScissor( 0, 0, 854, 480 );
	}
	GX2ClearColor( &drcColorBuffer, 0.0f, 0.0f, 0.0f, 1.0f );
	GX2ClearDepthStencilEx( &drcDepthBuffer, 1.0f, 0, GX2_CLEAR_FLAGS_BOTH );
	GX2SetContextState( drcContextState );
	WiiU_KeyboardDrawDRC();
	GX2CopyColorBufferToScanBuffer( &drcColorBuffer, GX2_SCAN_TARGET_DRC );
	GX2GL_RestoreState();
}

/*
===============
Profiling

Every r_wiiuProfile seconds, logs where the time of the average frame went:
game logic, the renderer's frontend (culling, interactions, shadow volumes) and
backend (issuing the draws), and waiting for the GPU. If the GPU wait is small,
the CPU is the bottleneck.
===============
*/
static struct {
	double	periodStart;
	double	lastSwap;
	int		frames;
	double	frameMs, worstFrameMs, gpuWaitMs;
	int		gameMs, frontendMs, backendMs, draws;
} prof;

static void ProfileFrame( double gpuWaitMs, int draws ) {
	double now = Sys_MillisecondsPrecise();
	if ( prof.lastSwap > 0.0 ) {
		double frameMs = now - prof.lastSwap;
		prof.frameMs += frameMs;
		prof.worstFrameMs = Max( prof.worstFrameMs, frameMs );
		prof.frames++;
	}
	prof.lastSwap = now;
	prof.gpuWaitMs += gpuWaitMs;
	prof.draws += draws;
	// time_frontend / time_backend are set every frame on the Wii U (Session.cpp),
	// time_gameFrame adds up and is reset by com_speeds if that's on
	prof.gameMs += time_gameFrame;
	prof.frontendMs += time_frontend;
	prof.backendMs += time_backend;
	if ( !com_speeds.GetBool() ) {
		time_gameFrame = 0;
	}

	int interval = r_wiiuProfile.GetInteger();
	if ( interval <= 0 ) {
		memset( &prof, 0, sizeof( prof ) );
		prof.lastSwap = now;
		return;
	}
	if ( prof.periodStart <= 0.0 ) {
		prof.periodStart = now;
	}
	double elapsed = now - prof.periodStart;
	if ( elapsed >= interval * 1000.0 && prof.frames > 0 ) {
		float n = prof.frames;
		common->Printf( "Wii U perf: %.1f fps | frame avg %.1f ms, worst %.0f ms | game %.1f, render frontend %.1f, backend %.1f, GPU wait %.1f ms | %.0f draws | %d MB in use\n",
			prof.frames * 1000.0 / elapsed, prof.frameMs / n, prof.worstFrameMs,
			prof.gameMs / n, prof.frontendMs / n, prof.backendMs / n, prof.gpuWaitMs / n, prof.draws / n, Wiiu_MemoryUsedMB() );
		double last = prof.lastSwap;
		memset( &prof, 0, sizeof( prof ) );
		prof.lastSwap = last;
		prof.periodStart = now;
	}
}

void GLimp_SwapBuffers( void ) {
	if ( !gx2Initialized ) {
		return;
	}
	int draws = GX2GL_FrameDraws();
	if ( gx2Foreground ) {
		if ( r_gx2Test.GetInteger() >= 1 && r_gx2Test.GetInteger() <= 3 ) {
			GX2GL_DrawTestPattern( r_gx2Test.GetInteger() );
		}
		static int frameNum;
		if ( ++frameNum == r_gx2DumpFrame.GetInteger() ) {
			DumpFrame();
		}

		if ( r_gx2Test.GetInteger() == 7 ) {
			static int dummyOwner;
			WiiU_KeyboardOpen( &dummyOwner, "Doom 3", 20, false );
		}

		WiiU_KeyboardUpdate();
		bool keyboard = WiiU_KeyboardVisible();
		if ( keyboard ) {
			WiiU_KeyboardDrawTV();
			GX2GL_RestoreState();
		}
		GX2CopyColorBufferToScanBuffer( &gx2ColorBuffer, GX2_SCAN_TARGET_TV );
		if ( keyboard ) {
			DrawKeyboardDRC();
		} else {
			GX2CopyColorBufferToScanBuffer( &gx2ColorBuffer, GX2_SCAN_TARGET_DRC );
		}
		GX2SwapScanBuffers();
		GX2Flush();
	}

	// switches to the next set of per-frame memory, waiting for the GPU if necessary
	double waitStart = Sys_MillisecondsPrecise();
	GX2GL_EndFrame( GX2GetLastSubmittedTimeStamp() );
	ProfileFrame( Sys_MillisecondsPrecise() - waitStart, draws );
	GX2GL_RestoreState();

	if ( gx2Foreground && !displaysEnabled ) {
		// only now, so the displays don't show whatever garbage was in the scan buffers
		GX2SetTVEnable( TRUE );
		GX2SetDRCEnable( TRUE );
		displaysEnabled = true;
	}
}

GLExtension_t GLimp_ExtensionPointer( const char *name ) {
	GLExtension_t proc = GX2GL_GetProcAddress( name );
	if ( proc ) {
		return proc;
	}

	// core GL functions, implemented in gx2_gl.cpp or stubbed in stub_gl.cpp
	struct coreProc_t {
		const char *	name;
		GLExtension_t	proc;
	};
	static const coreProc_t coreProcs[] = {
#define QGLPROC(name, rettype, args) { #name, (GLExtension_t)name },
#include "renderer/qgl_proc.h"
	};
	for ( size_t i = 0; i < sizeof( coreProcs ) / sizeof( coreProcs[0] ); i++ ) {
		if ( idStr::Cmp( coreProcs[i].name, name ) == 0 ) {
			return coreProcs[i].proc;
		}
	}
	return NULL;
}

bool GLimp_SetScreenParms( glimpParms_t parms ) {
	return true;
}

void GLimp_SetGamma( unsigned short red[256], unsigned short green[256], unsigned short blue[256] ) {
}

void GLimp_ResetGamma( void ) {
}

void GLimp_ActivateContext( void ) {
}

void GLimp_DeactivateContext( void ) {
}

void GLimp_GrabInput( int flags ) {
}

bool GLimp_SetSwapInterval( int swapInterval ) {
	GX2SetSwapInterval( swapInterval );
	return true;
}

int GLimp_GetSwapInterval( void ) {
	return GX2GetSwapInterval();
}

void GLimp_UpdateWindowSize( void ) {
}

bool GLimp_SetWindowResizable( bool enableResizable ) {
	return false;
}

glimpParms_t GLimp_GetCurState( void ) {
	return curParms;
}

float GLimp_GetDisplayRefresh( void ) {
	return 60.0f;
}

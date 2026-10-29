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

// Shared between the Wii U GX2 "OpenGL" implementation (gx2_gl.cpp) and its
// window/display setup (gx2_glimp.cpp)

#ifndef __GX2_LOCAL_H__
#define __GX2_LOCAL_H__

#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <gx2/context.h>
#include <gx2/surface.h>

// the single render target everything is drawn into; it's copied to the TV
// and the GamePad scan buffers when the frame is done
extern GX2ColorBuffer	gx2ColorBuffer;
extern GX2DepthBuffer	gx2DepthBuffer;
// render target 1: every draw that writes depth also writes it here (packed into
// RGBA8 by the shaders), so it can be copied into depth textures. Converting the
// depth buffer, sampling render targets and float formats all read as zeros in Cemu
extern GX2ColorBuffer	gx2DepthColorBuffer;
extern GX2ContextState*	gx2ContextState;
extern bool				gx2Foreground;	// false while the HOME menu owns the GPU

// the thread that runs the renderer's back end, NULL before it was started
extern OSThread *		gx2RenderThread;

// GL layer
void	GX2GL_Init( void );
void	GX2GL_Shutdown( void );
// called after anything (clears, surface copies) that clobbered the GX2 state
void	GX2GL_RestoreState( void );
// called once per frame after it was submitted (GX2Flush()), with the GPU timestamp
// it will reach then. Switches to the next set of per-frame memory, which may mean
// waiting for the GPU to finish the frame before
void	GX2GL_EndFrame( OSTime submitted );
// draws in the current frame so far
int		GX2GL_FrameDraws( void );

// per-frame scratch memory the GPU can read (vertex data for immediate mode,
// expanded attributes, texture upload staging), valid until GX2GL_EndFrame()
void *	GX2GL_FrameAlloc( size_t size, size_t align = 64 );

void	GX2GL_DrawTestPattern( int mode );

#endif

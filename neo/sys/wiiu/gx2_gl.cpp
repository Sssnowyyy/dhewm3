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
The subset of OpenGL 1.x (+ ARB_vertex_buffer_object, ARB_multitexture and
the ARB_vertex/fragment_program entry points) that the Doom 3 renderer uses,
implemented on top of the Wii U's GX2 API.

Everything the renderer calls but this file doesn't implement still comes from
sys/stub/stub_gl.cpp, whose definitions are weak symbols on the Wii U.

Shaders are GLSL (sys/wiiu/shaders/), compiled at build time:
- "ff" emulates the fixed function pipeline as far as the renderer uses it:
  2 texture units, object linear texgen, texture matrices, a few env modes
- ARB programs can't be compiled at runtime, so the renderer tells us the file
  name of each one (GX2GL_NameProgram()) and we use the GLSL port with that name;
  draws with programs that weren't ported are skipped

Current limitations:
- glClear() ignores the scissor rect, no clip planes, no depth bounds test
- reflection map texgen isn't supported (the ARB2 backend uses programs for it)
*/

#include <malloc.h>
#include <coreinit/mutex.h>
#include <coreinit/memorymap.h>

#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include "sys/platform.h"
#include "renderer/tr_local.h"

#include "sys/wiiu/gx2_local.h"
#include "sys/wiiu/wiiu_renderthread.h"

#include "sys/wiiu/gx2_shaders.h"

// the renderer draws lighting with GL_EQUAL against the depth of an earlier
// depth-only pass, which needs bit-identical positions from different shaders
// (ARB_position_invariant). GX2 shaders, and especially Cemu translating each
// one separately, don't guarantee that.
idCVar r_gx2DepthEqualAsLequal( "r_gx2DepthEqualAsLequal", "1", CVAR_RENDERER | CVAR_BOOL, "use GL_LEQUAL for GL_EQUAL depth tests" );
idCVar r_gx2ShaderDebug( "r_gx2ShaderDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "debug: passed to shaders that have a uDebug uniform" );
idCVar r_gx2Debug( "r_gx2Debug", "0", CVAR_RENDERER | CVAR_INTEGER, "print GX2 GL layer statistics every N frames (0 = never)" );

/*
===============================================================================

	Per-frame GPU memory

===============================================================================
*/

#define FRAME_ARENA_SIZE	( 16 * 1024 * 1024 )
#define NUM_FRAME_SLOTS		2

idCVar r_gx2VerifyTextures( "r_gx2VerifyTextures", "0", CVAR_RENDERER | CVAR_BOOL, "diagnostics: read precompressed textures back from the GPU and compare them with what was uploaded" );
idCVar r_gx2StateCache( "r_gx2StateCache", "1", CVAR_RENDERER | CVAR_BOOL, "only send render state, shaders and textures to the GPU when they change" );
idCVar r_gx2Pipeline( "r_gx2Pipeline", "1", CVAR_RENDERER | CVAR_BOOL, "let the CPU prepare the next frame while the GPU draws the last one (0: wait for the GPU every frame)" );

// Per-frame memory is double buffered, so the CPU can work on the next frame while
// the GPU still draws the previous one: a slot is only reused once the GPU is done
// with the frame that used it last (GX2GL_EndFrame()).
static byte *			frameArenas[NUM_FRAME_SLOTS];
static OSTime			frameSlotTimeStamps[NUM_FRAME_SLOTS];
static idList<void *>	deferredFrees[NUM_FRAME_SLOTS];	// freed when the GPU is done with the slot's frame
static int				frameSlot;
static byte *			frameArena;
static size_t			frameArenaUsed;

// The GPU's vertex/index cache is invalidated once per frame (not per draw, that
// stalls the GPU). Everything the GPU reads in a frame was written before it read
// it: client memory data is copied into the frame's arena (which isn't reused
// within a frame), VBO data is written before the draws that use it.
static void InvalidateAttributeCache( void ) {
	GX2Invalidate( GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, NULL, 0xFFFFFFFF );
}

void *GX2GL_FrameAlloc( size_t size, size_t align ) {
	size_t ofs = ( frameArenaUsed + align - 1 ) & ~( align - 1 );
	if ( ofs + size > FRAME_ARENA_SIZE ) {
		if ( size > FRAME_ARENA_SIZE ) {
			common->FatalError( "GX2GL_FrameAlloc: %u bytes is more than the whole arena", (unsigned)size );
		}
		// let the GPU catch up, then everything already submitted is done with the arena
		if ( gx2Foreground ) {
			GX2DrawDone();
		}
		InvalidateAttributeCache();
		ofs = 0;
	}
	frameArenaUsed = ofs + size;
	return frameArena + ofs;
}

static OSMutex deferredFreeMutex;	// the engine thread frees vertex cache buffers too

static void DeferredFree( void *ptr ) {
	if ( ptr ) {
		OSLockMutex( &deferredFreeMutex );
		deferredFrees[frameSlot].Append( ptr );
		OSUnlockMutex( &deferredFreeMutex );
	}
}

// copies data the GPU will read as vertices / indices into this frame's arena: the
// renderer reuses its own per-frame memory before the GPU has drawn the frame
static void *CopyToFrame( const void *src, size_t size ) {
	void *dst = GX2GL_FrameAlloc( size, 64 );
	memcpy( dst, src, size );
	DCFlushRange( dst, size );
	return dst;
}

/*
===============================================================================

	State

===============================================================================
*/

#define MAX_UNITS			8
#define MAX_VERTEX_ATTRIBS	16

struct glArray_t {
	bool			enabled;
	GLint			size;
	GLenum			type;
	GLsizei			stride;
	const void *	ptr;
	GLuint			buffer;		// VBO bound when the pointer was set, ptr is an offset into it then
};

struct glTexUnit_t {
	bool			enable2D;
	bool			enableCube;
	GLuint			bound2D;
	GLuint			boundCube;
	GLenum			envMode;
	GLenum			combineRGB, source1RGB, operand1RGB;	// the GL_COMBINE_ARB bits the renderer uses
	float			envColor[4];
	bool			texGenEnabled[4];
	GLenum			texGenMode[4];
	float			texGenPlane[4][4];
	glArray_t		texCoordArray;
	float			currentTexCoord[4];
};

struct glMatrixStack_t {
	float			m[32][16];
	int				depth;
};

struct glBuffer_t {
	byte *			data;
	GLsizeiptrARB	size;
};

struct glTexture_t {
	GLenum			target;			// GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP_EXT, 0 until first bound
	GX2Texture		tex;
	bool			allocated;
	GLenum			internalFormat;
	int				maxLevel;		// highest uploaded mip level
	GLenum			minFilter, magFilter, wrapS, wrapT;
	float			maxAniso;
	GX2Sampler		sampler;
	bool			samplerDirty;
	uint32_t		bindVersion;	// changes whenever the texture or sampler registers do
	// diagnostics, see VerifyTexture(): checksum of the precompressed level 0 as uploaded
	bool			verify;
	uint32_t		level0Sum;
};
static uint32_t bindVersionCounter;

// ARB programs are bound by the renderer's program_t idents, without glGenProgramsARB()
#define MAX_PROGRAM_IDENTS	512
#define MAX_LOCAL_PARMS		8

struct gx2Shader_t;
struct glProgram_t {
	gx2Shader_t *	shader;		// GLSL port, NULL if there is none
	float			local[MAX_LOCAL_PARMS][4];
};

static struct {
	// render state
	bool			blend, depthTest, stencilTest, cullFace, alphaTest, scissorTest, polygonOffsetFill;
	bool			vertexProgram, fragmentProgram;
	GLenum			blendSrc, blendDst, blendEquation;
	GLenum			depthFunc;
	bool			depthMask;
	float			depthNear, depthFar;
	bool			colorMask[4];
	GLenum			stencilFunc;
	GLint			stencilRef;
	GLuint			stencilFuncMask;
	GLuint			stencilWriteMask;
	GLenum			stencilFail[2], stencilZFail[2], stencilZPass[2];	// front, back
	GLenum			alphaFunc;
	float			alphaRef;
	GLenum			cullMode;
	GLenum			frontFace;
	float			polyOffsetFactor, polyOffsetUnits;
	GLint			viewport[4];
	GLint			scissor[4];
	float			clearColor[4];
	float			clearDepth;
	GLint			clearStencil;

	// matrices
	GLenum			matrixMode;
	glMatrixStack_t	modelview, projection, texture[MAX_UNITS];

	// textures
	int				activeUnit;
	int				clientActiveUnit;
	glTexUnit_t		units[MAX_UNITS];

	// arrays
	glArray_t		vertexArray, colorArray, normalArray;
	glArray_t		vertexAttribs[MAX_VERTEX_ATTRIBS];
	float			currentColor[4];

	// buffers

	// programs
	GLuint			boundVertexProgram, boundFragmentProgram;
	float			programEnv[2][256][4];	// vertex, fragment
} gl;

static idList<glTexture_t *>	textures;	// index is the GL name
/*
Buffer objects: the engine thread fills vertex cache buffers (the renderer's front
end) while the render thread draws from others (the back end), so these work from
both threads: they're kept in pages that never move, and each thread has its own
buffer bindings. Everything else must only be used by one thread at a time, see
CHECK_THREAD().
*/
#define BUFFER_PAGE_SIZE	1024
#define MAX_BUFFER_PAGES	256
static glBuffer_t **		bufferPages[MAX_BUFFER_PAGES];
static volatile GLuint		numBufferNames = 1;		// 0 is no buffer
static OSMutex				bufferMutex;

struct bufferBindings_t {
	GLuint		arrayBuffer, elementArrayBuffer;
};
static bufferBindings_t		renderThreadBindings, otherBindings;

static bufferBindings_t &Bindings( void ) {
	return OSGetCurrentThread() == gx2RenderThread ? renderThreadBindings : otherBindings;
}

// GL functions that send anything to the GPU (or change state the back end
// uses) wait until the render thread is idle and move to its core when they're
// called from another thread, see gx2_glimp.cpp
#define CHECK_THREAD() \
	if ( gx2RenderThread && OSGetCurrentThread() != gx2RenderThread ) { \
		WiiU_SyncRenderThreadFor( __func__ ); \
	}
static glProgram_t				programs[2][MAX_PROGRAM_IDENTS];	// vertex, fragment

// immediate mode
struct immVertex_t {
	float			xyz[4];
	float			color[4];
	float			st[4];
};
static GLenum					immMode;
static idList<immVertex_t>		immVerts;

// vertex shader inputs, matched by name (see shaders/README.txt)
enum {
	SEM_POSITION,
	SEM_COLOR,
	SEM_NORMAL,
	SEM_TEXCOORD0,		// .. SEM_TEXCOORD0 + MAX_UNITS - 1
	SEM_ATTRIB0 = SEM_TEXCOORD0 + MAX_UNITS,	// .. SEM_ATTRIB0 + MAX_VERTEX_ATTRIBS - 1
	SEM_COUNT = SEM_ATTRIB0 + MAX_VERTEX_ATTRIBS
};

#define MAX_SHADER_ATTRIBS	16
#define MAX_SHADER_SAMPLERS	16

struct gx2Uniform_t {
	int				offset;		// -1 if the shader doesn't have it
	int				count;		// array elements
};

struct gx2Shader_t {
	const char *		name;
	WHBGfxShaderGroup	group;

	int					numAttribs;
	int					attribSemantic[MAX_SHADER_ATTRIBS];
	uint32_t			attribLocation[MAX_SHADER_ATTRIBS];

	// vertex shader
	gx2Uniform_t		mvp, modelView, projection, vsEnv, vsLocal, texGen[2], texMat[2];
	// pixel shader
	gx2Uniform_t		psEnv, psLocal, mode, envColor[2], debug;

	int					numSamplers;
	int					samplerUnit[MAX_SHADER_SAMPLERS];
	bool				samplerCube[MAX_SHADER_SAMPLERS];
	uint32_t			samplerLocation[MAX_SHADER_SAMPLERS];
};

static idList<gx2Shader_t *>	shaders;
static gx2Shader_t *			ffShader;

// bound to samplers that have no (valid) texture
static GLuint					whiteTexture2D, whiteTextureCube;
static GLuint					lastDepthTexture;	// debug: last depth texture copied to

static struct {
	int				draws, programDraws, skippedPrograms, skippedOther, uploads;
} stats;
static int			totalUploads, totalCompressedUploads;	// texture levels since startup

/*
===============================================================================

	Helpers

===============================================================================
*/

static void MatrixIdentity( float *m ) {
	memset( m, 0, 16 * sizeof( float ) );
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b, all column-major
static void MatrixMultiply( const float *a, const float *b, float *out ) {
	float t[16];
	for ( int c = 0; c < 4; c++ ) {
		for ( int r = 0; r < 4; r++ ) {
			t[c*4+r] = a[0*4+r] * b[c*4+0] + a[1*4+r] * b[c*4+1] + a[2*4+r] * b[c*4+2] + a[3*4+r] * b[c*4+3];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

static bool MatrixIsIdentity( const float *m ) {
	static const float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	return memcmp( m, id, sizeof( id ) ) == 0;
}

static glMatrixStack_t *CurrentMatrixStack( void ) {
	switch ( gl.matrixMode ) {
		case GL_PROJECTION:	return &gl.projection;
		case GL_TEXTURE:	return &gl.texture[gl.activeUnit];
		default:			return &gl.modelview;
	}
}

static float *CurrentMatrix( void ) {
	glMatrixStack_t *s = CurrentMatrixStack();
	return s->m[s->depth];
}

static glTexture_t *BoundTexture( int unit, GLenum target ) {
	GLuint name = ( target == GL_TEXTURE_CUBE_MAP_EXT ) ? gl.units[unit].boundCube : gl.units[unit].bound2D;
	if ( name == 0 || (int)name >= textures.Num() ) {
		return NULL;
	}
	return textures[name];
}

static glBuffer_t *Buffer( GLuint name ) {
	if ( name == 0 || name >= numBufferNames ) {
		return NULL;
	}
	return bufferPages[name / BUFFER_PAGE_SIZE][name % BUFFER_PAGE_SIZE];
}

// resolves a gl*Pointer() pointer, which is an offset if a VBO was bound
static const byte *ArrayPointer( const glArray_t &a ) {
	if ( a.buffer ) {
		glBuffer_t *b = Buffer( a.buffer );
		return b ? b->data + (uintptr_t)a.ptr : NULL;
	}
	return (const byte *)a.ptr;
}

static int TypeSize( GLenum type ) {
	switch ( type ) {
		case GL_UNSIGNED_BYTE:	case GL_BYTE:	return 1;
		case GL_UNSIGNED_SHORT:	case GL_SHORT:	return 2;
		default:								return 4;
	}
}

static int ArrayStride( const glArray_t &a ) {
	return a.stride ? a.stride : a.size * TypeSize( a.type );
}

static GX2CompareFunction CompareFunc( GLenum f ) {
	switch ( f ) {
		case GL_NEVER:		return GX2_COMPARE_FUNC_NEVER;
		case GL_LESS:		return GX2_COMPARE_FUNC_LESS;
		case GL_EQUAL:		return GX2_COMPARE_FUNC_EQUAL;
		case GL_LEQUAL:		return GX2_COMPARE_FUNC_LEQUAL;
		case GL_GREATER:	return GX2_COMPARE_FUNC_GREATER;
		case GL_NOTEQUAL:	return GX2_COMPARE_FUNC_NOT_EQUAL;
		case GL_GEQUAL:		return GX2_COMPARE_FUNC_GEQUAL;
		default:			return GX2_COMPARE_FUNC_ALWAYS;
	}
}

static GX2StencilFunction StencilOp( GLenum op ) {
	switch ( op ) {
		case GL_ZERO:			return GX2_STENCIL_FUNCTION_ZERO;
		case GL_REPLACE:		return GX2_STENCIL_FUNCTION_REPLACE;
		case GL_INCR:			return GX2_STENCIL_FUNCTION_INCR_CLAMP;
		case GL_DECR:			return GX2_STENCIL_FUNCTION_DECR_CLAMP;
		case GL_INVERT:			return GX2_STENCIL_FUNCTION_INV;
		case GL_INCR_WRAP_EXT:	return GX2_STENCIL_FUNCTION_INCR_WRAP;
		case GL_DECR_WRAP_EXT:	return GX2_STENCIL_FUNCTION_DECR_WRAP;
		default:				return GX2_STENCIL_FUNCTION_KEEP;
	}
}

static GX2BlendMode BlendFactor( GLenum f ) {
	switch ( f ) {
		case GL_ZERO:					return GX2_BLEND_MODE_ZERO;
		case GL_ONE:					return GX2_BLEND_MODE_ONE;
		case GL_SRC_COLOR:				return GX2_BLEND_MODE_SRC_COLOR;
		case GL_ONE_MINUS_SRC_COLOR:	return GX2_BLEND_MODE_INV_SRC_COLOR;
		case GL_SRC_ALPHA:				return GX2_BLEND_MODE_SRC_ALPHA;
		case GL_ONE_MINUS_SRC_ALPHA:	return GX2_BLEND_MODE_INV_SRC_ALPHA;
		case GL_DST_ALPHA:				return GX2_BLEND_MODE_DST_ALPHA;
		case GL_ONE_MINUS_DST_ALPHA:	return GX2_BLEND_MODE_INV_DST_ALPHA;
		case GL_DST_COLOR:				return GX2_BLEND_MODE_DST_COLOR;
		case GL_ONE_MINUS_DST_COLOR:	return GX2_BLEND_MODE_INV_DST_COLOR;
		case GL_SRC_ALPHA_SATURATE:		return GX2_BLEND_MODE_SRC_ALPHA_SAT;
		default:						return GX2_BLEND_MODE_ONE;
	}
}

static GX2BlendCombineMode BlendEquation( GLenum e ) {
	switch ( e ) {
		case GL_FUNC_SUBTRACT:			return GX2_BLEND_COMBINE_MODE_SUB;
		case GL_FUNC_REVERSE_SUBTRACT:	return GX2_BLEND_COMBINE_MODE_REV_SUB;
		case GL_MIN:					return GX2_BLEND_COMBINE_MODE_MIN;
		case GL_MAX:					return GX2_BLEND_COMBINE_MODE_MAX;
		default:						return GX2_BLEND_COMBINE_MODE_ADD;
	}
}

static GX2TexClampMode ClampMode( GLenum wrap ) {
	switch ( wrap ) {
		case GL_REPEAT:				return GX2_TEX_CLAMP_MODE_WRAP;
		case GL_CLAMP_TO_BORDER:	return GX2_TEX_CLAMP_MODE_CLAMP_BORDER;
		default:					return GX2_TEX_CLAMP_MODE_CLAMP;
	}
}

// GL's window coordinates have y going up, GX2's go down
static int FlipY( int y, int h ) {
	return (int)gx2ColorBuffer.surface.height - ( y + h );
}

/*
===============================================================================

	Init / shutdown / per frame

===============================================================================
*/

static gx2Uniform_t FindUniform( const GX2UniformVar *vars, uint32_t count, const char *name ) {
	gx2Uniform_t u = { -1, 0 };
	for ( uint32_t i = 0; i < count; i++ ) {
		// arrays show up as "name[0]" with some compilers
		const char *n = vars[i].name;
		int len = strlen( name );
		if ( !idStr::Cmpn( n, name, len ) && ( n[len] == '\0' || n[len] == '[' ) ) {
			u.offset = vars[i].offset;
			u.count = vars[i].count ? vars[i].count : 1;
			break;
		}
	}
	return u;
}

static int AttribSemantic( const char *name ) {
	if ( !idStr::Cmp( name, "aPosition" ) )		return SEM_POSITION;
	if ( !idStr::Cmp( name, "aColor" ) )		return SEM_COLOR;
	if ( !idStr::Cmp( name, "aNormal" ) )		return SEM_NORMAL;
	if ( !idStr::Cmp( name, "aTexCoord" ) )		return SEM_TEXCOORD0;
	if ( !idStr::Cmpn( name, "aTexCoord", 9 ) )	return SEM_TEXCOORD0 + atoi( name + 9 );
	if ( !idStr::Cmpn( name, "aAttrib", 7 ) )	return SEM_ATTRIB0 + atoi( name + 7 );
	return -1;
}

static gx2Shader_t *LoadShader( const char *name, const void *gsh ) {
	gx2Shader_t *sh = new gx2Shader_t;
	memset( sh, 0, sizeof( *sh ) );
	sh->name = name;
	if ( !WHBGfxLoadGFDShaderGroup( &sh->group, 0, gsh ) ) {
		common->Warning( "GX2GL: couldn't load shader %s", name );
		delete sh;
		return NULL;
	}

	const GX2VertexShader *vs = sh->group.vertexShader;
	for ( uint32_t i = 0; i < vs->attribVarCount && sh->numAttribs < MAX_SHADER_ATTRIBS; i++ ) {
		int sem = AttribSemantic( vs->attribVars[i].name );
		if ( sem < 0 || sem >= SEM_COUNT ) {
			common->Warning( "GX2GL: shader %s has unknown input %s", name, vs->attribVars[i].name );
			continue;
		}
		sh->attribSemantic[sh->numAttribs] = sem;
		sh->attribLocation[sh->numAttribs] = vs->attribVars[i].location;
		sh->numAttribs++;
	}
	sh->mvp = FindUniform( vs->uniformVars, vs->uniformVarCount, "uMVP" );
	sh->modelView = FindUniform( vs->uniformVars, vs->uniformVarCount, "uModelView" );
	sh->projection = FindUniform( vs->uniformVars, vs->uniformVarCount, "uProjection" );
	sh->vsEnv = FindUniform( vs->uniformVars, vs->uniformVarCount, "uEnv" );
	sh->vsLocal = FindUniform( vs->uniformVars, vs->uniformVarCount, "uLocal" );
	sh->texGen[0] = FindUniform( vs->uniformVars, vs->uniformVarCount, "uTexGen0" );
	sh->texGen[1] = FindUniform( vs->uniformVars, vs->uniformVarCount, "uTexGen1" );
	sh->texMat[0] = FindUniform( vs->uniformVars, vs->uniformVarCount, "uTexMat0" );
	sh->texMat[1] = FindUniform( vs->uniformVars, vs->uniformVarCount, "uTexMat1" );

	const GX2PixelShader *ps = sh->group.pixelShader;
	sh->psEnv = FindUniform( ps->uniformVars, ps->uniformVarCount, "uEnv" );
	sh->psLocal = FindUniform( ps->uniformVars, ps->uniformVarCount, "uLocal" );
	sh->mode = FindUniform( ps->uniformVars, ps->uniformVarCount, "uMode" );
	sh->envColor[0] = FindUniform( ps->uniformVars, ps->uniformVarCount, "uEnvColor0" );
	sh->envColor[1] = FindUniform( ps->uniformVars, ps->uniformVarCount, "uEnvColor1" );
	sh->debug = FindUniform( ps->uniformVars, ps->uniformVarCount, "uDebug" );

	// samplers: tex<N> / texCube<N> is texture unit N
	for ( uint32_t i = 0; i < ps->samplerVarCount && sh->numSamplers < MAX_SHADER_SAMPLERS; i++ ) {
		const char *n = ps->samplerVars[i].name;
		if ( idStr::Cmpn( n, "tex", 3 ) ) {
			continue;
		}
		n += 3;
		if ( !idStr::Cmpn( n, "Cube", 4 ) ) {
			n += 4;
		}
		sh->samplerUnit[sh->numSamplers] = Min( atoi( n ), MAX_UNITS - 1 );
		sh->samplerCube[sh->numSamplers] = ps->samplerVars[i].type == GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE;
		sh->samplerLocation[sh->numSamplers] = ps->samplerVars[i].location;
		sh->numSamplers++;
	}

	if ( r_gx2Debug.GetInteger() ) {
		common->Printf( "GX2GL: shader %s: %d inputs, %d samplers, mvp %d, env %d/%d\n", name, sh->numAttribs, sh->numSamplers,
			sh->mvp.offset, sh->vsEnv.offset, sh->psEnv.offset );
	}
	return sh;
}

static gx2Shader_t *FindShader( const char *name ) {
	for ( int i = 0; i < shaders.Num(); i++ ) {
		if ( !idStr::Icmp( shaders[i]->name, name ) ) {
			return shaders[i];
		}
	}
	return NULL;
}

extern "C" {
static GLuint CreateWhiteTexture( GLenum target );
}

void GX2GL_Init( void ) {
	for ( int i = 0; i < NUM_FRAME_SLOTS; i++ ) {
		frameArenas[i] = (byte *)memalign( 256, FRAME_ARENA_SIZE );
		frameSlotTimeStamps[i] = 0;
	}
	frameSlot = 0;
	frameArena = frameArenas[0];
	frameArenaUsed = 0;

	for ( int i = 0; gx2EmbeddedShaders[i].name; i++ ) {
		gx2Shader_t *sh = LoadShader( gx2EmbeddedShaders[i].name, gx2EmbeddedShaders[i].gsh );
		if ( sh ) {
			shaders.Append( sh );
		}
	}
	ffShader = FindShader( "ff" );
	if ( !ffShader ) {
		common->FatalError( "GX2GL: the fixed function shader is missing" );
	}

	memset( &gl, 0, sizeof( gl ) );
	gl.blendSrc = GL_ONE;
	gl.blendDst = GL_ZERO;
	gl.blendEquation = GL_FUNC_ADD;
	gl.depthFunc = GL_LESS;
	gl.depthMask = true;
	gl.depthFar = 1.0f;
	gl.colorMask[0] = gl.colorMask[1] = gl.colorMask[2] = gl.colorMask[3] = true;
	gl.stencilFunc = GL_ALWAYS;
	gl.stencilFuncMask = gl.stencilWriteMask = 0xff;
	for ( int i = 0; i < 2; i++ ) {
		gl.stencilFail[i] = gl.stencilZFail[i] = gl.stencilZPass[i] = GL_KEEP;
	}
	gl.alphaFunc = GL_ALWAYS;
	gl.cullMode = GL_BACK;
	gl.frontFace = GL_CCW;
	gl.clearDepth = 1.0f;
	gl.matrixMode = GL_MODELVIEW;
	MatrixIdentity( gl.modelview.m[0] );
	MatrixIdentity( gl.projection.m[0] );
	for ( int i = 0; i < MAX_UNITS; i++ ) {
		MatrixIdentity( gl.texture[i].m[0] );
		gl.units[i].envMode = GL_MODULATE;
		gl.units[i].combineRGB = GL_MODULATE;
		gl.units[i].source1RGB = GL_PREVIOUS_ARB;
		gl.units[i].operand1RGB = GL_SRC_COLOR;
		gl.units[i].currentTexCoord[3] = 1.0f;
		for ( int j = 0; j < 4; j++ ) {
			gl.units[i].texGenMode[j] = GL_EYE_LINEAR;
		}
	}
	gl.currentColor[0] = gl.currentColor[1] = gl.currentColor[2] = gl.currentColor[3] = 1.0f;
	gl.viewport[2] = gl.scissor[2] = gx2ColorBuffer.surface.width;
	gl.viewport[3] = gl.scissor[3] = gx2ColorBuffer.surface.height;

	// name 0 is never valid
	textures.Clear();
	textures.Append( NULL );
	OSInitMutex( &bufferMutex );
	OSInitMutex( &deferredFreeMutex );
	numBufferNames = 1;
	memset( &renderThreadBindings, 0, sizeof( renderThreadBindings ) );
	memset( &otherBindings, 0, sizeof( otherBindings ) );
	memset( programs, 0, sizeof( programs ) );

	GX2GL_RestoreState();

	whiteTexture2D = CreateWhiteTexture( GL_TEXTURE_2D );
	whiteTextureCube = CreateWhiteTexture( GL_TEXTURE_CUBE_MAP_EXT );
}

void GX2GL_Shutdown( void ) {
	// after ProcUI took the foreground (exiting from the HOME menu) the GPU
	// doesn't run our commands anymore, waiting for it would hang
	if ( gx2Foreground ) {
		GX2DrawDone();
	}
	for ( int i = 0; i < NUM_FRAME_SLOTS; i++ ) {
		for ( int j = 0; j < deferredFrees[i].Num(); j++ ) {
			free( deferredFrees[i][j] );
		}
		deferredFrees[i].Clear();
	}
	for ( int i = 0; i < shaders.Num(); i++ ) {
		WHBGfxFreeShaderGroup( &shaders[i]->group );
	}
	shaders.DeleteContents( true );
	ffShader = NULL;
	for ( int i = 0; i < textures.Num(); i++ ) {
		if ( textures[i] ) {
			free( textures[i]->tex.surface.image );
			delete textures[i];
		}
	}
	textures.Clear();
	for ( int p = 0; p < MAX_BUFFER_PAGES; p++ ) {
		if ( !bufferPages[p] ) {
			continue;
		}
		for ( int i = 0; i < BUFFER_PAGE_SIZE; i++ ) {
			if ( bufferPages[p][i] ) {
				free( bufferPages[p][i]->data );
				delete bufferPages[p][i];
			}
		}
		free( bufferPages[p] );
		bufferPages[p] = NULL;
	}
	numBufferNames = 1;
	for ( int i = 0; i < NUM_FRAME_SLOTS; i++ ) {
		free( frameArenas[i] );
		frameArenas[i] = NULL;
	}
	frameArena = NULL;
}

/*
What was last sent to the GPU, so draws only send what changed. Forgotten when
something else may have changed the registers (clears, copies, the keyboard...).
*/
struct depthStencilState_t {
	int		depthTest, depthWrite, depthFunc, stencilTest, twoSided, stencilFunc, ops[6], funcMask, writeMask, ref;
};
struct blendState_t {
	int		src, dst, equation, blend, colorMask, depthMask, alphaTest, alphaFunc;
	float	alphaRef;
};
struct rasterState_t {
	int		frontFace, cullFront, cullBack, offsetFill;
	float	offsetUnits, offsetFactor;
};
struct viewState_t {
	float	viewport[4], depthNear, depthFar;
	int		scissor[4];
};
#define MAX_PIXEL_SAMPLERS 16
static struct {
	bool					valid;
	depthStencilState_t		depthStencil;
	blendState_t			blend;
	rasterState_t			raster;
	viewState_t				view;
	GX2FetchShader *		fetchShader;
	GX2VertexShader *		vertexShader;
	GX2PixelShader *		pixelShader;
	glTexture_t *			texture[MAX_PIXEL_SAMPLERS];
	uint32_t				textureVersion[MAX_PIXEL_SAMPLERS];
} applied;

static void InvalidateStateCache( void ) {
	memset( &applied, 0, sizeof( applied ) );
}

// true if the state differs from what was sent last, which is then updated
template<class T> static bool StateChanged( T &last, const T &now ) {
	if ( applied.valid && memcmp( &last, &now, sizeof( T ) ) == 0 ) {
		return false;
	}
	last = now;
	return true;
}

void GX2GL_RestoreState( void ) {
	InvalidateStateCache();
	GX2SetContextState( gx2ContextState );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );
}

int GX2GL_FrameDraws( void ) {
	return stats.draws;
}

void GX2GL_EndFrame( OSTime submitted ) {
	frameSlotTimeStamps[frameSlot] = submitted;
	frameSlot = ( frameSlot + 1 ) % NUM_FRAME_SLOTS;

	// wait until the GPU is done with the last frame that used the next slot
	if ( !gx2Foreground ) {
		// it doesn't run our commands while the HOME menu has it
	} else if ( r_gx2Pipeline.GetBool() ) {
		GX2WaitTimeStamp( frameSlotTimeStamps[frameSlot] );
	} else {
		GX2DrawDone();
	}
	OSLockMutex( &deferredFreeMutex );
	for ( int i = 0; i < deferredFrees[frameSlot].Num(); i++ ) {
		free( deferredFrees[frameSlot][i] );
	}
	deferredFrees[frameSlot].Clear();
	OSUnlockMutex( &deferredFreeMutex );
	frameArena = frameArenas[frameSlot];
	frameArenaUsed = 0;
	InvalidateAttributeCache();

	static int frameNum;
	frameNum++;
	if ( r_gx2Debug.GetInteger() > 0 && frameNum % r_gx2Debug.GetInteger() == 0 ) {
		common->Printf( "GX2GL frame %d: %d draws (%d with programs), %d skipped (no GLSL port), %d skipped (other), %d texture uploads"
			" (since start: %d uncompressed, %d precompressed levels)\n",
			frameNum, stats.draws, stats.programDraws, stats.skippedPrograms, stats.skippedOther, stats.uploads,
			totalUploads, totalCompressedUploads );
	}
	memset( &stats, 0, sizeof( stats ) );
}

/*
===============================================================================

	Drawing

===============================================================================
*/

static void ApplyRenderState( void ) {
	if ( !r_gx2StateCache.GetBool() ) {
		InvalidateStateCache();
	}

	// depth / stencil
	depthStencilState_t ds;
	memset( &ds, 0, sizeof( ds ) );
	ds.depthTest = gl.depthTest;
	ds.depthWrite = gl.depthTest && gl.depthMask;
	ds.depthFunc = gl.depthFunc;
	if ( ds.depthFunc == GL_EQUAL && r_gx2DepthEqualAsLequal.GetBool() ) {
		ds.depthFunc = GL_LEQUAL;
	}
	ds.stencilTest = gl.stencilTest;
	ds.twoSided = gl.stencilFail[0] != gl.stencilFail[1] || gl.stencilZFail[0] != gl.stencilZFail[1] || gl.stencilZPass[0] != gl.stencilZPass[1];
	ds.stencilFunc = gl.stencilFunc;
	for ( int i = 0; i < 2; i++ ) {
		ds.ops[i*3+0] = gl.stencilZPass[i];
		ds.ops[i*3+1] = gl.stencilZFail[i];
		ds.ops[i*3+2] = gl.stencilFail[i];
	}
	ds.funcMask = gl.stencilFuncMask;
	ds.writeMask = gl.stencilWriteMask;
	ds.ref = gl.stencilRef;
	if ( StateChanged( applied.depthStencil, ds ) ) {
		GX2SetDepthStencilControl( ds.depthTest, ds.depthWrite, CompareFunc( ds.depthFunc ),
			ds.stencilTest, ds.twoSided,
			CompareFunc( ds.stencilFunc ), StencilOp( ds.ops[0] ), StencilOp( ds.ops[1] ), StencilOp( ds.ops[2] ),
			CompareFunc( ds.stencilFunc ), StencilOp( ds.ops[3] ), StencilOp( ds.ops[4] ), StencilOp( ds.ops[5] ) );
		GX2SetStencilMask( ds.funcMask, ds.writeMask, ds.ref, ds.funcMask, ds.writeMask, ds.ref );
	}

	// blending
	blendState_t bs;
	memset( &bs, 0, sizeof( bs ) );
	bs.src = gl.blendSrc;
	bs.dst = gl.blendDst;
	bs.equation = gl.blendEquation;
	bs.blend = gl.blend;
	bs.colorMask = ( gl.colorMask[0] ? 1 : 0 ) | ( gl.colorMask[1] ? 2 : 0 ) | ( gl.colorMask[2] ? 4 : 0 ) | ( gl.colorMask[3] ? 8 : 0 );
	// render target 1 mirrors the depth buffer: only written by draws that write depth
	bs.depthMask = ( gl.depthTest && gl.depthMask ) ? GX2_CHANNEL_MASK_RGBA : 0;
	bs.alphaTest = gl.alphaTest;
	bs.alphaFunc = gl.alphaFunc;
	bs.alphaRef = gl.alphaRef;
	if ( StateChanged( applied.blend, bs ) ) {
		GX2SetBlendControl( GX2_RENDER_TARGET_0, BlendFactor( bs.src ), BlendFactor( bs.dst ), BlendEquation( bs.equation ),
			TRUE, BlendFactor( bs.src ), BlendFactor( bs.dst ), BlendEquation( bs.equation ) );
		GX2SetColorControl( GX2_LOGIC_OP_COPY, bs.blend ? 1 : 0, FALSE, TRUE );
		GX2SetTargetChannelMasks( (GX2ChannelMask)bs.colorMask, (GX2ChannelMask)bs.depthMask, (GX2ChannelMask)0, (GX2ChannelMask)0,
			(GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0 );
		GX2SetAlphaTest( bs.alphaTest, CompareFunc( bs.alphaFunc ), bs.alphaRef );
	}

	// rasterizer
	rasterState_t rs;
	memset( &rs, 0, sizeof( rs ) );
	rs.frontFace = gl.frontFace;
	rs.cullFront = gl.cullFace && ( gl.cullMode == GL_FRONT || gl.cullMode == GL_FRONT_AND_BACK );
	rs.cullBack = gl.cullFace && ( gl.cullMode == GL_BACK || gl.cullMode == GL_FRONT_AND_BACK );
	rs.offsetFill = gl.polygonOffsetFill;
	if ( rs.offsetFill ) {
		rs.offsetUnits = gl.polyOffsetUnits;
		rs.offsetFactor = gl.polyOffsetFactor;
	}
	if ( StateChanged( applied.raster, rs ) ) {
		GX2SetPolygonControl( rs.frontFace == GL_CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW, rs.cullFront, rs.cullBack,
			FALSE, GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
			rs.offsetFill, rs.offsetFill, FALSE );
		if ( rs.offsetFill ) {
			GX2SetPolygonOffset( rs.offsetUnits, rs.offsetFactor, rs.offsetUnits, rs.offsetFactor, 0.0f );
		}
	}

	viewState_t vs;
	memset( &vs, 0, sizeof( vs ) );
	vs.viewport[0] = gl.viewport[0];
	vs.viewport[1] = FlipY( gl.viewport[1], gl.viewport[3] );
	vs.viewport[2] = gl.viewport[2];
	vs.viewport[3] = gl.viewport[3];
	vs.depthNear = gl.depthNear;
	vs.depthFar = gl.depthFar;
	if ( gl.scissorTest ) {
		int x = Max( 0, gl.scissor[0] );
		int y = Max( 0, FlipY( gl.scissor[1], gl.scissor[3] ) );
		vs.scissor[0] = x;
		vs.scissor[1] = y;
		vs.scissor[2] = Max( 0, Min( (int)gx2ColorBuffer.surface.width - x, gl.scissor[2] ) );
		vs.scissor[3] = Max( 0, Min( (int)gx2ColorBuffer.surface.height - y, gl.scissor[3] ) );
	} else {
		vs.scissor[2] = gx2ColorBuffer.surface.width;
		vs.scissor[3] = gx2ColorBuffer.surface.height;
	}
	if ( StateChanged( applied.view, vs ) ) {
		GX2SetViewport( vs.viewport[0], vs.viewport[1], vs.viewport[2], vs.viewport[3], vs.depthNear, vs.depthFar );
		GX2SetScissor( vs.scissor[0], vs.scissor[1], vs.scissor[2], vs.scissor[3] );
	}
}

static void BindTextureSampler( glTexture_t *t, int unit ) {
	if ( t->samplerDirty ) {
		bool mip = t->minFilter != GL_NEAREST && t->minFilter != GL_LINEAR;
		GX2TexXYFilterMode minF = ( t->minFilter == GL_NEAREST || t->minFilter == GL_NEAREST_MIPMAP_NEAREST || t->minFilter == GL_NEAREST_MIPMAP_LINEAR )
			? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;
		GX2TexXYFilterMode magF = t->magFilter == GL_NEAREST ? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;
		GX2TexMipFilterMode mipF = !mip ? GX2_TEX_MIP_FILTER_MODE_NONE
			: ( t->minFilter == GL_NEAREST_MIPMAP_NEAREST || t->minFilter == GL_LINEAR_MIPMAP_NEAREST ) ? GX2_TEX_MIP_FILTER_MODE_POINT : GX2_TEX_MIP_FILTER_MODE_LINEAR;
		GX2TexAnisoRatio aniso = t->maxAniso >= 16 ? GX2_TEX_ANISO_RATIO_16_TO_1 : t->maxAniso >= 8 ? GX2_TEX_ANISO_RATIO_8_TO_1
			: t->maxAniso >= 4 ? GX2_TEX_ANISO_RATIO_4_TO_1 : t->maxAniso >= 2 ? GX2_TEX_ANISO_RATIO_2_TO_1 : GX2_TEX_ANISO_RATIO_NONE;

		GX2InitSampler( &t->sampler, ClampMode( t->wrapS ), magF );
		GX2InitSamplerXYFilter( &t->sampler, magF, minF, aniso );
		GX2InitSamplerZMFilter( &t->sampler, GX2_TEX_Z_FILTER_MODE_POINT, mipF );
		GLenum wrapR = t->target == GL_TEXTURE_CUBE_MAP_EXT ? GL_CLAMP_TO_EDGE : t->wrapT;
		GX2InitSamplerClamping( &t->sampler, ClampMode( t->wrapS ), ClampMode( t->wrapT ), ClampMode( wrapR ) );
		GX2InitSamplerLOD( &t->sampler, 0.0f, mip ? (float)t->maxLevel : 0.0f, 0.0f );
		t->samplerDirty = false;
		t->bindVersion = ++bindVersionCounter;
	}
	if ( unit < MAX_PIXEL_SAMPLERS ) {
		if ( applied.valid && applied.texture[unit] == t && applied.textureVersion[unit] == t->bindVersion ) {
			return;
		}
		applied.texture[unit] = t;
		applied.textureVersion[unit] = t->bindVersion;
	}
	GX2SetPixelTexture( &t->tex, unit );
	GX2SetPixelSampler( &t->sampler, unit );
}

struct stream_t {
	const void *	ptr;
	uint32_t		stride;
	uint32_t		size;		// bytes the GPU may read
};

struct fetchShader_t {
	const gx2Shader_t *	shader;
	GX2AttribFormat		formats[MAX_SHADER_ATTRIBS];
	GX2FetchShader		fs;
};
static idList<fetchShader_t *> fetchShaders;

static GX2FetchShader *FetchShader( const gx2Shader_t *sh, const GX2AttribFormat *formats ) {
	for ( int i = 0; i < fetchShaders.Num(); i++ ) {
		fetchShader_t *f = fetchShaders[i];
		if ( f->shader == sh && !memcmp( f->formats, formats, sh->numAttribs * sizeof( formats[0] ) ) ) {
			return &f->fs;
		}
	}

	// one buffer per input, matching how GL client arrays are specified
	GX2AttribStream attribs[MAX_SHADER_ATTRIBS];
	for ( int i = 0; i < sh->numAttribs; i++ ) {
		GX2AttribStream &a = attribs[i];
		memset( &a, 0, sizeof( a ) );
		a.location = sh->attribLocation[i];
		a.buffer = i;
		a.offset = 0;
		a.format = formats[i];
		a.type = GX2_ATTRIB_INDEX_PER_VERTEX;
		a.aluDivisor = 0;
		a.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;	// big endian floats, 8bit components as they are
		switch ( formats[i] ) {
			case GX2_ATTRIB_FORMAT_FLOAT_32:		a.mask = GX2_SEL_MASK( GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1 ); break;
			case GX2_ATTRIB_FORMAT_FLOAT_32_32:		a.mask = GX2_SEL_MASK( GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1 ); break;
			case GX2_ATTRIB_FORMAT_FLOAT_32_32_32:	a.mask = GX2_SEL_MASK( GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1 ); break;
			default:								a.mask = GX2_SEL_MASK( GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W ); break;
		}
	}

	fetchShader_t *f = new fetchShader_t;
	memset( f, 0, sizeof( *f ) );
	f->shader = sh;
	memcpy( f->formats, formats, sh->numAttribs * sizeof( formats[0] ) );
	uint32_t size = GX2CalcFetchShaderSizeEx( sh->numAttribs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE );
	void *program = memalign( GX2_SHADER_PROGRAM_ALIGNMENT, size );
	GX2InitFetchShaderEx( &f->fs, (uint8_t *)program, sh->numAttribs, attribs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_SHADER, program, size );
	fetchShaders.Append( f );
	return &f->fs;
}

static GX2AttribFormat FloatFormat( int components ) {
	switch ( components ) {
		case 1:		return GX2_ATTRIB_FORMAT_FLOAT_32;
		case 2:		return GX2_ATTRIB_FORMAT_FLOAT_32_32;
		case 3:		return GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
		default:	return GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
	}
}

static bool ArrayFormat( const glArray_t &a, GX2AttribFormat &format ) {
	if ( a.type == GL_FLOAT ) {
		format = FloatFormat( a.size );
		return true;
	}
	if ( a.type == GL_UNSIGNED_BYTE && a.size == 4 ) {
		format = GX2_ATTRIB_FORMAT_UNORM_8_8_8_8;
		return true;
	}
	return false;
}

// where each vertex shader input comes from for a draw, NULL means the current (constant) value
struct vertexSources_t {
	const glArray_t *	arrays[SEM_COUNT];
};

static const float *ConstantValue( int sem ) {
	static const float defaultAttrib[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	static const float defaultNormal[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	if ( sem == SEM_COLOR ) {
		return gl.currentColor;
	}
	if ( sem >= SEM_TEXCOORD0 && sem < SEM_TEXCOORD0 + MAX_UNITS ) {
		return gl.units[sem - SEM_TEXCOORD0].currentTexCoord;
	}
	if ( sem == SEM_NORMAL ) {
		return defaultNormal;
	}
	return defaultAttrib;
}

static void ClientArraySources( vertexSources_t &src ) {
	memset( &src, 0, sizeof( src ) );
	if ( gl.vertexArray.enabled ) {
		src.arrays[SEM_POSITION] = &gl.vertexArray;
	}
	if ( gl.colorArray.enabled ) {
		src.arrays[SEM_COLOR] = &gl.colorArray;
	}
	if ( gl.normalArray.enabled ) {
		src.arrays[SEM_NORMAL] = &gl.normalArray;
	}
	for ( int i = 0; i < MAX_UNITS; i++ ) {
		if ( gl.units[i].texCoordArray.enabled ) {
			src.arrays[SEM_TEXCOORD0 + i] = &gl.units[i].texCoordArray;
		}
	}
	for ( int i = 0; i < MAX_VERTEX_ATTRIBS; i++ ) {
		if ( gl.vertexAttribs[i].enabled ) {
			src.arrays[SEM_ATTRIB0 + i] = &gl.vertexAttribs[i];
		} else if ( i >= 8 && i - 8 < MAX_UNITS ) {
			// NVIDIA style aliasing of conventional and generic attributes
			src.arrays[SEM_ATTRIB0 + i] = src.arrays[SEM_TEXCOORD0 + i - 8];
		} else if ( i == 2 ) {
			src.arrays[SEM_ATTRIB0 + i] = src.arrays[SEM_NORMAL];
		} else if ( i == 3 ) {
			src.arrays[SEM_ATTRIB0 + i] = src.arrays[SEM_COLOR];
		}
	}
}

static glProgram_t *Program( int type, GLuint ident ) {
	return ident < MAX_PROGRAM_IDENTS ? &programs[type][ident] : NULL;
}

// matches ff.frag
enum { ENV_DISABLED, ENV_MODULATE, ENV_REPLACE, ENV_ADD, ENV_INV_PRIMARY, ENV_CONSTANT, ENV_DECAL };

static int EnvMode( const glTexUnit_t &u ) {
	switch ( u.envMode ) {
		case GL_REPLACE:	return ENV_REPLACE;
		case GL_ADD:		return ENV_ADD;
		case GL_DECAL:		return ENV_DECAL;
		case GL_COMBINE_ARB:
			// only what the renderer uses: texture * ( 1 - primary color ) and previous * constant
			if ( u.source1RGB == GL_CONSTANT_ARB ) {
				return ENV_CONSTANT;
			}
			if ( u.operand1RGB == GL_ONE_MINUS_SRC_COLOR ) {
				return ENV_INV_PRIMARY;
			}
			return ENV_MODULATE;
		default:			return ENV_MODULATE;
	}
}

static void SetVertexUniform( const gx2Uniform_t &u, const float *data, int floats ) {
	if ( u.offset >= 0 ) {
		GX2SetVertexUniformReg( u.offset, floats, data );
	}
}

static void SetPixelUniform( const gx2Uniform_t &u, const float *data, int floats ) {
	if ( u.offset >= 0 ) {
		GX2SetPixelUniformReg( u.offset, floats, data );
	}
}

static void SetFixedFunctionUniforms( const gx2Shader_t *sh ) {
	float modes[4] = { 0, 0, 0, 0 };
	for ( int u = 0; u < 2; u++ ) {
		glTexUnit_t &tu = gl.units[u];
		glTexture_t *t = NULL;
		if ( tu.enableCube ) {
			t = BoundTexture( u, GL_TEXTURE_CUBE_MAP_EXT );
		} else if ( tu.enable2D ) {
			t = BoundTexture( u, GL_TEXTURE_2D );
		}
		if ( t && t->allocated ) {
			modes[u] = EnvMode( tu );
			modes[2 + u] = tu.enableCube ? 1.0f : 0.0f;
		}

		// tc = texMatrix * ( gen * position + sel * texcoord ), with gen holding the
		// planes of the coordinates that have object linear texgen enabled and sel
		// selecting the others from the texcoord input
		float gen[16], sel[16];
		memset( gen, 0, sizeof( gen ) );
		memset( sel, 0, sizeof( sel ) );
		for ( int c = 0; c < 4; c++ ) {
			if ( !tu.texGenEnabled[c] ) {
				sel[c*4+c] = 1.0f;
			} else if ( tu.texGenMode[c] == GL_OBJECT_LINEAR || tu.texGenMode[c] == GL_EYE_LINEAR ) {
				for ( int col = 0; col < 4; col++ ) {
					gen[col*4+c] = tu.texGenPlane[c][col];
				}
			}
		}
		const float *texMat = gl.texture[u].m[gl.texture[u].depth];
		MatrixMultiply( texMat, gen, gen );
		MatrixMultiply( texMat, sel, sel );
		SetVertexUniform( sh->texGen[u], gen, 16 );
		SetVertexUniform( sh->texMat[u], sel, 16 );
		SetPixelUniform( sh->envColor[u], tu.envColor, 4 );
	}
	SetPixelUniform( sh->mode, modes, 4 );
}

static void Draw( GLenum mode, const vertexSources_t &src, GLenum indexType, const void *indices, bool indicesInBuffer, int count, int first ) {
	if ( !gx2Foreground || count <= 0 || !src.arrays[SEM_POSITION] ) {
		return;
	}

	// pick the shader
	gx2Shader_t *sh = ffShader;
	glProgram_t *vp = NULL, *fp = NULL;
	if ( gl.vertexProgram || gl.fragmentProgram ) {
		if ( gl.vertexProgram ) {
			vp = Program( 0, gl.boundVertexProgram );
		}
		if ( gl.fragmentProgram ) {
			fp = Program( 1, gl.boundFragmentProgram );
		}
		// shaders are compiled in pairs, the vertex and fragment program of a
		// file are always used together
		sh = vp ? vp->shader : ( fp ? fp->shader : NULL );
		if ( !sh || ( vp && fp && vp->shader != fp->shader ) ) {
			stats.skippedPrograms++;
			return;
		}
		stats.programDraws++;
	}

	// how many vertices the GPU may read from client memory
	int numVerts = 0;
	bool clientMemory = false;
	for ( int i = 0; i < sh->numAttribs; i++ ) {
		const glArray_t *a = src.arrays[sh->attribSemantic[i]];
		if ( a && !a->buffer ) {
			clientMemory = true;
		}
	}
	if ( clientMemory ) {
		if ( indices ) {
			uint32_t maxIndex = 0;
			if ( indexType == GL_UNSIGNED_SHORT ) {
				const uint16_t *idx = (const uint16_t *)indices;
				for ( int i = 0; i < count; i++ ) {
					maxIndex = Max( maxIndex, (uint32_t)idx[i] );
				}
			} else {
				const uint32_t *idx = (const uint32_t *)indices;
				for ( int i = 0; i < count; i++ ) {
					maxIndex = Max( maxIndex, idx[i] );
				}
			}
			numVerts = maxIndex + 1;
		} else {
			numVerts = first + count;
		}
	}

	stream_t streams[MAX_SHADER_ATTRIBS];
	GX2AttribFormat formats[MAX_SHADER_ATTRIBS];
	for ( int i = 0; i < sh->numAttribs; i++ ) {
		int sem = sh->attribSemantic[i];
		const glArray_t *a = src.arrays[sem];
		stream_t &s = streams[i];
		if ( a && ArrayFormat( *a, formats[i] ) ) {
			s.ptr = ArrayPointer( *a );
			s.stride = ArrayStride( *a );
			if ( !s.ptr ) {
				stats.skippedOther++;
				return;
			}
			if ( a->buffer ) {
				s.size = Buffer( a->buffer )->size - (uintptr_t)a->ptr;	// flushed when the buffer was filled
			} else {
				s.size = s.stride * ( numVerts - 1 ) + a->size * TypeSize( a->type );
				s.ptr = CopyToFrame( s.ptr, s.size );
			}
		} else {
			s.ptr = CopyToFrame( ConstantValue( sem ), 4 * sizeof( float ) );
			s.stride = 0;
			s.size = 4 * sizeof( float );
			formats[i] = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
		}
	}

	ApplyRenderState();

	GX2FetchShader *fetchShader = FetchShader( sh, formats );
	if ( !applied.valid || applied.fetchShader != fetchShader ) {
		GX2SetFetchShader( fetchShader );
		applied.fetchShader = fetchShader;
	}
	if ( !applied.valid || applied.vertexShader != sh->group.vertexShader ) {
		GX2SetVertexShader( sh->group.vertexShader );
		applied.vertexShader = sh->group.vertexShader;
	}
	if ( !applied.valid || applied.pixelShader != sh->group.pixelShader ) {
		GX2SetPixelShader( sh->group.pixelShader );
		applied.pixelShader = sh->group.pixelShader;
	}

	const float *modelView = gl.modelview.m[gl.modelview.depth];
	const float *projection = gl.projection.m[gl.projection.depth];
	float mvp[16];
	MatrixMultiply( projection, modelView, mvp );
	SetVertexUniform( sh->mvp, mvp, 16 );
	SetVertexUniform( sh->modelView, modelView, 16 );
	SetVertexUniform( sh->projection, projection, 16 );

	if ( sh == ffShader ) {
		SetFixedFunctionUniforms( sh );
	} else {
		static const float zeroLocal[MAX_LOCAL_PARMS][4] = {};
		SetVertexUniform( sh->vsEnv, gl.programEnv[0][0], Min( sh->vsEnv.count, 256 ) * 4 );
		SetVertexUniform( sh->vsLocal, vp ? vp->local[0] : zeroLocal[0], Min( sh->vsLocal.count, MAX_LOCAL_PARMS ) * 4 );
		SetPixelUniform( sh->psEnv, gl.programEnv[1][0], Min( sh->psEnv.count, 256 ) * 4 );
		SetPixelUniform( sh->psLocal, fp ? fp->local[0] : zeroLocal[0], Min( sh->psLocal.count, MAX_LOCAL_PARMS ) * 4 );
		float debug[4] = { (float)r_gx2ShaderDebug.GetInteger(), 0.0f, 0.0f, 0.0f };
		SetPixelUniform( sh->debug, debug, 4 );
	}

	for ( int i = 0; i < sh->numSamplers; i++ ) {
		bool cube = sh->samplerCube[i];
		glTexture_t *t = BoundTexture( sh->samplerUnit[i], cube ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D );
		if ( !t || !t->allocated || ( t->tex.surface.dim == GX2_SURFACE_DIM_TEXTURE_CUBE ) != cube ) {
			t = textures[cube ? whiteTextureCube : whiteTexture2D];
		}
		BindTextureSampler( t, sh->samplerLocation[i] );
	}
	applied.valid = true;

	for ( int i = 0; i < sh->numAttribs; i++ ) {
		GX2SetAttribBuffer( i, streams[i].size, streams[i].stride, streams[i].ptr );
	}

	GX2PrimitiveMode prim;
	switch ( mode ) {
		case GL_POINTS:			prim = GX2_PRIMITIVE_MODE_POINTS; break;
		case GL_LINES:			prim = GX2_PRIMITIVE_MODE_LINES; break;
		case GL_LINE_LOOP:		prim = GX2_PRIMITIVE_MODE_LINE_LOOP; break;
		case GL_LINE_STRIP:		prim = GX2_PRIMITIVE_MODE_LINE_STRIP; break;
		case GL_TRIANGLE_STRIP:	prim = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP; break;
		case GL_TRIANGLE_FAN:	case GL_POLYGON: prim = GX2_PRIMITIVE_MODE_TRIANGLE_FAN; break;
		case GL_QUADS:			prim = GX2_PRIMITIVE_MODE_QUADS; break;
		default:				prim = GX2_PRIMITIVE_MODE_TRIANGLES; break;
	}

	if ( indices ) {
		int indexSize = indexType == GL_UNSIGNED_SHORT ? 2 : 4;
		if ( !indicesInBuffer ) {
			indices = CopyToFrame( indices, count * indexSize );
		}
		GX2DrawIndexedEx( prim, count, indexType == GL_UNSIGNED_SHORT ? GX2_INDEX_TYPE_U16 : GX2_INDEX_TYPE_U32, indices, 0, 1 );
	} else {
		GX2DrawEx( prim, count, first, 1 );
	}
	stats.draws++;
}

static void DrawArraysInternal( GLenum mode, GLenum indexType, const void *indices, bool indicesInBuffer, int count, int first ) {
	vertexSources_t src;
	ClientArraySources( src );
	Draw( mode, src, indexType, indices, indicesInBuffer, count, first );
}

/*
===============================================================================

	GL entry points

===============================================================================
*/

extern "C" {

const GLubyte * APIENTRY glGetString( GLenum name ) {
	CHECK_THREAD();
	switch ( name ) {
		case GL_VENDOR:		return (const GLubyte *)"dhewm3";
		case GL_RENDERER:	return (const GLubyte *)"Wii U GX2";
		case GL_VERSION:	return (const GLubyte *)"2.0 GX2";
		case GL_EXTENSIONS:
			return (const GLubyte *)"GL_ARB_multitexture GL_ARB_texture_env_combine GL_ARB_texture_cube_map GL_ARB_texture_env_dot3 "
				"GL_ARB_texture_env_add GL_ARB_texture_non_power_of_two GL_ARB_vertex_buffer_object "
				"GL_ARB_vertex_program GL_ARB_fragment_program GL_EXT_stencil_wrap "
				// GX2 supports these natively (BC1-3), the renderer then uses the
				// precompressed .dds textures instead of decoding .tga files and
				// generating mipmaps on the (slow) CPU
				"GL_ARB_texture_compression GL_EXT_texture_compression_s3tc";
	}
	return (const GLubyte *)"";
}

GLenum APIENTRY glGetError( void ) {
	return GL_NO_ERROR;
}

void APIENTRY glGetIntegerv( GLenum pname, GLint *params ) {
	CHECK_THREAD();
	switch ( pname ) {
		case GL_MAX_TEXTURE_SIZE:				*params = 4096; break;
		case GL_MAX_TEXTURE_UNITS_ARB:			*params = MAX_UNITS; break;
		case GL_MAX_TEXTURE_COORDS_ARB:			*params = MAX_UNITS; break;
		case GL_MAX_TEXTURE_IMAGE_UNITS_ARB:	*params = 16; break;
		case GL_PROGRAM_ERROR_POSITION_ARB:		*params = -1; break;
		case GL_STENCIL_BITS:					*params = 8; break;
		case GL_DEPTH_BITS:						*params = 24; break;
		case GL_VIEWPORT:						memcpy( params, gl.viewport, sizeof( gl.viewport ) ); break;
		default:								*params = 0; break;
	}
}

void APIENTRY glGetFloatv( GLenum pname, GLfloat *params ) {
	CHECK_THREAD();
	switch ( pname ) {
		case GL_MODELVIEW_MATRIX:	memcpy( params, gl.modelview.m[gl.modelview.depth], 16 * sizeof( float ) ); break;
		case GL_PROJECTION_MATRIX:	memcpy( params, gl.projection.m[gl.projection.depth], 16 * sizeof( float ) ); break;
		case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: *params = 16.0f; break;
		default:					*params = 0.0f; break;
	}
}

static bool *Capability( GLenum cap ) {
	glTexUnit_t &u = gl.units[gl.activeUnit];
	switch ( cap ) {
		case GL_BLEND:					return &gl.blend;
		case GL_DEPTH_TEST:				return &gl.depthTest;
		case GL_STENCIL_TEST:			return &gl.stencilTest;
		case GL_CULL_FACE:				return &gl.cullFace;
		case GL_ALPHA_TEST:				return &gl.alphaTest;
		case GL_SCISSOR_TEST:			return &gl.scissorTest;
		case GL_POLYGON_OFFSET_FILL:	return &gl.polygonOffsetFill;
		case GL_VERTEX_PROGRAM_ARB:		return &gl.vertexProgram;
		case GL_FRAGMENT_PROGRAM_ARB:	return &gl.fragmentProgram;
		case GL_TEXTURE_2D:				return &u.enable2D;
		case GL_TEXTURE_CUBE_MAP_EXT:	return &u.enableCube;
		case GL_TEXTURE_GEN_S:			return &u.texGenEnabled[0];
		case GL_TEXTURE_GEN_T:			return &u.texGenEnabled[1];
		case GL_TEXTURE_GEN_R:			return &u.texGenEnabled[2];
		case GL_TEXTURE_GEN_Q:			return &u.texGenEnabled[3];
	}
	return NULL;
}

void APIENTRY glEnable( GLenum cap ) {
	CHECK_THREAD();
	bool *b = Capability( cap );
	if ( b ) {
		*b = true;
	}
}

void APIENTRY glDisable( GLenum cap ) {
	CHECK_THREAD();
	bool *b = Capability( cap );
	if ( b ) {
		*b = false;
	}
}

GLboolean APIENTRY glIsEnabled( GLenum cap ) {
	CHECK_THREAD();
	bool *b = Capability( cap );
	return b && *b;
}

static glArray_t *ClientArray( GLenum array ) {
	switch ( array ) {
		case GL_VERTEX_ARRAY:			return &gl.vertexArray;
		case GL_COLOR_ARRAY:			return &gl.colorArray;
		case GL_NORMAL_ARRAY:			return &gl.normalArray;
		case GL_TEXTURE_COORD_ARRAY:	return &gl.units[gl.clientActiveUnit].texCoordArray;
	}
	return NULL;
}

void APIENTRY glEnableClientState( GLenum array ) {
	CHECK_THREAD();
	glArray_t *a = ClientArray( array );
	if ( a ) {
		a->enabled = true;
	}
}

void APIENTRY glDisableClientState( GLenum array ) {
	CHECK_THREAD();
	glArray_t *a = ClientArray( array );
	if ( a ) {
		a->enabled = false;
	}
}

static void SetArray( glArray_t &a, GLint size, GLenum type, GLsizei stride, const void *ptr ) {
	a.size = size;
	a.type = type;
	a.stride = stride;
	a.ptr = ptr;
	a.buffer = Bindings().arrayBuffer;
}

void APIENTRY glVertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) {
	CHECK_THREAD();
	SetArray( gl.vertexArray, size, type, stride, ptr );
}

void APIENTRY glColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) {
	CHECK_THREAD();
	SetArray( gl.colorArray, size, type, stride, ptr );
}

void APIENTRY glNormalPointer( GLenum type, GLsizei stride, const GLvoid *ptr ) {
	CHECK_THREAD();
	SetArray( gl.normalArray, 3, type, stride, ptr );
}

void APIENTRY glTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) {
	CHECK_THREAD();
	SetArray( gl.units[gl.clientActiveUnit].texCoordArray, size, type, stride, ptr );
}

void APIENTRY glDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices ) {
	CHECK_THREAD();
	const void *idx = indices;
	GLuint elementArrayBuffer = Bindings().elementArrayBuffer;
	if ( elementArrayBuffer ) {
		glBuffer_t *b = Buffer( elementArrayBuffer );
		if ( !b ) {
			return;
		}
		idx = b->data + (uintptr_t)indices;
	}
	DrawArraysInternal( mode, type, idx, elementArrayBuffer != 0, count, 0 );
}

void APIENTRY glDrawArrays( GLenum mode, GLint first, GLsizei count ) {
	CHECK_THREAD();
	DrawArraysInternal( mode, GL_UNSIGNED_INT, NULL, false, count, first );
}

// matrices

void APIENTRY glMatrixMode( GLenum mode ) {
	CHECK_THREAD();
	gl.matrixMode = mode;
}

void APIENTRY glLoadIdentity( void ) {
	CHECK_THREAD();
	MatrixIdentity( CurrentMatrix() );
}

void APIENTRY glLoadMatrixf( const GLfloat *m ) {
	CHECK_THREAD();
	memcpy( CurrentMatrix(), m, 16 * sizeof( float ) );
}

void APIENTRY glMultMatrixf( const GLfloat *m ) {
	CHECK_THREAD();
	float *cur = CurrentMatrix();
	MatrixMultiply( cur, m, cur );
}

void APIENTRY glPushMatrix( void ) {
	CHECK_THREAD();
	glMatrixStack_t *s = CurrentMatrixStack();
	if ( s->depth < 31 ) {
		memcpy( s->m[s->depth + 1], s->m[s->depth], 16 * sizeof( float ) );
		s->depth++;
	}
}

void APIENTRY glPopMatrix( void ) {
	CHECK_THREAD();
	glMatrixStack_t *s = CurrentMatrixStack();
	if ( s->depth > 0 ) {
		s->depth--;
	}
}

void APIENTRY glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar ) {
	CHECK_THREAD();
	float m[16];
	MatrixIdentity( m );
	m[0] = 2.0f / ( right - left );
	m[5] = 2.0f / ( top - bottom );
	m[10] = -2.0f / ( zFar - zNear );
	m[12] = -( right + left ) / ( right - left );
	m[13] = -( top + bottom ) / ( top - bottom );
	m[14] = -( zFar + zNear ) / ( zFar - zNear );
	glMultMatrixf( m );
}

// state

void APIENTRY glBlendFunc( GLenum sfactor, GLenum dfactor ) {
	CHECK_THREAD();
	gl.blendSrc = sfactor;
	gl.blendDst = dfactor;
}

void APIENTRY glBlendEquation( GLenum mode ) {
	CHECK_THREAD();
	gl.blendEquation = mode;
}

void APIENTRY glDepthFunc( GLenum func ) {
	CHECK_THREAD();
	gl.depthFunc = func;
}

void APIENTRY glDepthMask( GLboolean flag ) {
	CHECK_THREAD();
	gl.depthMask = flag;
}

void APIENTRY glDepthRange( GLclampd zNear, GLclampd zFar ) {
	CHECK_THREAD();
	gl.depthNear = zNear;
	gl.depthFar = zFar;
}

void APIENTRY glColorMask( GLboolean r, GLboolean g, GLboolean b, GLboolean a ) {
	CHECK_THREAD();
	gl.colorMask[0] = r;
	gl.colorMask[1] = g;
	gl.colorMask[2] = b;
	gl.colorMask[3] = a;
}

void APIENTRY glStencilFunc( GLenum func, GLint ref, GLuint mask ) {
	CHECK_THREAD();
	gl.stencilFunc = func;
	gl.stencilRef = ref;
	gl.stencilFuncMask = mask;
}

void APIENTRY glStencilMask( GLuint mask ) {
	CHECK_THREAD();
	gl.stencilWriteMask = mask;
}

void APIENTRY glStencilOp( GLenum fail, GLenum zfail, GLenum zpass ) {
	CHECK_THREAD();
	for ( int i = 0; i < 2; i++ ) {
		gl.stencilFail[i] = fail;
		gl.stencilZFail[i] = zfail;
		gl.stencilZPass[i] = zpass;
	}
}

void APIENTRY glStencilOpSeparate( GLenum face, GLenum fail, GLenum zfail, GLenum zpass ) {
	CHECK_THREAD();
	for ( int i = 0; i < 2; i++ ) {
		if ( ( i == 0 && face == GL_BACK ) || ( i == 1 && face == GL_FRONT ) ) {
			continue;
		}
		gl.stencilFail[i] = fail;
		gl.stencilZFail[i] = zfail;
		gl.stencilZPass[i] = zpass;
	}
}

void APIENTRY glAlphaFunc( GLenum func, GLclampf ref ) {
	CHECK_THREAD();
	gl.alphaFunc = func;
	gl.alphaRef = ref;
}

void APIENTRY glCullFace( GLenum mode ) {
	CHECK_THREAD();
	gl.cullMode = mode;
}

void APIENTRY glFrontFace( GLenum mode ) {
	CHECK_THREAD();
	gl.frontFace = mode;
}

void APIENTRY glPolygonOffset( GLfloat factor, GLfloat units ) {
	CHECK_THREAD();
	gl.polyOffsetFactor = factor;
	gl.polyOffsetUnits = units;
}

void APIENTRY glViewport( GLint x, GLint y, GLsizei width, GLsizei height ) {
	CHECK_THREAD();
	gl.viewport[0] = x;
	gl.viewport[1] = y;
	gl.viewport[2] = width;
	gl.viewport[3] = height;
}

void APIENTRY glScissor( GLint x, GLint y, GLsizei width, GLsizei height ) {
	CHECK_THREAD();
	gl.scissor[0] = x;
	gl.scissor[1] = y;
	gl.scissor[2] = width;
	gl.scissor[3] = height;
}

void APIENTRY glClearColor( GLclampf r, GLclampf g, GLclampf b, GLclampf a ) {
	CHECK_THREAD();
	gl.clearColor[0] = r;
	gl.clearColor[1] = g;
	gl.clearColor[2] = b;
	gl.clearColor[3] = a;
}

void APIENTRY glClearDepth( GLclampd depth ) {
	CHECK_THREAD();
	gl.clearDepth = depth;
}

void APIENTRY glClearStencil( GLint s ) {
	CHECK_THREAD();
	gl.clearStencil = s;
}

void APIENTRY glClear( GLbitfield mask ) {
	CHECK_THREAD();
	if ( !gx2Foreground ) {
		return;
	}
	// NOTE: unlike glClear() this ignores the scissor rect
	if ( mask & GL_COLOR_BUFFER_BIT ) {
		GX2ClearColor( &gx2ColorBuffer, gl.clearColor[0], gl.clearColor[1], gl.clearColor[2], gl.clearColor[3] );
	}
	int flags = ( ( mask & GL_DEPTH_BUFFER_BIT ) ? GX2_CLEAR_FLAGS_DEPTH : 0 ) | ( ( mask & GL_STENCIL_BUFFER_BIT ) ? GX2_CLEAR_FLAGS_STENCIL : 0 );
	if ( flags ) {
		GX2ClearDepthStencilEx( &gx2DepthBuffer, gl.clearDepth, gl.clearStencil, (GX2ClearFlags)flags );
	}
	if ( mask & GL_DEPTH_BUFFER_BIT ) {
		// packed depth (see PackDepth() in the shaders), all ones decodes to >= 1
		GX2ClearColor( &gx2DepthColorBuffer, 1.0f, 1.0f, 1.0f, 1.0f );
	}
	GX2GL_RestoreState();
}

void APIENTRY glFinish( void ) {
	CHECK_THREAD();
	if ( gx2Foreground ) {
		GX2DrawDone();
	}
}

void APIENTRY glFlush( void ) {
	CHECK_THREAD();
	GX2Flush();
}

// immediate mode

void APIENTRY glBegin( GLenum mode ) {
	CHECK_THREAD();
	immMode = mode;
	immVerts.SetNum( 0, false );
}

static void ImmVertex( float x, float y, float z ) {
	immVertex_t &v = immVerts.Alloc();
	v.xyz[0] = x;
	v.xyz[1] = y;
	v.xyz[2] = z;
	v.xyz[3] = 1.0f;
	memcpy( v.color, gl.currentColor, sizeof( v.color ) );
	memcpy( v.st, gl.units[0].currentTexCoord, sizeof( v.st ) );
}

void APIENTRY glVertex2f( GLfloat x, GLfloat y ) {
	CHECK_THREAD(); ImmVertex( x, y, 0.0f ); }
void APIENTRY glVertex3f( GLfloat x, GLfloat y, GLfloat z ) {
	CHECK_THREAD(); ImmVertex( x, y, z ); }
void APIENTRY glVertex3fv( const GLfloat *v ) {
	CHECK_THREAD(); ImmVertex( v[0], v[1], v[2] ); }

void APIENTRY glEnd( void ) {
	CHECK_THREAD();
	int n = immVerts.Num();
	if ( n == 0 ) {
		return;
	}
	const immVertex_t *verts = immVerts.Ptr();

	glArray_t pos, color, st;
	memset( &pos, 0, sizeof( pos ) );
	pos.enabled = true;
	pos.size = 4;
	pos.type = GL_FLOAT;
	pos.stride = sizeof( immVertex_t );
	color = st = pos;
	pos.ptr = verts[0].xyz;
	color.ptr = verts[0].color;
	st.ptr = verts[0].st;

	vertexSources_t src;
	memset( &src, 0, sizeof( src ) );
	src.arrays[SEM_POSITION] = &pos;
	src.arrays[SEM_COLOR] = &color;
	src.arrays[SEM_TEXCOORD0] = &st;
	src.arrays[SEM_ATTRIB0 + 8] = &st;
	Draw( immMode, src, GL_UNSIGNED_INT, NULL, false, n, 0 );
}

void APIENTRY glColor3f( GLfloat r, GLfloat g, GLfloat b ) {
	CHECK_THREAD();
	gl.currentColor[0] = r; gl.currentColor[1] = g; gl.currentColor[2] = b; gl.currentColor[3] = 1.0f;
}
void APIENTRY glColor3fv( const GLfloat *v ) {
	CHECK_THREAD(); glColor3f( v[0], v[1], v[2] ); }
void APIENTRY glColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a ) {
	CHECK_THREAD();
	gl.currentColor[0] = r; gl.currentColor[1] = g; gl.currentColor[2] = b; gl.currentColor[3] = a;
}
void APIENTRY glColor4fv( const GLfloat *v ) {
	CHECK_THREAD(); glColor4f( v[0], v[1], v[2], v[3] ); }
void APIENTRY glColor4ubv( const GLubyte *v ) {
	CHECK_THREAD(); glColor4f( v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f ); }

void APIENTRY glTexCoord2f( GLfloat s, GLfloat t ) {
	CHECK_THREAD();
	float *tc = gl.units[0].currentTexCoord;
	tc[0] = s; tc[1] = t; tc[2] = 0.0f; tc[3] = 1.0f;
}
void APIENTRY glTexCoord2fv( const GLfloat *v ) {
	CHECK_THREAD(); glTexCoord2f( v[0], v[1] ); }

// textures

void APIENTRY glGenTextures( GLsizei n, GLuint *names ) {
	CHECK_THREAD();
	for ( int i = 0; i < n; i++ ) {
		glTexture_t *t = new glTexture_t;
		memset( t, 0, sizeof( *t ) );
		t->minFilter = GL_NEAREST_MIPMAP_LINEAR;
		t->magFilter = GL_LINEAR;
		t->wrapS = t->wrapT = GL_REPEAT;
		t->maxAniso = 1.0f;
		t->samplerDirty = true;
		names[i] = textures.Append( t );
	}
}

void APIENTRY glDeleteTextures( GLsizei n, const GLuint *names ) {
	CHECK_THREAD();
	for ( int i = 0; i < n; i++ ) {
		if ( names[i] > 0 && (int)names[i] < textures.Num() && textures[names[i]] ) {
			DeferredFree( textures[names[i]]->tex.surface.image );
			delete textures[names[i]];
			textures[names[i]] = NULL;
		}
	}
}

void APIENTRY glBindTexture( GLenum target, GLuint texture ) {
	CHECK_THREAD();
	glTexUnit_t &u = gl.units[gl.activeUnit];
	if ( target == GL_TEXTURE_CUBE_MAP_EXT ) {
		u.boundCube = texture;
	} else {
		u.bound2D = texture;
	}
	if ( texture > 0 && (int)texture < textures.Num() && textures[texture] && !textures[texture]->target ) {
		textures[texture]->target = target;
	}
}

static void TexParameter( GLenum target, GLenum pname, float param ) {
	glTexture_t *t = BoundTexture( gl.activeUnit, target );
	if ( !t ) {
		return;
	}
	switch ( pname ) {
		case GL_TEXTURE_MIN_FILTER:				t->minFilter = (GLenum)param; break;
		case GL_TEXTURE_MAG_FILTER:				t->magFilter = (GLenum)param; break;
		case GL_TEXTURE_WRAP_S:					t->wrapS = (GLenum)param; break;
		case GL_TEXTURE_WRAP_T:					t->wrapT = (GLenum)param; break;
		case GL_TEXTURE_MAX_ANISOTROPY_EXT:		t->maxAniso = param; break;
		default: return;
	}
	t->samplerDirty = true;
}

void APIENTRY glTexParameteri( GLenum target, GLenum pname, GLint param ) {
	CHECK_THREAD(); TexParameter( target, pname, (float)param ); }
void APIENTRY glTexParameterf( GLenum target, GLenum pname, GLfloat param ) {
	CHECK_THREAD(); TexParameter( target, pname, param ); }

void APIENTRY glTexEnvi( GLenum target, GLenum pname, GLint param ) {
	CHECK_THREAD();
	glTexUnit_t &u = gl.units[gl.activeUnit];
	switch ( pname ) {
		case GL_TEXTURE_ENV_MODE:	u.envMode = param; break;
		case GL_COMBINE_RGB_ARB:	u.combineRGB = param; break;
		case GL_SOURCE1_RGB_ARB:	u.source1RGB = param; break;
		case GL_OPERAND1_RGB_ARB:	u.operand1RGB = param; break;
	}
}

void APIENTRY glTexEnvfv( GLenum target, GLenum pname, const GLfloat *params ) {
	CHECK_THREAD();
	if ( pname == GL_TEXTURE_ENV_COLOR ) {
		memcpy( gl.units[gl.activeUnit].envColor, params, 4 * sizeof( float ) );
	}
}

void APIENTRY glTexGenf( GLenum coord, GLenum pname, GLfloat param ) {
	CHECK_THREAD();
	if ( pname == GL_TEXTURE_GEN_MODE && coord >= GL_S && coord <= GL_Q ) {
		gl.units[gl.activeUnit].texGenMode[coord - GL_S] = (GLenum)param;
	}
}

void APIENTRY glTexGenfv( GLenum coord, GLenum pname, const GLfloat *params ) {
	CHECK_THREAD();
	if ( ( pname == GL_OBJECT_PLANE || pname == GL_EYE_PLANE ) && coord >= GL_S && coord <= GL_Q ) {
		// TODO: GL_EYE_PLANE should be transformed by the inverse modelview matrix
		memcpy( gl.units[gl.activeUnit].texGenPlane[coord - GL_S], params, 4 * sizeof( float ) );
	}
}

static int CubeFace( GLenum target ) {
	if ( target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_EXT ) {
		return target - GL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT;
	}
	return -1;
}

static bool IsS3TCFormat( GLenum internalFormat ) {
	return internalFormat >= GL_COMPRESSED_RGB_S3TC_DXT1_EXT && internalFormat <= GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
}

static GX2SurfaceFormat SurfaceFormat( GLenum internalFormat ) {
	switch ( internalFormat ) {
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:	return GX2_SURFACE_FORMAT_UNORM_BC1;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:	return GX2_SURFACE_FORMAT_UNORM_BC2;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:	return GX2_SURFACE_FORMAT_UNORM_BC3;
		// everything else is stored as RGBA8, incl. depth textures (packed depth)
		default:								return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	}
}

static bool IsDepthFormat( GLenum internalFormat ) {
	return internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT16_ARB
		|| internalFormat == GL_DEPTH_COMPONENT24_ARB || internalFormat == GL_DEPTH_COMPONENT32_ARB;
}

static uint32_t CompMap( GLenum internalFormat ) {
	switch ( internalFormat ) {
		case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT16_ARB: case GL_DEPTH_COMPONENT24_ARB: case GL_DEPTH_COMPONENT32_ARB:
			// packed into RGBA, shaders reading depth textures unpack it
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A );
		case GL_ALPHA: case GL_ALPHA8:
			return GX2_COMP_MAP( GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_A );
		case GL_LUMINANCE: case GL_LUMINANCE8: case 1:
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1 );
		case GL_INTENSITY: case GL_INTENSITY8:
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R );
		case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8: case 2:
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_A );
		case GL_RGB: case GL_RGB8: case GL_RGB5: case 3: case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_1 );
		default:
			return GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A );
	}
}

// (re)creates the GX2 texture for a new level 0 size
/*
===============================================================================

	Diagnostics for textures that look corrupted on a real Wii U (not in Cemu)

===============================================================================
*/

static int		nonContiguousTextures;
static int		verifiedOk, verifiedBad, verifiedOkAtUpload, verifiedBadAtUpload, verifyPrints;
static int		verifyNext;

// the GPU sees physical memory: true if the CPU's view of it is one block there too
static bool PhysicallyContiguous( const void *ptr, size_t size ) {
	uint32_t base = OSEffectiveToPhysical( (uint32_t)(uintptr_t)ptr );
	if ( !base ) {
		return false;
	}
	for ( size_t ofs = 4096 - ( (uintptr_t)ptr & 4095 ); ofs < size; ofs += 4096 ) {
		if ( OSEffectiveToPhysical( (uint32_t)(uintptr_t)ptr + ofs ) != base + ofs ) {
			return false;
		}
	}
	return OSEffectiveToPhysical( (uint32_t)(uintptr_t)ptr + size - 1 ) == base + size - 1;
}

static uint32_t BlockRowsSum( const byte *data, uint32_t pitchBytes, int rows, int rowBytes ) {
	uint32_t sum = 2166136261u;
	for ( int row = 0; row < rows; row++ ) {
		const byte *p = data + row * pitchBytes;
		for ( int i = 0; i < rowBytes; i++ ) {
			sum = ( sum ^ p[i] ) * 16777619u;
		}
	}
	return sum;
}

static void CompressedLayout( const glTexture_t *t, int &blocksWide, int &blocksHigh, int &blockBytes ) {
	blockBytes = ( t->internalFormat <= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ) ? 8 : 16;
	blocksWide = ( (int)t->tex.surface.width + 3 ) / 4;
	blocksHigh = ( (int)t->tex.surface.height + 3 ) / 4;
}

// copies level 0 of a precompressed texture back into a linear surface and
// compares it with what was uploaded. 1: same, 0: different
static int VerifyTexture( glTexture_t *t, int texnum, const char *when ) {
	GX2Surface lin;
	memset( &lin, 0, sizeof( lin ) );
	lin.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	lin.width = t->tex.surface.width;
	lin.height = t->tex.surface.height;
	lin.depth = 1;
	lin.mipLevels = 1;
	lin.format = t->tex.surface.format;
	lin.aa = GX2_AA_MODE1X;
	lin.use = GX2_SURFACE_USE_TEXTURE;
	lin.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &lin );
	lin.image = GX2GL_FrameAlloc( lin.imageSize, Max( (uint32_t)64, lin.alignment ) );
	memset( lin.image, 0xcd, lin.imageSize );
	DCFlushRange( lin.image, lin.imageSize );

	GX2CopySurface( &t->tex.surface, 0, 0, &lin, 0, 0 );
	GX2GL_RestoreState();
	GX2Invalidate( (GX2InvalidateMode)( GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE ), lin.image, lin.imageSize );
	GX2DrawDone();
	DCInvalidateRange( lin.image, lin.imageSize );

	int blocksWide, blocksHigh, blockBytes;
	CompressedLayout( t, blocksWide, blocksHigh, blockBytes );
	uint32_t sum = BlockRowsSum( (const byte *)lin.image, lin.pitch * blockBytes, blocksHigh, blocksWide * blockBytes );
	if ( sum == t->level0Sum ) {
		return 1;
	}
	if ( verifyPrints++ < 15 ) {
		common->Printf( "GX2GL texture check (%s): texture %d (%ux%u, format 0x%x, %u bytes at %p) differs from what was uploaded%s\n",
			when, texnum, t->tex.surface.width, t->tex.surface.height, t->internalFormat, t->tex.surface.imageSize + t->tex.surface.mipmapSize,
			t->tex.surface.image, PhysicallyContiguous( t->tex.surface.image, t->tex.surface.imageSize + t->tex.surface.mipmapSize ) ? "" : ", not physically contiguous" );
	}
	return 0;
}

} // extern "C"

// a few textures per call, all of them over time
void GX2GL_VerifySomeTextures( void ) {
	if ( !r_gx2VerifyTextures.GetBool() || !gx2Foreground ) {
		return;
	}
	int checked = 0;
	for ( int n = 0; n < textures.Num() && checked < 24; n++ ) {
		verifyNext = ( verifyNext + 1 ) % Max( 1, textures.Num() );
		glTexture_t *t = textures[verifyNext];
		if ( t && t->allocated && t->verify ) {
			if ( VerifyTexture( t, verifyNext, "later" ) ) {
				verifiedOk++;
			} else {
				verifiedBad++;
			}
			checked++;
		}
	}
	common->Printf( "GX2GL texture check: right after upload %d ok, %d differ | later %d ok, %d differ | %d textures not physically contiguous\n",
		verifiedOkAtUpload, verifiedBadAtUpload, verifiedOk, verifiedBad, nonContiguousTextures );
}

extern "C" {

static void AllocTexture( glTexture_t *t, bool cube, int width, int height, GLenum internalFormat ) {
	if ( t->allocated ) {
		if ( (int)t->tex.surface.width == width && (int)t->tex.surface.height == height && t->internalFormat == internalFormat ) {
			return;
		}
		DeferredFree( t->tex.surface.image );
	}

	bool depth = IsDepthFormat( internalFormat );
	int levels = 1;
	while ( !depth && ( ( width >> levels ) > 0 || ( height >> levels ) > 0 ) ) {
		levels++;
	}

	GX2Texture &tex = t->tex;
	memset( &tex, 0, sizeof( tex ) );
	tex.surface.dim = cube ? GX2_SURFACE_DIM_TEXTURE_CUBE : GX2_SURFACE_DIM_TEXTURE_2D;
	tex.surface.width = width;
	tex.surface.height = height;
	tex.surface.depth = cube ? 6 : 1;
	tex.surface.mipLevels = levels;
	tex.surface.format = SurfaceFormat( internalFormat );
	tex.surface.aa = GX2_AA_MODE1X;
	tex.surface.use = GX2_SURFACE_USE_TEXTURE;
	tex.surface.tileMode = GX2_TILE_MODE_DEFAULT;
	tex.viewNumSlices = cube ? 6 : 1;
	tex.viewNumMips = levels;
	tex.compMap = CompMap( internalFormat );
	GX2CalcSurfaceSizeAndAlignment( &tex.surface );
	GX2InitTextureRegs( &tex );

	// image and mip chain in one allocation. Not cleared: the GPU fills every level
	// that gets used (the sampler's max LOD is the highest uploaded level), and
	// clearing all that memory made level loads slower
	tex.surface.image = memalign( tex.surface.alignment, tex.surface.imageSize + tex.surface.mipmapSize );
	// ...but the CPU's data cache may still hold changes to this memory from
	// whatever used it before (often the file the texture was loaded from). If
	// they're written back after the GPU filled in the texture, they overwrite
	// parts of it, which looked like blocks of garbage in textures on a real Wii U
	// (Cemu doesn't emulate the cache). So write them back now.
	DCFlushRange( tex.surface.image, tex.surface.imageSize + tex.surface.mipmapSize );
	tex.surface.mipmaps = tex.surface.mipmapSize ? (byte *)tex.surface.image + tex.surface.imageSize : NULL;

	if ( !PhysicallyContiguous( tex.surface.image, tex.surface.imageSize + tex.surface.mipmapSize ) ) {
		if ( nonContiguousTextures++ < 5 ) {
			common->Printf( "GX2GL: texture memory at %p (%u bytes) isn't physically contiguous\n", tex.surface.image,
				tex.surface.imageSize + tex.surface.mipmapSize );
		}
	}
	t->verify = false;
	t->allocated = true;
	t->bindVersion = ++bindVersionCounter;
	t->internalFormat = internalFormat;
	t->maxLevel = 0;
	t->samplerDirty = true;
}

// uploads RGBA8 (or convertible) pixels into one level/face of a texture through
// a linear staging surface, so GX2 handles the tiling and mip layout for us
static void UploadLevel( glTexture_t *t, int level, int face, int x, int y, int width, int height, GLenum format, const void *pixels ) {
	if ( !pixels || level >= (int)t->tex.surface.mipLevels ) {
		return;
	}

	GX2Surface stage;
	memset( &stage, 0, sizeof( stage ) );
	stage.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	stage.width = width;
	stage.height = height;
	stage.depth = 1;
	stage.mipLevels = 1;
	stage.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	stage.aa = GX2_AA_MODE1X;
	stage.use = GX2_SURFACE_USE_TEXTURE;
	stage.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &stage );
	stage.image = GX2GL_FrameAlloc( stage.imageSize, Max( (uint32_t)64, stage.alignment ) );

	const byte *src = (const byte *)pixels;
	for ( int row = 0; row < height; row++ ) {
		byte *dst = (byte *)stage.image + row * stage.pitch * 4;
		switch ( format ) {
			case GL_RGB:
				for ( int i = 0; i < width; i++, src += 3 ) {
					dst[i*4+0] = src[0]; dst[i*4+1] = src[1]; dst[i*4+2] = src[2]; dst[i*4+3] = 255;
				}
				break;
			case GL_BGR_EXT:
				for ( int i = 0; i < width; i++, src += 3 ) {
					dst[i*4+0] = src[2]; dst[i*4+1] = src[1]; dst[i*4+2] = src[0]; dst[i*4+3] = 255;
				}
				break;
			case GL_BGRA_EXT:
				for ( int i = 0; i < width; i++, src += 4 ) {
					dst[i*4+0] = src[2]; dst[i*4+1] = src[1]; dst[i*4+2] = src[0]; dst[i*4+3] = src[3];
				}
				break;
			case GL_LUMINANCE: case GL_ALPHA: case GL_INTENSITY:
				for ( int i = 0; i < width; i++, src++ ) {
					dst[i*4+0] = dst[i*4+1] = dst[i*4+2] = dst[i*4+3] = src[0];
				}
				break;
			case GL_LUMINANCE_ALPHA:
				for ( int i = 0; i < width; i++, src += 2 ) {
					dst[i*4+0] = dst[i*4+1] = dst[i*4+2] = src[0]; dst[i*4+3] = src[1];
				}
				break;
			default: // GL_RGBA
				memcpy( dst, src, width * 4 );
				src += width * 4;
				break;
		}
	}
	// GX2CopySurface() reads the staging memory through the GPU's texture cache,
	// which may still hold what an earlier upload put there (a real Wii U shows
	// that as blocks of garbage in the texture, Cemu doesn't emulate the cache)
	DCFlushRange( stage.image, stage.imageSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, stage.image, stage.imageSize );

	if ( x == 0 && y == 0 ) {
		GX2CopySurface( &stage, 0, 0, &t->tex.surface, level, Max( face, 0 ) );
	} else {
		GX2Rect rect = { 0, 0, width, height };
		GX2Point point = { x, y };
		GX2CopySurfaceEx( &stage, 0, 0, &t->tex.surface, level, Max( face, 0 ), 1, &rect, &point );
	}
	GX2GL_RestoreState();
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, t->tex.surface.image, t->tex.surface.imageSize + t->tex.surface.mipmapSize );
	totalUploads++;

	if ( level > t->maxLevel ) {
		t->maxLevel = level;
		t->samplerDirty = true;
	}
	stats.uploads++;
}

static GLuint CreateWhiteTexture( GLenum target ) {
	static const byte white[4] = { 255, 255, 255, 255 };
	GLuint name;
	glGenTextures( 1, &name );
	glTexture_t *t = textures[name];
	bool cube = target == GL_TEXTURE_CUBE_MAP_EXT;
	t->target = target;
	t->minFilter = GL_LINEAR;
	AllocTexture( t, cube, 1, 1, GL_RGBA8 );
	for ( int face = 0; face < ( cube ? 6 : 1 ); face++ ) {
		UploadLevel( t, 0, cube ? face : -1, 0, 0, 1, 1, GL_RGBA, white );
	}
	return name;
}

void APIENTRY glTexImage2D( GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
							GLint border, GLenum format, GLenum type, const GLvoid *pixels ) {
	CHECK_THREAD();
	int face = CubeFace( target );
	glTexture_t *t = BoundTexture( gl.activeUnit, face >= 0 ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D );
	if ( !t || width <= 0 || height <= 0 || ( type != GL_UNSIGNED_BYTE && !IsDepthFormat( internalFormat ) ) ) {
		return;
	}
	if ( IsDepthFormat( internalFormat ) ) {
		// only allocated here, filled with glCopyTexSubImage2D(). Always as big as the
		// screen, no matter what the renderer asked for (usually the next power of
		// two), so it can be copied in directly. Shaders using it must not depend on
		// the requested size (see soft_particle.frag)
		if ( level == 0 ) {
			AllocTexture( t, false, gx2DepthBuffer.surface.width, gx2DepthBuffer.surface.height, internalFormat );
		}
		return;
	}
	if ( IsS3TCFormat( internalFormat ) || internalFormat == GL_COMPRESSED_RGB_ARB || internalFormat == GL_COMPRESSED_RGBA_ARB ) {
		// asked to compress uncompressed data, that's too slow on the Wii U's CPU
		// (image_useCompression is 0 there anyway): store it uncompressed
		internalFormat = GL_RGBA8;
	}
	if ( level == 0 && ( face <= 0 ) ) {
		AllocTexture( t, face >= 0, width, height, internalFormat );
	}
	if ( !t->allocated || IsS3TCFormat( t->internalFormat ) ) {
		return;
	}
	UploadLevel( t, level, face, 0, 0, width, height, format, pixels );
}

void APIENTRY glTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
							   GLenum format, GLenum type, const GLvoid *pixels ) {
	CHECK_THREAD();
	int face = CubeFace( target );
	glTexture_t *t = BoundTexture( gl.activeUnit, face >= 0 ? GL_TEXTURE_CUBE_MAP_EXT : GL_TEXTURE_2D );
	if ( !t || !t->allocated || type != GL_UNSIGNED_BYTE ) {
		return;
	}
	UploadLevel( t, level, face, xoffset, yoffset, width, height, format, pixels );
}

// copies from the framebuffer (bottom-left origin) into a texture,
// from the depth buffer for depth textures
static void CopyFramebuffer( glTexture_t *t, int xoffset, int yoffset, int x, int y, int width, int height ) {
	if ( !gx2Foreground ) {
		return;
	}
	GX2Rect rect = { x, FlipY( y, height ), x + width, FlipY( y, height ) + height };
	GX2Point point = { xoffset, yoffset };
	if ( IsDepthFormat( t->internalFormat ) ) {
		if ( lastDepthTexture != gl.units[gl.activeUnit].bound2D ) {
			lastDepthTexture = gl.units[gl.activeUnit].bound2D;
			common->Printf( "GX2GL: capturing depth into texture %u (%ux%u)\n", lastDepthTexture, t->tex.surface.width, t->tex.surface.height );
		}
		// render target 1 mirrors the depth buffer (sampling depth buffer formats or
		// render targets directly didn't work in Cemu), the texture has the same size
		GX2CopySurfaceEx( &gx2DepthColorBuffer.surface, 0, 0, &t->tex.surface, 0, 0, 1, &rect, &point );
	} else {
		GX2CopySurfaceEx( &gx2ColorBuffer.surface, 0, 0, &t->tex.surface, 0, 0, 1, &rect, &point );
	}
	GX2GL_RestoreState();
	// NOTE: no GX2_INVALIDATE_MODE_TEXTURE here, Cemu takes that as "reload the texture
	// from memory", which throws away what the GPU just wrote (it's never written back)
}

void APIENTRY glCopyTexImage2D( GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border ) {
	CHECK_THREAD();
	glTexture_t *t = BoundTexture( gl.activeUnit, GL_TEXTURE_2D );
	if ( !t || level != 0 ) {
		return;
	}
	AllocTexture( t, false, width, height, internalFormat );
	CopyFramebuffer( t, 0, 0, x, y, width, height );
}

void APIENTRY glCopyTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height ) {
	CHECK_THREAD();
	glTexture_t *t = BoundTexture( gl.activeUnit, GL_TEXTURE_2D );
	if ( !t || !t->allocated || level != 0 ) {
		return;
	}
	CopyFramebuffer( t, xoffset, yoffset, x, y, width, height );
}

void APIENTRY glReadPixels( GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels ) {
	CHECK_THREAD();
	// TODO: screenshots
	if ( pixels && type == GL_UNSIGNED_BYTE ) {
		memset( pixels, 0, width * height * ( format == GL_RGBA ? 4 : 3 ) );
	}
}

} // extern "C"

/*
===============================================================================

	Extension functions, handed out by GLimp_ExtensionPointer()

===============================================================================
*/

// ARB_texture_compression with S3TC: the blocks are copied as they are (they're
// little endian, like the GPU wants them) into a linear staging surface, then
// GX2 copies that into the tiled texture
static void APIENTRY gx2_glCompressedTexImage2DARB( GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height,
													GLint border, GLsizei imageSize, const GLvoid *data ) {
	CHECK_THREAD();
	glTexture_t *t = BoundTexture( gl.activeUnit, GL_TEXTURE_2D );
	if ( !t || !data || width <= 0 || height <= 0 || !IsS3TCFormat( internalFormat ) || target != GL_TEXTURE_2D ) {
		return;
	}
	if ( level == 0 ) {
		AllocTexture( t, false, width, height, internalFormat );
	}
	if ( !t->allocated || t->internalFormat != internalFormat || level >= (int)t->tex.surface.mipLevels ) {
		return;
	}

	GX2Surface stage;
	memset( &stage, 0, sizeof( stage ) );
	stage.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	stage.width = width;
	stage.height = height;
	stage.depth = 1;
	stage.mipLevels = 1;
	stage.format = SurfaceFormat( internalFormat );
	stage.aa = GX2_AA_MODE1X;
	stage.use = GX2_SURFACE_USE_TEXTURE;
	stage.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &stage );
	stage.image = GX2GL_FrameAlloc( stage.imageSize, Max( (uint32_t)64, stage.alignment ) );

	int blockBytes = ( internalFormat <= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ) ? 8 : 16;
	int blocksWide = ( width + 3 ) / 4;
	int blocksHigh = ( height + 3 ) / 4;
	// for block compressed formats the pitch is in blocks
	uint32_t rowBytes = stage.pitch * blockBytes;
	if ( rowBytes < (uint32_t)( blocksWide * blockBytes ) || rowBytes * blocksHigh > stage.imageSize
		|| blocksWide * blockBytes * blocksHigh > imageSize ) {
		common->Warning( "GX2GL: unexpected layout for a %dx%d compressed texture (pitch %u, %u bytes)", width, height, stage.pitch, stage.imageSize );
		return;
	}
	const byte *src = (const byte *)data;
	for ( int row = 0; row < blocksHigh; row++ ) {
		memcpy( (byte *)stage.image + row * rowBytes, src + row * blocksWide * blockBytes, blocksWide * blockBytes );
	}
	if ( level == 0 ) {
		t->level0Sum = BlockRowsSum( (const byte *)data, blocksWide * blockBytes, blocksHigh, blocksWide * blockBytes );
		t->verify = true;
	}
	// GX2CopySurface() reads the staging memory through the GPU's texture cache,
	// which may still hold what an earlier upload put there (a real Wii U shows
	// that as blocks of garbage in the texture, Cemu doesn't emulate the cache)
	DCFlushRange( stage.image, stage.imageSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, stage.image, stage.imageSize );

	GX2CopySurface( &stage, 0, 0, &t->tex.surface, level, 0 );
	GX2GL_RestoreState();
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, t->tex.surface.image, t->tex.surface.imageSize + t->tex.surface.mipmapSize );
	totalCompressedUploads++;

	static int checkedAtUpload;
	if ( level == 0 && r_gx2VerifyTextures.GetBool() && gx2Foreground && checkedAtUpload < 150 ) {
		checkedAtUpload++;
		if ( VerifyTexture( t, gl.units[gl.activeUnit].bound2D, "right after upload" ) ) {
			verifiedOkAtUpload++;
		} else {
			verifiedBadAtUpload++;
		}
	}

	if ( level > t->maxLevel ) {
		t->maxLevel = level;
		t->samplerDirty = true;
	}
	stats.uploads++;
}

static void APIENTRY gx2_glGetCompressedTexImageARB( GLenum target, GLint level, GLvoid *img ) {
	CHECK_THREAD();
	// only used to write .dds files, which isn't supported
}

static void APIENTRY gx2_glActiveTextureARB( GLenum texture ) {
	CHECK_THREAD();
	gl.activeUnit = Min( (int)( texture - GL_TEXTURE0_ARB ), MAX_UNITS - 1 );
}

static void APIENTRY gx2_glClientActiveTextureARB( GLenum texture ) {
	CHECK_THREAD();
	gl.clientActiveUnit = Min( (int)( texture - GL_TEXTURE0_ARB ), MAX_UNITS - 1 );
}

static void APIENTRY gx2_glMultiTexCoord2fARB( GLenum target, GLfloat s, GLfloat t ) {
	CHECK_THREAD();
	float *tc = gl.units[Min( (int)( target - GL_TEXTURE0_ARB ), MAX_UNITS - 1 )].currentTexCoord;
	tc[0] = s; tc[1] = t; tc[2] = 0.0f; tc[3] = 1.0f;
}

static void APIENTRY gx2_glMultiTexCoord2fvARB( GLenum target, GLfloat *v ) {
	CHECK_THREAD();
	gx2_glMultiTexCoord2fARB( target, v[0], v[1] );
}

// ARB_vertex_buffer_object

static void APIENTRY gx2_glGenBuffersARB( GLsizei n, GLuint *names ) {
	OSLockMutex( &bufferMutex );
	for ( int i = 0; i < n; i++ ) {
		GLuint name = numBufferNames;
		int page = name / BUFFER_PAGE_SIZE;
		if ( page >= MAX_BUFFER_PAGES ) {
			common->FatalError( "GX2GL: out of buffer objects" );
		}
		if ( !bufferPages[page] ) {
			bufferPages[page] = (glBuffer_t **)calloc( BUFFER_PAGE_SIZE, sizeof( glBuffer_t * ) );
		}
		glBuffer_t *b = new glBuffer_t;
		b->data = NULL;
		b->size = 0;
		bufferPages[page][name % BUFFER_PAGE_SIZE] = b;
		numBufferNames = name + 1;	// only now other threads may look it up
		names[i] = name;
	}
	OSUnlockMutex( &bufferMutex );
}

static void APIENTRY gx2_glDeleteBuffersARB( GLsizei n, const GLuint *names ) {
	for ( int i = 0; i < n; i++ ) {
		OSLockMutex( &bufferMutex );
		glBuffer_t *b = Buffer( names[i] );
		if ( b ) {
			DeferredFree( b->data );
			delete b;
			bufferPages[names[i] / BUFFER_PAGE_SIZE][names[i] % BUFFER_PAGE_SIZE] = NULL;
		}
		OSUnlockMutex( &bufferMutex );
	}
}

static GLboolean APIENTRY gx2_glIsBufferARB( GLuint name ) {
	return Buffer( name ) != NULL;
}

static void APIENTRY gx2_glBindBufferARB( GLenum target, GLuint name ) {
	if ( target == GL_ELEMENT_ARRAY_BUFFER_ARB ) {
		Bindings().elementArrayBuffer = name;
	} else {
		Bindings().arrayBuffer = name;
	}
}

static glBuffer_t *BoundBuffer( GLenum target ) {
	bufferBindings_t &b = Bindings();
	return Buffer( target == GL_ELEMENT_ARRAY_BUFFER_ARB ? b.elementArrayBuffer : b.arrayBuffer );
}

static void APIENTRY gx2_glBufferDataARB( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage ) {
	glBuffer_t *b = BoundBuffer( target );
	if ( !b ) {
		return;
	}
	if ( b->size != size ) {
		DeferredFree( b->data );
		b->data = (byte *)memalign( GX2_VERTEX_BUFFER_ALIGNMENT, size );
		b->size = size;
	}
	if ( data ) {
		memcpy( b->data, data, size );
		DCFlushRange( b->data, size );
	}
}

static void APIENTRY gx2_glBufferSubDataARB( GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data ) {
	glBuffer_t *b = BoundBuffer( target );
	if ( !b || offset + size > b->size ) {
		return;
	}
	memcpy( b->data + offset, data, size );
	DCFlushRange( b->data + offset, size );
}

static GLvoid * APIENTRY gx2_glMapBufferARB( GLenum target, GLenum access ) {
	glBuffer_t *b = BoundBuffer( target );
	return b ? b->data : NULL;
}

static GLboolean APIENTRY gx2_glUnmapBufferARB( GLenum target ) {
	glBuffer_t *b = BoundBuffer( target );
	if ( b ) {
		DCFlushRange( b->data, b->size );
	}
	return GL_TRUE;
}

// ARB_vertex_program / ARB_fragment_program

static void APIENTRY gx2_glGenProgramsARB( GLsizei n, GLuint *names ) {
	CHECK_THREAD();
	// the renderer binds its programs by fixed idents, this isn't used
	static GLuint next = MAX_PROGRAM_IDENTS / 2;
	for ( int i = 0; i < n; i++ ) {
		names[i] = next < MAX_PROGRAM_IDENTS ? next++ : 0;
	}
}

static void APIENTRY gx2_glBindProgramARB( GLenum target, GLuint name ) {
	CHECK_THREAD();
	if ( target == GL_VERTEX_PROGRAM_ARB ) {
		gl.boundVertexProgram = name;
	} else {
		gl.boundFragmentProgram = name;
	}
}

static void APIENTRY gx2_glProgramStringARB( GLenum target, GLenum format, GLsizei len, const GLvoid *string ) {
	CHECK_THREAD();
	// can't compile ARB programs, see GX2GL_NameProgram()
}

void GX2GL_NameProgram( GLenum target, GLuint ident, const char *name ) {
	CHECK_THREAD();
	glProgram_t *p = Program( target == GL_VERTEX_PROGRAM_ARB ? 0 : 1, ident );
	if ( !p ) {
		return;
	}
	idStr base = name;
	base.StripPath();
	base.StripFileExtension();
	p->shader = FindShader( base.c_str() );
	if ( !p->shader ) {
		common->Printf( " (no GLSL port for the Wii U, skipped)" );
	}
}

static void APIENTRY gx2_glProgramEnvParameter4fvARB( GLenum target, GLuint index, const GLfloat *params ) {
	CHECK_THREAD();
	if ( index < 256 ) {
		memcpy( gl.programEnv[target == GL_VERTEX_PROGRAM_ARB ? 0 : 1][index], params, 4 * sizeof( float ) );
	}
}

static void APIENTRY gx2_glProgramLocalParameter4fvARB( GLenum target, GLuint index, const GLfloat *params ) {
	CHECK_THREAD();
	bool vertex = target == GL_VERTEX_PROGRAM_ARB;
	glProgram_t *p = Program( vertex ? 0 : 1, vertex ? gl.boundVertexProgram : gl.boundFragmentProgram );
	if ( p && index < MAX_LOCAL_PARMS ) {
		memcpy( p->local[index], params, 4 * sizeof( float ) );
	}
}

static void APIENTRY gx2_glVertexAttribPointerARB( GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *ptr ) {
	CHECK_THREAD();
	if ( index < MAX_VERTEX_ATTRIBS ) {
		SetArray( gl.vertexAttribs[index], size, type, stride, ptr );
	}
}

static void APIENTRY gx2_glEnableVertexAttribArrayARB( GLuint index ) {
	CHECK_THREAD();
	if ( index < MAX_VERTEX_ATTRIBS ) {
		gl.vertexAttribs[index].enabled = true;
	}
}

static void APIENTRY gx2_glDisableVertexAttribArrayARB( GLuint index ) {
	CHECK_THREAD();
	if ( index < MAX_VERTEX_ATTRIBS ) {
		gl.vertexAttribs[index].enabled = false;
	}
}

struct extProc_t {
	const char *	name;
	GLExtension_t	proc;
};

#define EXTPROC( name ) { #name, (GLExtension_t)gx2_##name }
static const extProc_t extProcs[] = {
	EXTPROC( glActiveTextureARB ),
	EXTPROC( glClientActiveTextureARB ),
	EXTPROC( glMultiTexCoord2fARB ),
	EXTPROC( glMultiTexCoord2fvARB ),
	EXTPROC( glGenBuffersARB ),
	EXTPROC( glDeleteBuffersARB ),
	EXTPROC( glIsBufferARB ),
	EXTPROC( glBindBufferARB ),
	EXTPROC( glBufferDataARB ),
	EXTPROC( glBufferSubDataARB ),
	EXTPROC( glMapBufferARB ),
	EXTPROC( glUnmapBufferARB ),
	EXTPROC( glCompressedTexImage2DARB ),
	EXTPROC( glGetCompressedTexImageARB ),
	EXTPROC( glGenProgramsARB ),
	EXTPROC( glBindProgramARB ),
	EXTPROC( glProgramStringARB ),
	EXTPROC( glProgramEnvParameter4fvARB ),
	EXTPROC( glProgramLocalParameter4fvARB ),
	EXTPROC( glVertexAttribPointerARB ),
	EXTPROC( glEnableVertexAttribArrayARB ),
	EXTPROC( glDisableVertexAttribArrayARB ),
	{ "glStencilOpSeparate", (GLExtension_t)glStencilOpSeparate },
};

// debug: clears the screen and draws a few known triangles through the GL emulation:
// a red one in the middle pointing up, green and blue ones in the top left and
// bottom right corners (through a GUI style 640x480 ortho matrix)
void GX2GL_DrawTestPattern( int mode ) {
	if ( mode == 2 || mode == 3 ) {
		// show the last captured depth texture (packed, red is the coarse depth),
		// 3: a white texture, to test this
		GLuint name = mode == 3 ? whiteTexture2D : lastDepthTexture;
		static int printed;
		if ( !printed++ ) {
			glTexture_t *t = ( name > 0 && (int)name < textures.Num() ) ? textures[name] : NULL;
			common->Printf( "GX2GL test: texture %u, %s, %ux%u, format 0x%x, internal 0x%x\n", name,
				t && t->allocated ? "allocated" : "NOT allocated", t ? t->tex.surface.width : 0, t ? t->tex.surface.height : 0,
				t ? t->tex.surface.format : 0, t ? t->internalFormat : 0 );
		}
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_BLEND );
		glDisable( GL_CULL_FACE );
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_ALPHA_TEST );
		gl.vertexProgram = gl.fragmentProgram = false;
		glViewport( 0, 0, gx2ColorBuffer.surface.width, gx2ColorBuffer.surface.height );
		glMatrixMode( GL_PROJECTION );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();
		gx2_glActiveTextureARB( GL_TEXTURE0_ARB );
		glBindTexture( GL_TEXTURE_2D, name );
		glEnable( GL_TEXTURE_2D );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
		glBegin( GL_QUADS );
		glTexCoord2f( 0, 1 ); glVertex2f( -1, -1 );
		glTexCoord2f( 1, 1 ); glVertex2f( 1, -1 );
		glTexCoord2f( 1, 0 ); glVertex2f( 1, 1 );
		glTexCoord2f( 0, 0 ); glVertex2f( -1, 1 );
		glEnd();
		glDisable( GL_TEXTURE_2D );
		return;
	}
	GX2ClearColor( &gx2ColorBuffer, 0.1f, 0.1f, 0.4f, 1.0f );
	GX2GL_RestoreState();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_TEXTURE_2D );
	gl.vertexProgram = gl.fragmentProgram = false;
	glViewport( 0, 0, gx2ColorBuffer.surface.width, gx2ColorBuffer.surface.height );

	// identity matrices: red triangle in the middle of the screen, pointing up
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glBegin( GL_TRIANGLES );
	glColor3f( 1, 0, 0 );
	glVertex2f( -0.25f, -0.25f );
	glVertex2f( 0.25f, -0.25f );
	glVertex2f( 0.0f, 0.25f );
	glEnd();

	// GUI style 640x480 ortho: green quad in the top left corner, blue in the bottom right
	glMatrixMode( GL_PROJECTION );
	glOrtho( 0, 640, 480, 0, -1, 1 );
	glBegin( GL_TRIANGLES );
	glColor3f( 0, 1, 0 );
	glVertex2f( 0, 0 ); glVertex2f( 100, 0 ); glVertex2f( 0, 100 );
	glColor3f( 0, 0, 1 );
	glVertex2f( 640, 480 ); glVertex2f( 540, 480 ); glVertex2f( 640, 380 );
	glEnd();
}

GLExtension_t GX2GL_GetProcAddress( const char *name ) {
	for ( size_t i = 0; i < sizeof( extProcs ) / sizeof( extProcs[0] ); i++ ) {
		if ( idStr::Cmp( extProcs[i].name, name ) == 0 ) {
			return extProcs[i].proc;
		}
	}
	return NULL;
}

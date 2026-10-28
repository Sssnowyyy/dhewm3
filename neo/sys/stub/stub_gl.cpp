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
#include "sys/platform.h"

#include "renderer/tr_local.h"

#ifdef __WIIU__
// the Wii U's GX2 implementation (sys/wiiu/gx2_*.cpp) overrides the functions it
// supports, the remaining ones are still stubbed here
#define STUB_GL __attribute__((weak))
#else
#define STUB_GL
#endif

#ifdef _MSC_VER
#pragma warning(push)
// for each gl function we get an inconsistent dll linkage warning, because SDL_OpenGL.h says they're dllimport
// showing one warning is enough and it doesn't matter anyway (these stubs are for the dedicated server)
#pragma warning( once : 4273 )
#endif

STUB_GL void APIENTRY glAccum(GLenum op, GLfloat value){};
STUB_GL void APIENTRY glAlphaFunc(GLenum func, GLclampf ref){};
STUB_GL GLboolean APIENTRY glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences){ return false; };
STUB_GL void APIENTRY glArrayElement(GLint i){};
STUB_GL void APIENTRY glBegin(GLenum mode){};
STUB_GL void APIENTRY glBindTexture(GLenum target, GLuint texture){};
STUB_GL void APIENTRY glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap){};
STUB_GL void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor){};
STUB_GL void APIENTRY glBlendEquation(GLenum mode){};
STUB_GL void APIENTRY glCallList(GLuint list){};
STUB_GL void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists){};
STUB_GL void APIENTRY glClear(GLbitfield mask){};
STUB_GL void APIENTRY glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha){};
STUB_GL void APIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha){};
STUB_GL void APIENTRY glClearDepth(GLclampd depth){};
STUB_GL void APIENTRY glClearIndex(GLfloat c){};
STUB_GL void APIENTRY glClearStencil(GLint s){};
STUB_GL void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation){};
STUB_GL void APIENTRY glColor3b(GLbyte red, GLbyte green, GLbyte blue){};
STUB_GL void APIENTRY glColor3bv(const GLbyte *v){};
STUB_GL void APIENTRY glColor3d(GLdouble red, GLdouble green, GLdouble blue){};
STUB_GL void APIENTRY glColor3dv(const GLdouble *v){};
STUB_GL void APIENTRY glColor3f(GLfloat red, GLfloat green, GLfloat blue){};
STUB_GL void APIENTRY glColor3fv(const GLfloat *v){};
STUB_GL void APIENTRY glColor3i(GLint red, GLint green, GLint blue){};
STUB_GL void APIENTRY glColor3iv(const GLint *v){};
STUB_GL void APIENTRY glColor3s(GLshort red, GLshort green, GLshort blue){};
STUB_GL void APIENTRY glColor3sv(const GLshort *v){};
STUB_GL void APIENTRY glColor3ub(GLubyte red, GLubyte green, GLubyte blue){};
STUB_GL void APIENTRY glColor3ubv(const GLubyte *v){};
STUB_GL void APIENTRY glColor3ui(GLuint red, GLuint green, GLuint blue){};
STUB_GL void APIENTRY glColor3uiv(const GLuint *v){};
STUB_GL void APIENTRY glColor3us(GLushort red, GLushort green, GLushort blue){};
STUB_GL void APIENTRY glColor3usv(const GLushort *v){};
STUB_GL void APIENTRY glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha){};
STUB_GL void APIENTRY glColor4bv(const GLbyte *v){};
STUB_GL void APIENTRY glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha){};
STUB_GL void APIENTRY glColor4dv(const GLdouble *v){};
STUB_GL void APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha){};
STUB_GL void APIENTRY glColor4fv(const GLfloat *v){};
STUB_GL void APIENTRY glColor4i(GLint red, GLint green, GLint blue, GLint alpha){};
STUB_GL void APIENTRY glColor4iv(const GLint *v){};
STUB_GL void APIENTRY glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha){};
STUB_GL void APIENTRY glColor4sv(const GLshort *v){};
STUB_GL void APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha){};
STUB_GL void APIENTRY glColor4ubv(const GLubyte *v){};
STUB_GL void APIENTRY glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha){};
STUB_GL void APIENTRY glColor4uiv(const GLuint *v){};
STUB_GL void APIENTRY glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha){};
STUB_GL void APIENTRY glColor4usv(const GLushort *v){};
STUB_GL void APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha){};
STUB_GL void APIENTRY glColorMaterial(GLenum face, GLenum mode){};
STUB_GL void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type){};
STUB_GL void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border){};
STUB_GL void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border){};
STUB_GL void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width){};
STUB_GL void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height){};
STUB_GL void APIENTRY glCullFace(GLenum mode){};
STUB_GL void APIENTRY glDeleteLists(GLuint list, GLsizei range){};
STUB_GL void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures){};
STUB_GL void APIENTRY glDepthFunc(GLenum func){};
STUB_GL void APIENTRY glDepthMask(GLboolean flag){};
STUB_GL void APIENTRY glDepthRange(GLclampd zNear, GLclampd zFar){};
STUB_GL void APIENTRY glDisable(GLenum cap){};
STUB_GL void APIENTRY glDisableClientState(GLenum array){};
STUB_GL void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count){};
STUB_GL void APIENTRY glDrawBuffer(GLenum mode){};
STUB_GL void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices){};
STUB_GL void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels){};
STUB_GL void APIENTRY glEdgeFlag(GLboolean flag){};
STUB_GL void APIENTRY glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glEdgeFlagv(const GLboolean *flag){};
STUB_GL void APIENTRY glEnable(GLenum cap){};
STUB_GL void APIENTRY glEnableClientState(GLenum array){};
STUB_GL void APIENTRY glEnd(void){};
STUB_GL void APIENTRY glEndList(void){};
STUB_GL void APIENTRY glEvalCoord1d(GLdouble u){};
STUB_GL void APIENTRY glEvalCoord1dv(const GLdouble *u){};
STUB_GL void APIENTRY glEvalCoord1f(GLfloat u){};
STUB_GL void APIENTRY glEvalCoord1fv(const GLfloat *u){};
STUB_GL void APIENTRY glEvalCoord2d(GLdouble u, GLdouble v){};
STUB_GL void APIENTRY glEvalCoord2dv(const GLdouble *u){};
STUB_GL void APIENTRY glEvalCoord2f(GLfloat u, GLfloat v){};
STUB_GL void APIENTRY glEvalCoord2fv(const GLfloat *u){};
STUB_GL void APIENTRY glEvalMesh1(GLenum mode, GLint i1, GLint i2){};
STUB_GL void APIENTRY glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2){};
STUB_GL void APIENTRY glEvalPoint1(GLint i){};
STUB_GL void APIENTRY glEvalPoint2(GLint i, GLint j){};
STUB_GL void APIENTRY glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer){};
STUB_GL void APIENTRY glFinish(void){};
STUB_GL void APIENTRY glFlush(void){};
STUB_GL void APIENTRY glFogf(GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glFogfv(GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glFogi(GLenum pname, GLint param){};
STUB_GL void APIENTRY glFogiv(GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glFrontFace(GLenum mode){};
STUB_GL void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar){};
STUB_GL GLuint APIENTRY glGenLists(GLsizei range){return 0;};
STUB_GL void APIENTRY glGenTextures(GLsizei n, GLuint *textures){};
STUB_GL void APIENTRY glGetBooleanv(GLenum pname, GLboolean *params){};
STUB_GL void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation){};
STUB_GL void APIENTRY glGetDoublev(GLenum pname, GLdouble *params){};
STUB_GL GLenum APIENTRY glGetError(void){return 0;};
STUB_GL void APIENTRY glGetFloatv(GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetIntegerv(GLenum pname, GLint *params){
	switch( pname ) {
		case GL_MAX_TEXTURE_SIZE: *params = 1024; break;
		case GL_MAX_TEXTURE_UNITS_ARB: *params = 2; break;
		default: *params = 0; break;
	}
};
STUB_GL void APIENTRY glGetLightfv(GLenum light, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetLightiv(GLenum light, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glGetMapdv(GLenum target, GLenum query, GLdouble *v){};
STUB_GL void APIENTRY glGetMapfv(GLenum target, GLenum query, GLfloat *v){};
STUB_GL void APIENTRY glGetMapiv(GLenum target, GLenum query, GLint *v){};
STUB_GL void APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetMaterialiv(GLenum face, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glGetPixelMapfv(GLenum map, GLfloat *values){};
STUB_GL void APIENTRY glGetPixelMapuiv(GLenum map, GLuint *values){};
STUB_GL void APIENTRY glGetPixelMapusv(GLenum map, GLushort *values){};
STUB_GL void APIENTRY glGetPointerv(GLenum pname, GLvoid* *params){};
STUB_GL void APIENTRY glGetPolygonStipple(GLubyte *mask){};
STUB_GL const GLubyte * APIENTRY glGetString(GLenum name){
	switch( name ) {
		case GL_EXTENSIONS: return (GLubyte *)"GL_ARB_multitexture GL_ARB_texture_env_combine GL_ARB_texture_cube_map GL_ARB_texture_env_dot3";
	}
	return (const GLubyte *)"";
};
STUB_GL void APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params){};
STUB_GL void APIENTRY glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetTexGeniv(GLenum coord, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels){};
STUB_GL void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params){};
STUB_GL void APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params){};
STUB_GL void APIENTRY glHint(GLenum target, GLenum mode){};
STUB_GL void APIENTRY glIndexMask(GLuint mask){};
STUB_GL void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glIndexd(GLdouble c){};
STUB_GL void APIENTRY glIndexdv(const GLdouble *c){};
STUB_GL void APIENTRY glIndexf(GLfloat c){};
STUB_GL void APIENTRY glIndexfv(const GLfloat *c){};
STUB_GL void APIENTRY glIndexi(GLint c){};
STUB_GL void APIENTRY glIndexiv(const GLint *c){};
STUB_GL void APIENTRY glIndexs(GLshort c){};
STUB_GL void APIENTRY glIndexsv(const GLshort *c){};
STUB_GL void APIENTRY glIndexub(GLubyte c){};
STUB_GL void APIENTRY glIndexubv(const GLubyte *c){};
STUB_GL void APIENTRY glInitNames(void){};
STUB_GL void APIENTRY glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer){};
STUB_GL GLboolean APIENTRY glIsEnabled(GLenum cap){ return false; };
STUB_GL GLboolean APIENTRY glIsList(GLuint list){ return false; };
STUB_GL GLboolean APIENTRY glIsTexture(GLuint texture){ return false; };
STUB_GL void APIENTRY glLightModelf(GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glLightModeli(GLenum pname, GLint param){};
STUB_GL void APIENTRY glLightModeliv(GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glLighti(GLenum light, GLenum pname, GLint param){};
STUB_GL void APIENTRY glLightiv(GLenum light, GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glLineStipple(GLint factor, GLushort pattern){};
STUB_GL void APIENTRY glLineWidth(GLfloat width){};
STUB_GL void APIENTRY glListBase(GLuint base){};
STUB_GL void APIENTRY glLoadIdentity(void){};
STUB_GL void APIENTRY glLoadMatrixd(const GLdouble *m){};
STUB_GL void APIENTRY glLoadMatrixf(const GLfloat *m){};
STUB_GL void APIENTRY glLoadName(GLuint name){};
STUB_GL void APIENTRY glLogicOp(GLenum opcode){};
STUB_GL void APIENTRY glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points){};
STUB_GL void APIENTRY glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points){};
STUB_GL void APIENTRY glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points){};
STUB_GL void APIENTRY glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points){};
STUB_GL void APIENTRY glMapGrid1d(GLint un, GLdouble u1, GLdouble u2){};
STUB_GL void APIENTRY glMapGrid1f(GLint un, GLfloat u1, GLfloat u2){};
STUB_GL void APIENTRY glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2){};
STUB_GL void APIENTRY glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2){};
STUB_GL void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param){};
STUB_GL void APIENTRY glMaterialiv(GLenum face, GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glMatrixMode(GLenum mode){};
STUB_GL void APIENTRY glMultMatrixd(const GLdouble *m){};
STUB_GL void APIENTRY glMultMatrixf(const GLfloat *m){};
STUB_GL void APIENTRY glNewList(GLuint list, GLenum mode){};
STUB_GL void APIENTRY glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz){};
STUB_GL void APIENTRY glNormal3bv(const GLbyte *v){};
STUB_GL void APIENTRY glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz){};
STUB_GL void APIENTRY glNormal3dv(const GLdouble *v){};
STUB_GL void APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz){};
STUB_GL void APIENTRY glNormal3fv(const GLfloat *v){};
STUB_GL void APIENTRY glNormal3i(GLint nx, GLint ny, GLint nz){};
STUB_GL void APIENTRY glNormal3iv(const GLint *v){};
STUB_GL void APIENTRY glNormal3s(GLshort nx, GLshort ny, GLshort nz){};
STUB_GL void APIENTRY glNormal3sv(const GLshort *v){};
STUB_GL void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar){};
STUB_GL void APIENTRY glPassThrough(GLfloat token){};
STUB_GL void APIENTRY glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values){};
STUB_GL void APIENTRY glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values){};
STUB_GL void APIENTRY glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values){};
STUB_GL void APIENTRY glPixelStoref(GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glPixelStorei(GLenum pname, GLint param){};
STUB_GL void APIENTRY glPixelTransferf(GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glPixelTransferi(GLenum pname, GLint param){};
STUB_GL void APIENTRY glPixelZoom(GLfloat xfactor, GLfloat yfactor){};
STUB_GL void APIENTRY glPointSize(GLfloat size){};
STUB_GL void APIENTRY glPolygonMode(GLenum face, GLenum mode){};
STUB_GL void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units){};
STUB_GL void APIENTRY glPolygonStipple(const GLubyte *mask){};
STUB_GL void APIENTRY glPopAttrib(void){};
STUB_GL void APIENTRY glPopClientAttrib(void){};
STUB_GL void APIENTRY glPopMatrix(void){};
STUB_GL void APIENTRY glPopName(void){};
STUB_GL void APIENTRY glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities){};
STUB_GL void APIENTRY glPushAttrib(GLbitfield mask){};
STUB_GL void APIENTRY glPushClientAttrib(GLbitfield mask){};
STUB_GL void APIENTRY glPushMatrix(void){};
STUB_GL void APIENTRY glPushName(GLuint name){};
STUB_GL void APIENTRY glRasterPos2d(GLdouble x, GLdouble y){};
STUB_GL void APIENTRY glRasterPos2dv(const GLdouble *v){};
STUB_GL void APIENTRY glRasterPos2f(GLfloat x, GLfloat y){};
STUB_GL void APIENTRY glRasterPos2fv(const GLfloat *v){};
STUB_GL void APIENTRY glRasterPos2i(GLint x, GLint y){};
STUB_GL void APIENTRY glRasterPos2iv(const GLint *v){};
STUB_GL void APIENTRY glRasterPos2s(GLshort x, GLshort y){};
STUB_GL void APIENTRY glRasterPos2sv(const GLshort *v){};
STUB_GL void APIENTRY glRasterPos3d(GLdouble x, GLdouble y, GLdouble z){};
STUB_GL void APIENTRY glRasterPos3dv(const GLdouble *v){};
STUB_GL void APIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z){};
STUB_GL void APIENTRY glRasterPos3fv(const GLfloat *v){};
STUB_GL void APIENTRY glRasterPos3i(GLint x, GLint y, GLint z){};
STUB_GL void APIENTRY glRasterPos3iv(const GLint *v){};
STUB_GL void APIENTRY glRasterPos3s(GLshort x, GLshort y, GLshort z){};
STUB_GL void APIENTRY glRasterPos3sv(const GLshort *v){};
STUB_GL void APIENTRY glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w){};
STUB_GL void APIENTRY glRasterPos4dv(const GLdouble *v){};
STUB_GL void APIENTRY glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w){};
STUB_GL void APIENTRY glRasterPos4fv(const GLfloat *v){};
STUB_GL void APIENTRY glRasterPos4i(GLint x, GLint y, GLint z, GLint w){};
STUB_GL void APIENTRY glRasterPos4iv(const GLint *v){};
STUB_GL void APIENTRY glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w){};
STUB_GL void APIENTRY glRasterPos4sv(const GLshort *v){};
STUB_GL void APIENTRY glReadBuffer(GLenum mode){};
STUB_GL void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels){};
STUB_GL void APIENTRY glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2){};
STUB_GL void APIENTRY glRectdv(const GLdouble *v1, const GLdouble *v2){};
STUB_GL void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2){};
STUB_GL void APIENTRY glRectfv(const GLfloat *v1, const GLfloat *v2){};
STUB_GL void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2){};
STUB_GL void APIENTRY glRectiv(const GLint *v1, const GLint *v2){};
STUB_GL void APIENTRY glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2){};
STUB_GL void APIENTRY glRectsv(const GLshort *v1, const GLshort *v2){};
STUB_GL GLint APIENTRY glRenderMode(GLenum mode){ return 0; };
STUB_GL void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z){};
STUB_GL void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z){};
STUB_GL void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z){};
STUB_GL void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z){};
STUB_GL void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height){};
STUB_GL void APIENTRY glSelectBuffer(GLsizei size, GLuint *buffer){};
STUB_GL void APIENTRY glShadeModel(GLenum mode){};
STUB_GL void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask){};
STUB_GL void APIENTRY glStencilMask(GLuint mask){};
STUB_GL void APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass){};
STUB_GL void APIENTRY glTexCoord1d(GLdouble s){};
STUB_GL void APIENTRY glTexCoord1dv(const GLdouble *v){};
STUB_GL void APIENTRY glTexCoord1f(GLfloat s){};
STUB_GL void APIENTRY glTexCoord1fv(const GLfloat *v){};
STUB_GL void APIENTRY glTexCoord1i(GLint s){};
STUB_GL void APIENTRY glTexCoord1iv(const GLint *v){};
STUB_GL void APIENTRY glTexCoord1s(GLshort s){};
STUB_GL void APIENTRY glTexCoord1sv(const GLshort *v){};
STUB_GL void APIENTRY glTexCoord2d(GLdouble s, GLdouble t){};
STUB_GL void APIENTRY glTexCoord2dv(const GLdouble *v){};
STUB_GL void APIENTRY glTexCoord2f(GLfloat s, GLfloat t){};
STUB_GL void APIENTRY glTexCoord2fv(const GLfloat *v){};
STUB_GL void APIENTRY glTexCoord2i(GLint s, GLint t){};
STUB_GL void APIENTRY glTexCoord2iv(const GLint *v){};
STUB_GL void APIENTRY glTexCoord2s(GLshort s, GLshort t){};
STUB_GL void APIENTRY glTexCoord2sv(const GLshort *v){};
STUB_GL void APIENTRY glTexCoord3d(GLdouble s, GLdouble t, GLdouble r){};
STUB_GL void APIENTRY glTexCoord3dv(const GLdouble *v){};
STUB_GL void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r){};
STUB_GL void APIENTRY glTexCoord3fv(const GLfloat *v){};
STUB_GL void APIENTRY glTexCoord3i(GLint s, GLint t, GLint r){};
STUB_GL void APIENTRY glTexCoord3iv(const GLint *v){};
STUB_GL void APIENTRY glTexCoord3s(GLshort s, GLshort t, GLshort r){};
STUB_GL void APIENTRY glTexCoord3sv(const GLshort *v){};
STUB_GL void APIENTRY glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q){};
STUB_GL void APIENTRY glTexCoord4dv(const GLdouble *v){};
STUB_GL void APIENTRY glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q){};
STUB_GL void APIENTRY glTexCoord4fv(const GLfloat *v){};
STUB_GL void APIENTRY glTexCoord4i(GLint s, GLint t, GLint r, GLint q){};
STUB_GL void APIENTRY glTexCoord4iv(const GLint *v){};
STUB_GL void APIENTRY glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q){};
STUB_GL void APIENTRY glTexCoord4sv(const GLshort *v){};
STUB_GL void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param){};
STUB_GL void APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glTexGend(GLenum coord, GLenum pname, GLdouble param){};
STUB_GL void APIENTRY glTexGendv(GLenum coord, GLenum pname, const GLdouble *params){};
STUB_GL void APIENTRY glTexGenf(GLenum coord, GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glTexGeni(GLenum coord, GLenum pname, GLint param){};
STUB_GL void APIENTRY glTexGeniv(GLenum coord, GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels){};
STUB_GL void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {};
STUB_GL void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param){};
STUB_GL void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params){};
STUB_GL void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param){};
STUB_GL void APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params){};
STUB_GL void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels){};
STUB_GL void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels){};
STUB_GL void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z){};
STUB_GL void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z){};
STUB_GL void APIENTRY glVertex2d(GLdouble x, GLdouble y){};
STUB_GL void APIENTRY glVertex2dv(const GLdouble *v){};
STUB_GL void APIENTRY glVertex2f(GLfloat x, GLfloat y){};
STUB_GL void APIENTRY glVertex2fv(const GLfloat *v){};
STUB_GL void APIENTRY glVertex2i(GLint x, GLint y){};
STUB_GL void APIENTRY glVertex2iv(const GLint *v){};
STUB_GL void APIENTRY glVertex2s(GLshort x, GLshort y){};
STUB_GL void APIENTRY glVertex2sv(const GLshort *v){};
STUB_GL void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z){};
STUB_GL void APIENTRY glVertex3dv(const GLdouble *v){};
STUB_GL void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z){};
STUB_GL void APIENTRY glVertex3fv(const GLfloat *v){};
STUB_GL void APIENTRY glVertex3i(GLint x, GLint y, GLint z){};
STUB_GL void APIENTRY glVertex3iv(const GLint *v){};
STUB_GL void APIENTRY glVertex3s(GLshort x, GLshort y, GLshort z){};
STUB_GL void APIENTRY glVertex3sv(const GLshort *v){};
STUB_GL void APIENTRY glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w){};
STUB_GL void APIENTRY glVertex4dv(const GLdouble *v){};
STUB_GL void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w){};
STUB_GL void APIENTRY glVertex4fv(const GLfloat *v){};
STUB_GL void APIENTRY glVertex4i(GLint x, GLint y, GLint z, GLint w){};
STUB_GL void APIENTRY glVertex4iv(const GLint *v){};
STUB_GL void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w){};
STUB_GL void APIENTRY glVertex4sv(const GLshort *v){};
STUB_GL void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer){};
STUB_GL void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height){};

static void StubFunction( void ) {};

// the renderer loads all core GL functions (qgl*) through GLimp_ExtensionPointer(),
// so hand out the matching stub - otherwise e.g. qglGetString() would return garbage.
// (the dedicated server never initializes the renderer, but headless builds do)
struct stubGLProc_t {
	const char *	name;
	GLExtension_t	proc;
};

static const stubGLProc_t stubGLProcs[] = {
#define QGLPROC(name, rettype, args) { #name, (GLExtension_t)name },
#include "renderer/qgl_proc.h"
};

STUB_GL GLExtension_t GLimp_ExtensionPointer( const char *name ) {
	for ( size_t i = 0; i < sizeof( stubGLProcs ) / sizeof( stubGLProcs[0] ); i++ ) {
		if ( idStr::Cmp( stubGLProcs[i].name, name ) == 0 ) {
			return stubGLProcs[i].proc;
		}
	}
	// extension functions
	return StubFunction;
};

STUB_GL bool GLimp_Init(glimpParms_t a) {return true;};
STUB_GL void GLimp_SetGamma(unsigned short*a, unsigned short*b, unsigned short*c) {};
STUB_GL void GLimp_ResetGamma() {}
STUB_GL bool GLimp_SetScreenParms(glimpParms_t parms) { return true; };
STUB_GL void GLimp_Shutdown() {};
STUB_GL void GLimp_SwapBuffers() {};
STUB_GL void GLimp_ActivateContext() {};
STUB_GL void GLimp_DeactivateContext() {};
STUB_GL void GLimp_GrabInput(int flags) {};
STUB_GL bool GLimp_SetSwapInterval( int swapInterval ) { return false; }
STUB_GL int GLimp_GetSwapInterval() { return 0; }
STUB_GL void GLimp_UpdateWindowSize() {}
STUB_GL bool GLimp_SetWindowResizable( bool enableResizable ) { return false; }
STUB_GL glimpParms_t GLimp_GetCurState() { glimpParms_t ret = {}; return ret; }
STUB_GL float GLimp_GetDisplayRefresh() { return 0.0f; }

#ifdef _MSC_VER
#pragma warning(pop)
#endif

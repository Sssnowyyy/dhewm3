#version 330
// port of glprogs/heatHazeWithMask.vfp (vertex part)
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModelView;
uniform mat4 uProjection;
uniform vec4 uLocal[2];	// 0: texcoord scroll, 1: deform magnitude

out vec4 vTex0;
out vec4 vTex1;
out vec4 vTex2;
out vec4 vColor;

void main() {
	gl_Position = uMVP * aPosition;
	vTex0 = aTexCoord;
	vTex1 = aTexCoord + uLocal[0];

	// texture deform magnitude, scaled by the projected distance
	vec4 v = vec4( 1.0, 0.0, ( uModelView * aPosition ).z, 1.0 );
	float x = ( uProjection * v ).x;
	float w = max( ( uProjection * v ).w, 1.0 );
	vTex2 = min( x / w, 0.02 ) * uLocal[1];

	vColor = aColor;
}

#version 330
// port of glprogs/colorProcess.vfp (vertex part)
layout(location = 0) in vec4 aPosition;

uniform mat4 uMVP;
uniform vec4 uLocal[2];	// 0: fraction, 1: target color

out vec4 vTex0;
out vec4 vTex1;

void main() {
	gl_Position = uMVP * aPosition;
	vTex0 = vec4( 1.0 ) - uLocal[0];
	vTex1 = uLocal[1] * uLocal[0];
}

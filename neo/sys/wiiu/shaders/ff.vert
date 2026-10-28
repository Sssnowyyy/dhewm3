#version 330
// fixed function emulation: per unit tc = uTexGen * position + uTexMat * texcoord,
// with object linear texgen and the texture matrix folded into those on the CPU
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aTexCoord;
layout(location = 3) in vec4 aTexCoord1;

uniform mat4 uMVP;
uniform mat4 uTexGen0;
uniform mat4 uTexMat0;
uniform mat4 uTexGen1;
uniform mat4 uTexMat1;

out vec4 vColor;
out vec4 vTex0;
out vec4 vTex1;

void main() {
	gl_Position = uMVP * aPosition;
	vColor = aColor;
	vTex0 = uTexGen0 * aPosition + uTexMat0 * aTexCoord;
	vTex1 = uTexGen1 * aPosition + uTexMat1 * aTexCoord1;
}

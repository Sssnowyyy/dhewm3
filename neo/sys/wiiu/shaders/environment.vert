#version 330
// port of glprogs/environment.vfp (vertex part)
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 4) in vec4 aNormal;

uniform mat4 uMVP;
uniform vec4 uEnv[6];	// 5: local view origin

out vec4 vTex0;
out vec4 vTex1;
out vec4 vColor;

void main() {
	gl_Position = uMVP * aPosition;
	vTex0 = aNormal;
	vTex1 = uEnv[5] - aPosition;
	vColor = aColor;
}

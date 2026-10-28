#version 330
// port of glprogs/shadow.vp: vertices with w = 0 are projected to infinity away from the light
layout(location = 0) in vec4 aPosition;

uniform mat4 uMVP;
uniform vec4 uEnv[5];	// 4: local light origin

void main() {
	vec4 p = aPosition - uEnv[4];
	p = p.wwww * uEnv[4] + p;
	gl_Position = uMVP * p;
}

#version 330
// port of dhewm3's built-in soft_particle.vfp (from The Dark Mod), vertex part
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aTexCoord;

uniform mat4 uMVP;
uniform vec4 uEnv[14];	// 12, 13: diffuse texture matrix S, T

out vec4 vTex0;
out vec4 vColor;

void main() {
	gl_Position = uMVP * aPosition;
	vTex0 = vec4( dot( aTexCoord, uEnv[12] ), dot( aTexCoord, uEnv[13] ), 0.0, 1.0 );
	vColor = aColor;
}

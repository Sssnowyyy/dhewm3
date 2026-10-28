#version 330
// port of glprogs/bumpyEnvironment.vfp (vertex part)
layout(location = 0) in vec4 aPosition;
layout(location = 2) in vec4 aTexCoord;
layout(location = 4) in vec4 aNormal;
layout(location = 9) in vec4 aAttrib9;
layout(location = 10) in vec4 aAttrib10;

uniform mat4 uMVP;
// 5: local view origin, 6-8: model matrix rows
uniform vec4 uEnv[9];

out vec4 vTex0;
out vec4 vTex1;
out vec4 vTex2;
out vec4 vTex3;
out vec4 vTex4;

void main() {
	gl_Position = uMVP * aPosition;
	vTex0 = aTexCoord;

	// eye vector in global space
	vec3 toEye = uEnv[5].xyz - aPosition.xyz;
	vTex1 = vec4( dot( toEye, uEnv[6].xyz ), dot( toEye, uEnv[7].xyz ), dot( toEye, uEnv[8].xyz ), 1.0 );

	// tangent space to global space rows
	vTex2 = vec4( dot( aAttrib9.xyz, uEnv[6].xyz ), dot( aAttrib10.xyz, uEnv[6].xyz ), dot( aNormal.xyz, uEnv[6].xyz ), 1.0 );
	vTex3 = vec4( dot( aAttrib9.xyz, uEnv[7].xyz ), dot( aAttrib10.xyz, uEnv[7].xyz ), dot( aNormal.xyz, uEnv[7].xyz ), 1.0 );
	vTex4 = vec4( dot( aAttrib9.xyz, uEnv[8].xyz ), dot( aAttrib10.xyz, uEnv[8].xyz ), dot( aNormal.xyz, uEnv[8].xyz ), 1.0 );
}

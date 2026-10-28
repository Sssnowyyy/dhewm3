#version 330
// port of glprogs/interaction.vfp (vertex part)
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 8) in vec4 aAttrib8;		// texcoord
layout(location = 9) in vec4 aAttrib9;		// tangent 0
layout(location = 10) in vec4 aAttrib10;	// tangent 1
layout(location = 11) in vec4 aAttrib11;	// normal

uniform mat4 uMVP;
// 4: local light origin, 5: local view origin, 6-8: light projection S, T, Q,
// 9: light falloff S, 10-11: bump matrix, 12-13: diffuse matrix,
// 14-15: specular matrix, 16: vertex color modulate, 17: vertex color add
uniform vec4 uEnv[18];

// packed into fewer varyings than the ARB program uses: a mostly constant
// vec4 output (the falloff texcoord) didn't make it to the pixel shader
out vec4 vTex0;				// light vector in texture space
out vec4 vBumpDiffuse;		// xy: bump map, zw: diffuse map
out vec4 vSpecularFalloff;	// xy: specular map, z: light falloff S
out vec4 vTex3;				// light projection (x, y, -, w)
out vec4 vTex6;				// half angle in texture space
out vec4 vColor;

void main() {
	gl_Position = uMVP * aPosition;

	// vector to light in texture space
	vec3 toLight = uEnv[4].xyz - aPosition.xyz;
	vTex0 = vec4( dot( aAttrib9.xyz, toLight ), dot( aAttrib10.xyz, toLight ), dot( aAttrib11.xyz, toLight ), 1.0 );

	// bump, diffuse and specular map coordinates, light falloff
	vBumpDiffuse = vec4( dot( aAttrib8, uEnv[10] ), dot( aAttrib8, uEnv[11] ), dot( aAttrib8, uEnv[12] ), dot( aAttrib8, uEnv[13] ) );
	vSpecularFalloff = vec4( dot( aAttrib8, uEnv[14] ), dot( aAttrib8, uEnv[15] ), dot( aPosition, uEnv[9] ), 1.0 );

	// light projection
	vTex3 = vec4( dot( aPosition, uEnv[6] ), dot( aPosition, uEnv[7] ), 0.0, dot( aPosition, uEnv[8] ) );

	// half angle vector in texture space
	vec3 halfAngle = normalize( toLight ) + normalize( uEnv[5].xyz - aPosition.xyz );
	vTex6 = vec4( dot( aAttrib9.xyz, halfAngle ), dot( aAttrib10.xyz, halfAngle ), dot( aAttrib11.xyz, halfAngle ), 1.0 );

	// 1.0, color or 1.0 - color
	vColor = aColor * uEnv[16] + uEnv[17];
}

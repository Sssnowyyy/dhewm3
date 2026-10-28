#version 330
// port of glprogs/interaction.vfp (fragment part)
in vec4 vTex0;
in vec4 vBumpDiffuse;
in vec4 vSpecularFalloff;
in vec4 vTex3;
in vec4 vTex6;
in vec4 vColor;
out vec4 fragColor;

uniform samplerCube tex0;	// normalization cube map
uniform sampler2D tex1;		// bump map
uniform sampler2D tex2;		// light falloff
uniform sampler2D tex3;		// light projection
uniform sampler2D tex4;		// diffuse map
uniform sampler2D tex5;		// specular map
uniform sampler2D tex6;		// specular lookup table
// 0: diffuse modifier, 1: specular modifier
uniform vec4 uEnv[2];
// x: r_gx2ShaderDebug, shows one term of the lighting equation instead
uniform vec4 uDebug;

void main() {
	vec3 specularDir = normalize( vTex6.xyz );

	vec3 lightDir = texture( tex0, vTex0.xyz ).xyz * 2.0 - 1.0;

	// normal maps have red moved to alpha (RXGB)
	vec4 localNormal = texture( tex1, vBumpDiffuse.xy );
	localNormal.x = localNormal.a;
	vec3 normal = localNormal.xyz * 2.0 - 1.0;

	vec4 light = vec4( dot( lightDir, normal ) );
	light *= textureProj( tex3, vTex3.xyw );
	light *= texture( tex2, vec2( vSpecularFalloff.z, 0.5 ) );

	vec4 color = texture( tex4, vBumpDiffuse.zw ) * uEnv[0];

	float specular = dot( specularDir, normal );
	vec4 specularColor = texture( tex6, vec2( specular, specular ) ) * uEnv[1];
	color += specularColor * ( texture( tex5, vSpecularFalloff.xy ) * 2.0 );

	fragColor = light * color * vColor;

	if ( uDebug.x > 0.5 ) {
		int d = int( uDebug.x + 0.5 );
		if ( d == 1 ) fragColor = texture( tex4, vBumpDiffuse.zw );			// diffuse map
		else if ( d == 2 ) fragColor = vec4( dot( lightDir, normal ) );	// N.L
		else if ( d == 3 ) fragColor = textureProj( tex3, vTex3.xyw ) * texture( tex2, vec2( vSpecularFalloff.z, 0.5 ) );	// light shape
		else if ( d == 4 ) fragColor = vColor;
		else if ( d == 5 ) fragColor = vec4( normal * 0.5 + 0.5, 1.0 );	// normal map
		else if ( d == 6 ) fragColor = uEnv[0];							// diffuse color
		else if ( d == 7 ) fragColor = vec4( lightDir * 0.5 + 0.5, 1.0 );	// light vector from the cube map
		else if ( d == 8 ) fragColor = vec4( normalize( vTex0.xyz ) * 0.5 + 0.5, 1.0 );	// light vector, interpolated
		else if ( d == 9 ) fragColor = textureProj( tex3, vTex3.xyw );					// light projection
		else if ( d == 10 ) fragColor = texture( tex2, vec2( vSpecularFalloff.z, 0.5 ) );				// light falloff
		else if ( d == 11 ) fragColor = vec4( fract( vTex3.xy / vTex3.w ), vTex3.w > 0.0 ? 1.0 : 0.0, 1.0 );	// projection coords
		else if ( d == 12 ) fragColor = vec4( fract( vSpecularFalloff.z ), 0.5, 0.0, 1.0 );	// falloff coords
	}
}

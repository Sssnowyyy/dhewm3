#version 330
// port of glprogs/bumpyEnvironment.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
in vec4 vTex2;
in vec4 vTex3;
in vec4 vTex4;
out vec4 fragColor;

uniform samplerCube tex0;	// environment cube map
uniform sampler2D tex1;		// normal map

void main() {
	vec4 localNormal = texture( tex1, vTex0.xy );
	localNormal.x = localNormal.a;
	vec3 normal = normalize( localNormal.xyz * 2.0 - 1.0 );
	vec3 globalNormal = vec3( dot( normal, vTex2.xyz ), dot( normal, vTex3.xyz ), dot( normal, vTex4.xyz ) );
	vec3 globalEye = normalize( vTex1.xyz );
	vec3 reflected = 2.0 * dot( globalEye, globalNormal ) * globalNormal - globalEye;
	fragColor = vec4( texture( tex0, reflected ).xyz, 1.0 );
}

#version 330
// port of glprogs/environment.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
in vec4 vColor;
out vec4 fragColor;

uniform samplerCube tex0;

void main() {
	vec3 normal = normalize( vTex0.xyz );
	vec3 toEye = normalize( vTex1.xyz );
	vec3 reflected = 2.0 * dot( toEye, normal ) * normal - toEye;
	fragColor = texture( tex0, reflected ) * vColor;
}

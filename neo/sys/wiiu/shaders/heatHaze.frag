#version 330
// port of glprogs/heatHaze.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
in vec4 vTex2;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D tex0;	// _currentRender
uniform sampler2D tex1;	// normal map
// 0: scale to the power of two _currentRender, 1: 1 / screen size
uniform vec4 uEnv[2];

void main() {
	vec4 localNormal = texture( tex1, vTex1.xy );
	localNormal.x = localNormal.a;
	localNormal = localNormal * 2.0 - 1.0;
	vec4 tc = clamp( localNormal * vTex2 + gl_FragCoord * uEnv[1], 0.0, 1.0 ) * uEnv[0];
	fragColor = vec4( texture( tex0, tc.xy ).xyz, 1.0 );
}

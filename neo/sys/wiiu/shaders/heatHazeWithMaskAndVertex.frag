#version 330
// port of glprogs/heatHazeWithMaskAndVertex.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
in vec4 vTex2;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D tex0;	// _currentRender
uniform sampler2D tex1;	// normal map
uniform sampler2D tex2;	// mask
// 0: scale to the power of two _currentRender, 1: 1 / screen size
uniform vec4 uEnv[2];

void main() {
	vec4 mask = texture( tex2, vTex0.xy );
	mask.xy *= vColor.xy;
	mask.xy -= 0.01;
	if ( any( lessThan( mask, vec4( 0.0 ) ) ) ) {
		discard;
	}
	vec4 localNormal = texture( tex1, vTex1.xy );
	localNormal.x = localNormal.a;
	localNormal = ( localNormal * 2.0 - 1.0 ) * mask;
	vec4 tc = clamp( localNormal * vTex2 + gl_FragCoord * uEnv[1], 0.0, 1.0 ) * uEnv[0];
	fragColor = vec4( texture( tex0, tc.xy ).xyz, 1.0 );
}

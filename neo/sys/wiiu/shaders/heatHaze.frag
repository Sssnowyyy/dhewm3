#version 330
// port of glprogs/heatHaze.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
in vec4 vTex2;
in vec4 vColor;
layout(location = 0) out vec4 fragColor;
// mirrors the depth buffer, for _currentDepth (see gx2_gl.cpp), packed into RGBA8
layout(location = 1) out vec4 fragDepth;

vec4 PackDepth( float z ) {
	vec4 enc = fract( min( z, 0.999999 ) * vec4( 1.0, 255.0, 65025.0, 16581375.0 ) );
	return enc - enc.yzww * vec4( 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0 );
}

uniform sampler2D tex0;	// _currentRender
uniform sampler2D tex1;	// normal map
// 0: scale to the power of two _currentRender, 1: 1 / screen size
uniform vec4 uEnv[2];

void main() {
	fragDepth = PackDepth( gl_FragCoord.z );
	vec4 localNormal = texture( tex1, vTex1.xy );
	localNormal.x = localNormal.a;
	localNormal = localNormal * 2.0 - 1.0;
	vec4 tc = clamp( localNormal * vTex2 + gl_FragCoord * uEnv[1], 0.0, 1.0 ) * uEnv[0];
	fragColor = vec4( texture( tex0, tc.xy ).xyz, 1.0 );
}

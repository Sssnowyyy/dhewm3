#version 330
// port of glprogs/colorProcess.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
layout(location = 0) out vec4 fragColor;
// mirrors the depth buffer, for _currentDepth (see gx2_gl.cpp), packed into RGBA8
layout(location = 1) out vec4 fragDepth;

vec4 PackDepth( float z ) {
	vec4 enc = fract( min( z, 0.999999 ) * vec4( 1.0, 255.0, 65025.0, 16581375.0 ) );
	return enc - enc.yzww * vec4( 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0 );
}

uniform sampler2D tex0;	// _currentRender
// 0: scale to the power of two _currentRender, 1: 1 / screen size
uniform vec4 uEnv[2];

void main() {
	fragDepth = PackDepth( gl_FragCoord.z );
	vec4 src = texture( tex0, ( gl_FragCoord * uEnv[1] * uEnv[0] ).xy );
	float gray = ( src.x + src.y + src.z ) * 0.33;
	fragColor = vec4( src.xyz * vTex0.xyz + gray * vTex1.xyz, 1.0 );
}

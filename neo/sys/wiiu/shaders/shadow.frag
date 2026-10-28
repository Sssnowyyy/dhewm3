#version 330
// shadow volumes only touch the stencil buffer
layout(location = 0) out vec4 fragColor;
// mirrors the depth buffer, for _currentDepth (see gx2_gl.cpp), packed into RGBA8
layout(location = 1) out vec4 fragDepth;

vec4 PackDepth( float z ) {
	vec4 enc = fract( min( z, 0.999999 ) * vec4( 1.0, 255.0, 65025.0, 16581375.0 ) );
	return enc - enc.yzww * vec4( 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0 );
}

void main() {
	fragDepth = PackDepth( gl_FragCoord.z );
	fragColor = vec4( 1.0 );
}

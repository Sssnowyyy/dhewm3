#version 330
// port of dhewm3's built-in soft_particle.vfp (from The Dark Mod), fragment part:
// fades particles out where they get close to the solid scene behind them
in vec4 vTex0;
in vec4 vColor;
layout(location = 0) out vec4 fragColor;
// mirrors the depth buffer, for _currentDepth (see gx2_gl.cpp), packed into RGBA8
layout(location = 1) out vec4 fragDepth;

vec4 PackDepth( float z ) {
	vec4 enc = fract( min( z, 0.999999 ) * vec4( 1.0, 255.0, 65025.0, 16581375.0 ) );
	return enc - enc.yzww * vec4( 1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0 );
}

uniform sampler2D tex0;	// particle texture
uniform sampler2D tex1;	// _currentDepth
// 23: particle radius, 1 / fade range, 1 / radius, 24: color channel mask
// (22 is 1 / the _currentDepth size the renderer asked for, but on the Wii U it
// always has the depth buffer's size, so textureSize() is used instead)
uniform vec4 uEnv[25];
// x: r_gx2ShaderDebug: 1 shows the scene depth, 2 the fade factor
uniform vec4 uDebug;

void main() {
	fragDepth = PackDepth( gl_FragCoord.z );
	// recover depths in doom units from the projection matrix's depth mapping
	const float depthScale = 0.33333333;
	const float depthBias = -0.33316667;
	// depth textures hold depth packed into RGBA8, see PackDepth()
	vec4 packedDepth = texture( tex1, gl_FragCoord.xy / vec2( textureSize( tex1, 0 ) ) );
	float rawDepth = dot( packedDepth, vec4( 1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0 ) );
	float sceneDepth = min( rawDepth, 0.9994 );
	sceneDepth = 1.0 / ( sceneDepth * depthScale + depthBias );
	float particleDepth = 1.0 / ( gl_FragCoord.z * depthScale + depthBias );

	// how much of the particle's volume is in front of the scene
	float fade = clamp( ( particleDepth - sceneDepth + uEnv[23].x ) * uEnv[23].y, 0.0, 1.0 );
	// and fade out close to the eye
	fade *= clamp( particleDepth * -uEnv[23].z, 0.0, 1.0 );
	vec4 mask = clamp( vec4( fade ) + uEnv[24], 0.0, 1.0 );

	fragColor = texture( tex0, vTex0.xy ) * mask * vColor;

	if ( uDebug.x > 0.5 ) {
		fragColor = uDebug.x < 1.5 ? vec4( fract( -sceneDepth / 256.0 ), rawDepth, 0.0, 1.0 ) : vec4( fade );
	}
}

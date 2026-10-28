#version 330
// port of glprogs/colorProcess.vfp (fragment part)
in vec4 vTex0;
in vec4 vTex1;
out vec4 fragColor;

uniform sampler2D tex0;	// _currentRender
// 0: scale to the power of two _currentRender, 1: 1 / screen size
uniform vec4 uEnv[2];

void main() {
	vec4 src = texture( tex0, ( gl_FragCoord * uEnv[1] * uEnv[0] ).xy );
	float gray = ( src.x + src.y + src.z ) * 0.33;
	fragColor = vec4( src.xyz * vTex0.xyz + gray * vTex1.xyz, 1.0 );
}

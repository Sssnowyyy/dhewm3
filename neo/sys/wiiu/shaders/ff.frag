#version 330
in vec4 vColor;
in vec4 vTex0;
in vec4 vTex1;
out vec4 fragColor;

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform samplerCube texCube0;
uniform samplerCube texCube1;
// x, y: texture env mode of unit 0, 1 (see gx2_gl.cpp ENV_*), z, w: unit uses a cube map
uniform vec4 uMode;
uniform vec4 uEnvColor0;
uniform vec4 uEnvColor1;

vec4 Combine( vec4 prev, vec4 primary, vec4 t, float mode, vec4 envColor ) {
	if ( mode < 1.5 ) {			// GL_MODULATE
		return prev * t;
	} else if ( mode < 2.5 ) {	// GL_REPLACE
		return t;
	} else if ( mode < 3.5 ) {	// GL_ADD
		return vec4( prev.rgb + t.rgb, prev.a * t.a );
	} else if ( mode < 4.5 ) {	// GL_COMBINE: texture * ( 1 - primary color )
		return vec4( t.rgb * ( vec3( 1.0 ) - primary.rgb ), t.a * prev.a );
	} else if ( mode < 5.5 ) {	// GL_COMBINE: previous * constant color
		return prev * envColor;
	} else {					// GL_DECAL
		return vec4( mix( prev.rgb, t.rgb, t.a ), prev.a );
	}
}

void main() {
	vec4 c = vColor;
	if ( uMode.x > 0.5 ) {
		vec4 t = uMode.z > 0.5 ? texture( texCube0, vTex0.xyz ) : texture( tex0, vTex0.xy / vTex0.w );
		c = Combine( c, vColor, t, uMode.x, uEnvColor0 );
	}
	if ( uMode.y > 0.5 ) {
		vec4 t = uMode.w > 0.5 ? texture( texCube1, vTex1.xyz ) : texture( tex1, vTex1.xy / vTex1.w );
		c = Combine( c, vColor, t, uMode.y, uEnvColor1 );
	}
	fragColor = c;
}

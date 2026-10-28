#version 330
// shadow volumes only touch the stencil buffer
out vec4 fragColor;

void main() {
	fragColor = vec4( 1.0 );
}

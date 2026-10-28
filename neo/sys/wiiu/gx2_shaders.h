// the GLSL shaders from sys/wiiu/shaders/, compiled to GX2 .gsh files at build time

#ifndef __GX2_SHADERS_H__
#define __GX2_SHADERS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *			name;	// "ff" or the ARB program file name without extension
	const unsigned char *	gsh;
	unsigned int			size;
} gx2EmbeddedShader_t;

// terminated by an entry with name == NULL
extern const gx2EmbeddedShader_t gx2EmbeddedShaders[];

#ifdef __cplusplus
}
#endif

#endif

GLSL shaders for the Wii U GX2 renderer, compiled at build time with CafeGLSL
(https://github.com/Exzap/CafeGLSL) and embedded into the .rpx.

ff.*         emulates the OpenGL fixed function pipeline (2 texture units)
others       ports of Doom 3's ARB programs from glprogs/ (and dhewm3's built-in
             soft_particle.vfp), picked by file name
             (interaction.vfp => interaction.*, shadow.vp => shadow.*)

The GX2 GL layer (../gx2_gl.cpp) matches everything by name:
  vertex inputs: aPosition, aColor, aTexCoord, aTexCoord1, aNormal, aAttrib<N>
  uniforms:      uMVP, uModelView, uProjection, uEnv[] / uLocal[] (ARB program
                 env / local parameters), uTexGen<N>, uTexMat<N>, uMode, uEnvColor<N>
  samplers:      tex<N> is GL texture unit N (2D or cube map, by sampler type)

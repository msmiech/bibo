// The OpenGL 3 renderer of RmlUi (Backends folder of the RmlUi sources), compiled with the OpenGL functions loaded by GLEW
// instead of the glad loader bundled with RmlUi. A macro with angle brackets cannot be passed reliably as a compiler
// definition by all CMake generators, so it is defined here.
#define RMLUI_GL3_CUSTOM_LOADER <GL/glew.h>
#include <RmlUi_Renderer_GL3.cpp>

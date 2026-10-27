#ifndef BIBO_RENDER_RENDERUTIL_HPP_
#define BIBO_RENDER_RENDERUTIL_HPP_
#include <GL/glew.h>

namespace bibo {
namespace render {
/**
 * Texture units of the scene (the shaders declare them with layout(binding =
 * ...))
 */
namespace texture_unit {
const int kColor = 0;       // legacy geometries: their texture
const int kShadowMaps = 1;  // cascaded shadow maps (sampler2DArrayShadow)
const int kNormalMap = 2;   // legacy geometries: their normal map
// generated materials (sampler2DArray): color and roughness
const int kMaterialAlbedo = 3;
// generated materials: normal, height and occlusion
const int kMaterialNormal = 4;
const int kEnvironment = 5;  // sky cube map
// top view of the island: height of the ground and of the cave roofs
const int kWorldMap = 6;
}  // namespace texture_unit

/**
 * @return true for a renderer without a graphics card (e.g. Mesa's llvmpipe on
 * servers without GPU): the world is drawn with less detail, so that it is not
 * too slow (e.g. the tests and the recording of the README GIF)
 */
bool IsSoftwareRenderer();

/**
 * Draws a triangle that covers the viewport (for full screen passes with
 * Shader/fullscreen.vert)
 */
class FullscreenTriangle {
 public:
  FullscreenTriangle();
  ~FullscreenTriangle();
  FullscreenTriangle(const FullscreenTriangle&) = delete;
  FullscreenTriangle& operator=(const FullscreenTriangle&) = delete;
  void Draw() const;

 private:
  // the core profile needs one, even without attributes
  GLuint vertex_array_ = 0;
};

/**
 * Saves the OpenGL state that the passes of the renderer change and restores it
 * when it goes out of scope
 */
class StateGuard {
 public:
  StateGuard();
  ~StateGuard();
  StateGuard(const StateGuard&) = delete;
  StateGuard& operator=(const StateGuard&) = delete;

 private:
  GLint framebuffer_ = 0, program_ = 0, viewport_[4] = {}, vertex_array_ = 0,
        active_texture_ = 0;
  GLboolean depth_test_ = GL_FALSE, blend_ = GL_FALSE, cull_face_ = GL_FALSE,
            depth_mask_ = GL_TRUE;
};
}  // namespace render
}  // namespace bibo

#endif  // BIBO_RENDER_RENDERUTIL_HPP_

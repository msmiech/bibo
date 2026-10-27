#ifndef BIBO_WORLD_SKYRENDERER_HPP_
#define BIBO_WORLD_SKYRENDERER_HPP_
#include <GL/glew.h>

#include <memory>

#include "../Render/RenderUtil.hpp"
#include "../Resources/Shader.hpp"

namespace bibo {
namespace world {
/**
 * The sky: a cube map of the sky in all directions (clouds, polar lights), one
 * face is rendered again per frame, so the polar lights and clouds move. Its
 * blurred mip levels light the scene (ambient light, reflections, fog). The
 * background is the cube map and the disk of the sun.
 */
class SkyRenderer {
 public:
  /**
   * @param lowDetail Smaller cube map and fewer samples of the polar lights
   * (software rendering)
   */
  explicit SkyRenderer(bool low_detail);
  ~SkyRenderer();
  SkyRenderer(const SkyRenderer&) = delete;
  SkyRenderer& operator=(const SkyRenderer&) = delete;

  /**
   * Renders the next face of the cube map (all of them the first time). Needs
   * the uniforms of the frame (sun, time).
   */
  void Update();
  /**
   * Binds the cube map to its texture unit (render::texture_unit::kEnvironment)
   */
  void Bind() const;
  /**
   * Draws the sky where nothing was drawn yet (at the far plane)
   */
  void Draw();
  GLuint GetEnvironment() const { return environment_; }

 private:
  void RenderFace(int face);

  std::unique_ptr<Shader> environment_shader_;
  std::unique_ptr<Shader> sky_shader_;
  render::FullscreenTriangle triangle_;
  GLuint environment_ = 0;
  GLuint framebuffer_ = 0;
  int size_;
  int aurora_steps_;
  int next_face_ = 0;
  bool complete_ = false;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_SKYRENDERER_HPP_

#ifndef BIBO_RENDER_RENDERER_HPP_
#define BIBO_RENDER_RENDERER_HPP_
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Resources/Shader.hpp"
#include "FrameUniforms.hpp"
#include "RenderUtil.hpp"
#include "ShadowCascades.hpp"

namespace bibo {
namespace render {
/**
 * Graphics options of a frame (the settings of the game)
 */
struct RenderSettings {
  bool hdr = true;  // tone mapping and auto exposure
  bool bloom = true;
  bool shadows = true;
  bool normal_mapping = true;
  bool transparency = true;
  bool wireframe = false;
};

/**
 * The camera the frame is rendered from
 */
struct CameraView {
  glm::mat4 view = glm::mat4(1.0f);
  glm::mat4 projection = glm::mat4(1.0f);
  glm::vec3 position = glm::vec3(0.0f);
  float field_of_view = 60.0f;  // vertical, degrees
  float aspect_ratio = 1.0f;
  float near_plane = 0.1f;
};

/**
 * What the renderer needs from the scene
 */
class RenderableScene {
 public:
  virtual ~RenderableScene() = default;
  virtual CameraView CameraView() = 0;
  /**
   * Sun, sky, fog, wind, time (the renderer sets the camera, the shadows and
   * the settings)
   */
  virtual void FillFrameUniforms(FrameUniforms& uniforms) = 0;
  /**
   * Before the scene is drawn, after the frame uniforms are uploaded (e.g. the
   * sky cube map)
   */
  virtual void PrepareFrame() = 0;
  /**
   * Everything that casts a shadow, into the shadow map of the cascade (depth
   * only)
   */
  virtual void DrawShadowCasters(const ShadowCascade& cascade) = 0;
  /**
   * Everything, shaded, into the HDR image (the depth buffer is cleared)
   */
  virtual void DrawMain(const RenderSettings& settings) = 0;
};

/**
 * Renders a frame: the cascaded shadow maps of the sun, the scene into an HDR
 * image, then the bloom, the auto exposure (eye adaptation) and the tone
 * mapping into the window
 */
class Renderer {
 public:
  static constexpr int kCascadeCount = 4;

  Renderer(int width, int height);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  /**
   * Size of the window (framebuffer) in pixels
   */
  void Resize(int width, int height);
  /**
   * Renders the scene into the framebuffer that is bound (the window); the HUD
   * and menus are drawn afterwards
   * @param time_delta For the eye adaptation
   */
  void Render(RenderableScene& scene, const RenderSettings& settings,
              float time_delta);
  /**
   * The next frame does not adapt the exposure slowly (e.g. after a jump of the
   * camera)
   */
  void ResetExposure() { exposure_reset_ = true; }
  const std::vector<ShadowCascade>& GetCascades() const { return cascades_; }
  int GetShadowMapSize() const { return shadow_size_; }

 private:
  void CreateTargets();
  void ReleaseTargets();
  void RenderShadows(RenderableScene& scene);
  void PostProcess(const RenderSettings& settings, float time_delta);

  int width_, height_;
  int shadow_size_;
  FrameUniformBuffer frame_uniforms_;
  std::vector<ShadowCascade> cascades_;
  FullscreenTriangle triangle_;

  std::unique_ptr<Shader> bright_pass_shader_, blur_shader_, luminance_shader_,
      exposure_shader_, composite_shader_;
  GLuint shadow_maps_ = 0, shadow_framebuffer_ = 0;
  GLuint hdr_framebuffer_ = 0, hdr_color_ = 0, hdr_depth_ = 0;
  GLuint bloom_framebuffers_[2] = {}, bloom_textures_[2] = {};
  GLuint luminance_framebuffer_ = 0, luminance_texture_ = 0;
  GLuint exposure_framebuffers_[2] = {}, exposure_textures_[2] = {};
  int exposure_index_ = 0;
  bool exposure_reset_ = true;
};
}  // namespace render
}  // namespace bibo

#endif  // BIBO_RENDER_RENDERER_HPP_

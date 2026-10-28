#ifndef BIBO_RENDER_FRAMEUNIFORMS_HPP_
#define BIBO_RENDER_FRAMEUNIFORMS_HPP_
#include <GL/glew.h>

#include <glm/glm.hpp>

namespace bibo {
namespace render {
constexpr int kMaxShadowCascades = 4;
// Animals (and where they just were) that push the plants aside
// (world::FoliageInteraction)
constexpr int kMaxFoliageInteractors = 16;

/**
 * The data of a frame that all shaders of the scene use (uniform buffer, layout
 * std140, see Shader/include/frame.glsl)
 */
struct FrameUniforms {
  glm::mat4 view = glm::mat4(1.0f);
  glm::mat4 projection = glm::mat4(1.0f);
  glm::mat4 view_projection = glm::mat4(1.0f);
  glm::mat4 inverse_view_projection = glm::mat4(1.0f);
  glm::mat4 light_space[kMaxShadowCascades];
  glm::vec4 cascade_splits = glm::vec4(0.0f);
  glm::vec4 cascade_texel_sizes = glm::vec4(0.0f);
  glm::vec4 camera_position_time = glm::vec4(0.0f);
  glm::vec4 sun_direction_shadows = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
  glm::vec4 sun_color_normal_mapping = glm::vec4(1.0f);
  glm::vec4 sky_ambient_snow = glm::vec4(0.0f);
  glm::vec4 ground_ambient = glm::vec4(0.0f);
  glm::vec4 fog_density = glm::vec4(0.0f);
  // xy: direction, z: strength, w: number of foliage interactors
  glm::vec4 wind = glm::vec4(0.0f);
  glm::vec4 viewport = glm::vec4(1.0f);
  glm::vec4 world_map = glm::vec4(0.0f);
  glm::vec4 shadow_settings = glm::vec4(0.0f);
  // xyz: center of the body, w: its radius
  glm::vec4 foliage_interactors[kMaxFoliageInteractors] = {};
  // x: how far the plants are pushed aside (1: fully), yz: motion (xz, 1:
  // running)
  glm::vec4 foliage_pushes[kMaxFoliageInteractors] = {};
};
static_assert(sizeof(FrameUniforms) ==
                  8 * 64 + 12 * 16 + 2 * kMaxFoliageInteractors * 16,
              "FrameUniforms must match the uniform block Frame (std140)");

/**
 * Uniform buffer with the FrameUniforms, bound to binding point 0
 */
class FrameUniformBuffer {
 public:
  static const GLuint kBinding = 0;
  FrameUniformBuffer();
  ~FrameUniformBuffer();
  FrameUniformBuffer(const FrameUniformBuffer&) = delete;
  FrameUniformBuffer& operator=(const FrameUniformBuffer&) = delete;

  void Upload(const FrameUniforms& uniforms);
  void Bind() const;

 private:
  GLuint buffer_ = 0;
};
}  // namespace render
}  // namespace bibo

#endif  // BIBO_RENDER_FRAMEUNIFORMS_HPP_

#ifndef BIBO_SCENE_BEARRENDERER_HPP_
#define BIBO_SCENE_BEARRENDERER_HPP_
#include <GL/glew.h>

#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Resources/Shader.hpp"
#include "../Resources/SkinnedModel.hpp"
#include "Fur.hpp"

namespace bibo {
namespace scene {
/**
 * What the bear needs to be drawn like the rest of the scene (the sun, sky and
 * shadows come from the frame uniforms)
 */
struct BearRenderContext {
  glm::mat4 view = glm::mat4(1.0f);
  glm::mat4 projection = glm::mat4(1.0f);
  glm::vec3 eye = glm::vec3(0.0f);
};

/**
 * Draws the bear: the skinned mesh (skin, eyes, nose) and the fur strands. The
 * vertices are skinned on the CPU (BearCharacter) and uploaded once per frame;
 * the strands are generated on the GPU from them (bear_fur.vert), so there is
 * no geometry per strand in memory, only where it grows (Fur.hpp,
 * GenerateStrands). Far away fewer, wider strands are drawn (level of detail).
 */
class BearRenderer {
 public:
  static constexpr int kStrandSegments = 4;
  // Width of a strand at its root (world units)
  static constexpr float kStrandWidth = 0.0075f;

  BearRenderer(const resources::SkinnedModel& model, std::size_t strand_count);
  ~BearRenderer();
  BearRenderer(const BearRenderer&) = delete;
  BearRenderer& operator=(const BearRenderer&) = delete;

  /**
   * @param fur_offsets Bend of the guide hairs (fur::GuideHairs)
   */
  void Upload(const std::vector<glm::vec3>& positions,
              const std::vector<glm::vec3>& normals,
              const std::vector<glm::vec3>& fur_offsets,
              const std::vector<glm::vec3>& comb_directions);
  /**
   * Only the surface (e.g. into the shadow map), the shader needs a "position"
   * attribute at location 0
   */
  void DrawSurface(Shader* shader);
  /**
   * Skin, eyes and fur with lighting and shadows
   * @param center Center of the bear (for the level of detail)
   */
  void DrawShaded(const BearRenderContext& context, glm::vec3 center);
  /**
   * Length of the longest fur (world units), e.g. longer for a bigger bear
   * (default: fur::kMaxLength)
   */
  void SetMaxFurLength(float length) { max_fur_length_ = length; }

  std::size_t GetStrandCount() const { return strand_count_; }
  /**
   * Strands drawn by the last DrawShaded (fewer when the bear is far away)
   */
  std::size_t GetDrawnStrandCount() const { return drawn_strand_count_; }

 private:
  std::unique_ptr<Shader> skin_shader_;
  std::unique_ptr<Shader> fur_shader_;
  GLuint vertex_array_ = 0;
  // no attributes, but core profile needs a bound vertex array
  GLuint strand_vertex_array_ = 0;
  // per vertex: position, normal, fur bend, comb direction (vec4 each)
  GLuint skin_buffer_ = 0;
  GLuint color_buffer_ = 0;
  GLuint surface_buffer_ = 0;
  GLuint index_buffer_ = 0;
  GLuint strand_buffer_ = 0;
  GLsizei index_count_ = 0;
  std::size_t vertex_count_ = 0;
  std::size_t strand_count_ = 0;
  std::size_t drawn_strand_count_ = 0;
  float max_fur_length_ = fur::kMaxLength;
  std::vector<glm::vec4> staging_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEARRENDERER_HPP_

#ifndef BIBO_SCENE_FURRYRENDERER_HPP_
#define BIBO_SCENE_FURRYRENDERER_HPP_
#include <GL/glew.h>

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Resources/Shader.hpp"
#include "BearRenderer.hpp"
#include "FurryModel.hpp"
#include "ViewFrustum.hpp"

namespace bibo {
namespace scene {
/**
 * Draws animals made of rigid parts (FurryModel) like the bear (the shaders of
 * the BearRenderer): the skin with the eyes and the nose, the fur and the
 * spines (stiff, banded strands). The vertices of all animals are moved on the
 * CPU when they are drawn (as SetPose placed them last) and uploaded once per
 * frame, the strands are generated on the GPU. Far away fewer, wider strands
 * are drawn. Needs an OpenGL context.
 */
class FurryRenderer {
 public:
  struct Strands {
    std::size_t fur_count = 0;
    std::size_t spine_count = 0;
    // of the longest spines (world units), the fur is as long as the fur of the
    // bear
    float spine_length = 0.0f;
    float spine_width = 0.0f;
    std::uint32_t fur_seed = 1;
    std::uint32_t spine_seed = 2;
  };
  /**
   * Sphere around an animal in the space of its first part (for view frustum
   * culling and the level of detail)
   */
  struct Bounds {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 1.0f;
  };

  /**
   * @param count Number of animals
   */
  FurryRenderer(FurryModel model, std::size_t count, const Strands& strands,
                const Bounds& bounds);
  virtual ~FurryRenderer();
  FurryRenderer(const FurryRenderer&) = delete;
  FurryRenderer& operator=(const FurryRenderer&) = delete;

  /**
   * Where an animal is in this frame
   * @param parts Model matrix of every part (with the scale), indexed by
   * FurryModel::parts
   * @param bristle 0 to 1: the spines stand up
   */
  void SetPose(std::size_t index, const std::vector<glm::mat4>& parts,
               float bristle = 0.0f);
  /**
   * Only the surfaces (e.g. into the shadow map), the shader needs a "position"
   * attribute at location 0
   */
  void DrawSurfaces(Shader* shader);
  /**
   * Skin, eyes, fur and spines with lighting and shadows (the animals outside
   * of the frustum are left out)
   */
  void DrawShaded(const BearRenderContext& context, const ViewFrustum& frustum);
  /**
   * @return Number of animals drawn by the last DrawShaded
   */
  int GetDrawnCount() const { return drawn_count_; }
  const FurryModel& GetModel() const { return model_; }

 private:
  // The pose an animal was given and whether its vertices have been moved there
  // yet
  struct Pose {
    std::vector<glm::mat4> parts;
    float bristle = 0.0f;
    bool moved = true;
  };

  void MoveVertices(std::size_t index);
  void Upload();
  void BindVertices(std::size_t index);

  FurryModel model_;
  std::size_t count_;
  std::size_t vertex_count_;
  Strands strands_;
  Bounds bounds_;
  // bytes of the vertices of one animal in the skin buffer (aligned for the
  // shader storage buffer)
  GLsizeiptr block_size_;
  std::unique_ptr<Shader> skin_shader_;
  std::unique_ptr<Shader> fur_shader_;
  GLuint vertex_array_ = 0, strand_vertex_array_ = 0;
  GLuint skin_buffer_ = 0, color_buffer_ = 0, surface_buffer_ = 0,
         index_buffer_ = 0;
  GLuint spine_buffer_ = 0, fur_buffer_ = 0;
  std::size_t spine_count_ = 0, fur_count_ = 0;
  GLsizei index_count_ = 0;
  // position, normal, bend, comb per vertex, a block per animal
  std::vector<glm::vec4> staging_;
  std::vector<Pose> poses_;
  std::vector<glm::vec3> centers_;
  std::vector<float> radii_;
  bool uploaded_ = false;
  int drawn_count_ = 0;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_FURRYRENDERER_HPP_

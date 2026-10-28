#ifndef BIBO_SCENE_CREATURERENDERER_HPP_
#define BIBO_SCENE_CREATURERENDERER_HPP_
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "../Render/ShadowCascades.hpp"
#include "../Resources/Shader.hpp"
#include "../World/Vegetation.hpp"
#include "CreatureMeshes.hpp"

namespace bibo {
namespace scene {
/**
 * Draws the parts of the animals (salmon, splashes) where the game put them in
 * this frame: every model with one instanced draw call, shaded like the
 * vegetation (the shaders of the VegetationRenderer, without wind). Needs an
 * OpenGL context.
 */
class CreatureRenderer {
 public:
  CreatureRenderer();
  ~CreatureRenderer();
  CreatureRenderer(const CreatureRenderer&) = delete;
  CreatureRenderer& operator=(const CreatureRenderer&) = delete;

  /**
   * Removes the parts of the last frame
   */
  void Clear();
  /**
   * @param position Of the origin of the model, rotation: model to world,
   * scale: uniform
   */
  void Add(CreatureModel model, glm::vec3 position, glm::quat rotation,
           float scale);
  /**
   * The materials of the world must be bound (World::DrawOpaque)
   */
  void Draw();
  void DrawShadow(const render::ShadowCascade& cascade);
  std::size_t GetPartCount() const;

 private:
  struct GpuMesh {
    GLuint vertex_array = 0, vertex_buffer = 0, index_buffer = 0;
    GLsizei index_count = 0;
    float radius = 0.0f;
  };
  void Upload();
  void DrawBatches(Shader& program);

  std::unique_ptr<Shader> shader_;
  std::unique_ptr<Shader> depth_shader_;
  std::vector<GpuMesh> meshes_;
  std::vector<std::vector<world::Instance>> parts_;  // per model
  std::vector<world::Instance> staging_;
  std::vector<std::size_t> firsts_;  // of the models in staging
  GLuint instance_buffer_ = 0;
  bool uploaded_ = false;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_CREATURERENDERER_HPP_

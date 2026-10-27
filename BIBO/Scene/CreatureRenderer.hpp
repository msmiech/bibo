#pragma once
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
  void clear();
  /**
   * @param position Of the origin of the model, rotation: model to world,
   * scale: uniform
   */
  void add(CreatureModel model, glm::vec3 position, glm::quat rotation,
           float scale);
  /**
   * The materials of the world must be bound (World::drawOpaque)
   */
  void draw();
  void drawShadow(const render::ShadowCascade& cascade);
  std::size_t getPartCount() const;

 private:
  struct GpuMesh {
    GLuint vertexArray = 0, vertexBuffer = 0, indexBuffer = 0;
    GLsizei indexCount = 0;
    float radius = 0.0f;
  };
  void upload();
  void drawBatches(Shader& program);

  std::unique_ptr<Shader> shader;
  std::unique_ptr<Shader> depthShader;
  std::vector<GpuMesh> meshes;
  std::vector<std::vector<world::Instance>> parts;  // per model
  std::vector<world::Instance> staging;
  std::vector<std::size_t> firsts;  // of the models in staging
  GLuint instanceBuffer = 0;
  bool uploaded = false;
};
}  // namespace scene
}  // namespace bibo

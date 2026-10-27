#pragma once
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
 * CPU when they are drawn (as setPose placed them last) and uploaded once per
 * frame, the strands are generated on the GPU. Far away fewer, wider strands
 * are drawn. Needs an OpenGL context.
 */
class FurryRenderer {
 public:
  struct Strands {
    std::size_t furCount = 0;
    std::size_t spineCount = 0;
    float spineLength = 0.0f;  // of the longest spines (world units), the fur
                               // is as long as the fur of the bear
    float spineWidth = 0.0f;
    std::uint32_t furSeed = 1;
    std::uint32_t spineSeed = 2;
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
  void setPose(std::size_t index, const std::vector<glm::mat4>& parts,
               float bristle = 0.0f);
  /**
   * Only the surfaces (e.g. into the shadow map), the shader needs a "position"
   * attribute at location 0
   */
  void drawSurfaces(Shader* shader);
  /**
   * Skin, eyes, fur and spines with lighting and shadows (the animals outside
   * of the frustum are left out)
   */
  void drawShaded(const BearRenderContext& context, const ViewFrustum& frustum);
  /**
   * @return Number of animals drawn by the last drawShaded
   */
  int getDrawnCount() const { return drawnCount; }
  const FurryModel& getModel() const { return model; }

 private:
  // The pose an animal was given and whether its vertices have been moved there
  // yet
  struct Pose {
    std::vector<glm::mat4> parts;
    float bristle = 0.0f;
    bool moved = true;
  };

  void moveVertices(std::size_t index);
  void upload();
  void bindVertices(std::size_t index);

  FurryModel model;
  std::size_t count;
  std::size_t vertexCount;
  Strands strands;
  Bounds bounds;
  GLsizeiptr blockSize;  // bytes of the vertices of one animal in the skin
                         // buffer (aligned for the shader storage buffer)
  std::unique_ptr<Shader> skinShader;
  std::unique_ptr<Shader> furShader;
  GLuint vertexArray = 0, strandVertexArray = 0;
  GLuint skinBuffer = 0, colorBuffer = 0, surfaceBuffer = 0, indexBuffer = 0;
  GLuint spineBuffer = 0, furBuffer = 0;
  std::size_t spineCount = 0, furCount = 0;
  GLsizei indexCount = 0;
  std::vector<glm::vec4>
      staging;  // position, normal, bend, comb per vertex, a block per animal
  std::vector<Pose> poses;
  std::vector<glm::vec3> centers;
  std::vector<float> radii;
  bool uploaded = false;
  int drawnCount = 0;
};
}  // namespace scene
}  // namespace bibo

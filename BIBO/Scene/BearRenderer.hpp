#pragma once
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
 * generateStrands). Far away fewer, wider strands are drawn (level of detail).
 */
class BearRenderer {
 public:
  static constexpr int STRAND_SEGMENTS = 4;
  // Width of a strand at its root (world units)
  static constexpr float STRAND_WIDTH = 0.0075f;

  BearRenderer(const resources::SkinnedModel& model, std::size_t strandCount);
  ~BearRenderer();
  BearRenderer(const BearRenderer&) = delete;
  BearRenderer& operator=(const BearRenderer&) = delete;

  /**
   * @param furOffsets Bend of the guide hairs (fur::GuideHairs)
   */
  void upload(const std::vector<glm::vec3>& positions,
              const std::vector<glm::vec3>& normals,
              const std::vector<glm::vec3>& furOffsets,
              const std::vector<glm::vec3>& combDirections);
  /**
   * Only the surface (e.g. into the shadow map), the shader needs a "position"
   * attribute at location 0
   */
  void drawSurface(Shader* shader);
  /**
   * Skin, eyes and fur with lighting and shadows
   * @param center Center of the bear (for the level of detail)
   */
  void drawShaded(const BearRenderContext& context, glm::vec3 center);
  /**
   * Length of the longest fur (world units), e.g. longer for a bigger bear
   * (default: fur::MAX_LENGTH)
   */
  void setMaxFurLength(float length) { maxFurLength = length; }

  std::size_t getStrandCount() const { return strandCount; }
  /**
   * Strands drawn by the last drawShaded (fewer when the bear is far away)
   */
  std::size_t getDrawnStrandCount() const { return drawnStrandCount; }

 private:
  std::unique_ptr<Shader> skinShader;
  std::unique_ptr<Shader> furShader;
  GLuint vertexArray = 0;
  GLuint strandVertexArray =
      0;  // no attributes, but core profile needs a bound vertex array
  GLuint skinBuffer =
      0;  // per vertex: position, normal, fur bend, comb direction (vec4 each)
  GLuint colorBuffer = 0;
  GLuint surfaceBuffer = 0;
  GLuint indexBuffer = 0;
  GLuint strandBuffer = 0;
  GLsizei indexCount = 0;
  std::size_t vertexCount = 0;
  std::size_t strandCount = 0;
  std::size_t drawnStrandCount = 0;
  float maxFurLength = fur::MAX_LENGTH;
  std::vector<glm::vec4> staging;
};
}  // namespace scene
}  // namespace bibo

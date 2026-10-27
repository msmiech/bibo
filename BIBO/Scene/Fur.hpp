#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace bibo {
namespace fur {
/**
 * Length of the fur (world units) where the fur length of a vertex is 1 (alpha
 * of its color)
 */
const float MAX_LENGTH = 0.13f;

struct SphereCollider {
  glm::vec3 center;
  float radius;
};

/**
 * What the fur collides with and which forces act on it
 */
struct Environment {
  glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
  // acceleration of the fur tips (fur lengths / s^2), varies with gusts
  glm::vec3 wind = glm::vec3(0.0f);
  float time = 0.0f;  // seconds, for the gusts of the wind
  // the fur lies on the ground plane instead of going through it
  bool hasGround = false;
  glm::vec3 groundPoint = glm::vec3(0.0f);
  glm::vec3 groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
  std::vector<SphereCollider> spheres;  // e.g. rocks
};

/**
 * Dynamics of the fur: every vertex of the skin has a guide hair whose tip is a
 * damped spring. It lags behind when the body accelerates (running, stopping,
 * turning, tumbling), hangs down a bit, moves in the wind, lies on the ground
 * and on obstacles, and is ruffled by hits. The rendered strands interpolate
 * the guide hairs of the triangle they grow on (BearRenderer).
 *
 * The bend of a guide hair is its offset: the tip is at position + (normal +
 * offset) * fur length, so the offset is measured in fur lengths and in world
 * space.
 */
class GuideHairs {
 public:
  /**
   * Straightens all hairs and forgets the motion (e.g. after a restart)
   */
  void reset(const std::vector<glm::vec3>& positions);
  /**
   * @param positions Current world positions of the vertices
   * @param normals Current world normals of the vertices
   * @param lengths Fur length of every vertex (world units), 0 where there is
   * no fur
   */
  void update(float timeDelta, const std::vector<glm::vec3>& positions,
              const std::vector<glm::vec3>& normals,
              const std::vector<float>& lengths,
              const Environment& environment);
  /**
   * Pushes the hairs around a point at the next update (e.g. where the bear hit
   * a rock)
   * @param velocity Direction and speed of the push (world units / s)
   */
  void addImpulse(glm::vec3 position, glm::vec3 velocity, float radius);
  /**
   * Bend of every guide hair (world space, in fur lengths)
   */
  const std::vector<glm::vec3>& getOffsets() const { return offsets; }

 private:
  struct Impulse {
    glm::vec3 position;
    glm::vec3 velocity;
    float radius;
  };

  std::vector<glm::vec3> previousPositions;
  std::vector<glm::vec3> vertexVelocities;
  std::vector<glm::vec3> offsets;
  std::vector<glm::vec3> offsetVelocities;
  std::vector<Impulse> impulses;
};

/**
 * Root of a rendered strand on a triangle of the skin. The layout matches the
 * shader storage buffer of bear_fur.vert (std430).
 */
struct Strand {
  std::uint32_t vertices[3];  // the vertices of the triangle
  std::uint32_t seed;         // random number for the variations of the strand
  // weights of the first two vertices (the third one has the rest)
  float barycentric[2];
  // the strand is a bit shorter or longer than the fur of the triangle
  float lengthScale;
  float shade;  // brightness variation
};

/**
 * Places strands on the triangles with fur, more on larger triangles and where
 * the fur is longer. The strands are in random order, so the first n strands
 * are a sparser fur on the whole body (level of detail).
 * @param colors Vertex colors, alpha is the fur length
 * @param triangles 3 vertex indices per triangle
 */
std::vector<Strand> generateStrands(const std::vector<glm::vec3>& positions,
                                    const std::vector<glm::vec4>& colors,
                                    const std::vector<unsigned int>& triangles,
                                    std::size_t count, std::uint32_t seed);

/**
 * Direction the fur is combed in at every vertex (tangent to the surface):
 * backwards and down, like an animal's fur
 * @param forward Front of the model
 */
std::vector<glm::vec3> combDirections(const std::vector<glm::vec3>& normals,
                                      glm::vec3 forward, glm::vec3 up);
}  // namespace fur
}  // namespace bibo

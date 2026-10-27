#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace bibo {
namespace animation {
/**
 * Translation, rotation and scale of a bone relative to its parent
 */
struct Transform {
  glm::vec3 translation = glm::vec3(0.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 scale = glm::vec3(1.0f);

  glm::mat4 toMatrix() const;
  /**
   * @param matrix Translation, rotation and (positive) scale, no shear
   */
  static Transform fromMatrix(const glm::mat4& matrix);
  /**
   * Translation and scale are interpolated linearly, the rotation along the
   * shortest arc
   */
  static Transform mix(const Transform& a, const Transform& b, float t);
};

/**
 * Local transforms of all bones of a skeleton
 */
using Pose = std::vector<Transform>;

struct Bone {
  std::string name;
  int parent = -1;      // index of the parent bone, -1 for the root bone
  Transform bindLocal;  // relative to the parent in the bind pose (the pose the
                        // model was skinned in)
  glm::mat4 inverseBind = glm::mat4(
      1.0f);  // from model space into the space of the bone in the bind pose
};

class Skeleton {
 public:
  std::vector<Bone> bones;  // parents come before their children
  glm::mat4 rootParent =
      glm::mat4(1.0f);  // model space transform of the parent of the root bone
                        // (e.g. the armature)

  /**
   * @return Index of the bone or -1
   */
  int find(const std::string& name) const;
  Pose bindPose() const;
  /**
   * Model space transforms of all bones (forward kinematics)
   * @param base Transform of the model, e.g. its world transform
   */
  void modelMatrices(const Pose& pose, std::vector<glm::mat4>& result,
                     const glm::mat4& base = glm::mat4(1.0f)) const;
  /**
   * The pose whose bones have the given transforms
   * @param matrices Transforms of all bones in the space of base
   */
  Pose localPose(const std::vector<glm::mat4>& matrices,
                 const glm::mat4& base = glm::mat4(1.0f)) const;
  /**
   * Matrices for skinning: from the bind pose into the current pose
   * @param matrices Current transforms of all bones (e.g. modelMatrices)
   */
  void skinMatrices(const std::vector<glm::mat4>& matrices,
                    std::vector<glm::mat4>& result) const;
  /**
   * Computes inverseBind of all bones from the bind pose
   */
  void computeInverseBind();
};

/**
 * Shortest rotation that turns one direction into another (the directions do
 * not need to be normalized)
 */
glm::quat rotationBetween(glm::vec3 from, glm::vec3 to);

/**
 * Interpolation factor for smoothing a value towards a target independent of
 * the frame rate (value += (target - value) * smoothingFactor(...))
 * @param halfLife Time after which half of the distance to the target is
 * covered
 */
float smoothingFactor(float halfLife, float timeDelta);

/**
 * Damped spring that pulls a point towards a target (semi-implicit Euler,
 * stable for stiffness * dt^2 < 1)
 */
struct SpringPoint {
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 velocity = glm::vec3(0.0f);

  /**
   * @param stiffness Spring constant per unit mass (1/s^2)
   * @param damping Velocity damping (1/s)
   * @param acceleration Additional acceleration (e.g. gravity)
   */
  void update(glm::vec3 target, float stiffness, float damping,
              glm::vec3 acceleration, float timeDelta);
  void reset(glm::vec3 to);
};

/**
 * Damped spring for a single value (e.g. how far the body is squashed after a
 * landing)
 */
struct SpringValue {
  float value = 0.0f;
  float velocity = 0.0f;

  void update(float target, float stiffness, float damping, float timeDelta);
};
}  // namespace animation
}  // namespace bibo

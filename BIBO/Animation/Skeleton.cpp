#include "Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using namespace bibo::animation;

glm::mat4 Transform::toMatrix() const {
  glm::mat4 matrix = glm::mat4_cast(rotation);
  matrix[0] *= scale.x;
  matrix[1] *= scale.y;
  matrix[2] *= scale.z;
  matrix[3] = glm::vec4(translation, 1.0f);
  return matrix;
}

Transform Transform::fromMatrix(const glm::mat4& matrix) {
  Transform transform;
  transform.translation = glm::vec3(matrix[3]);
  transform.scale = glm::vec3(glm::length(glm::vec3(matrix[0])),
                              glm::length(glm::vec3(matrix[1])),
                              glm::length(glm::vec3(matrix[2])));
  glm::mat3 rotation(glm::vec3(matrix[0]) / transform.scale.x,
                     glm::vec3(matrix[1]) / transform.scale.y,
                     glm::vec3(matrix[2]) / transform.scale.z);
  transform.rotation = glm::normalize(glm::quat_cast(rotation));
  return transform;
}

Transform Transform::mix(const Transform& a, const Transform& b, float t) {
  Transform result;
  result.translation = glm::mix(a.translation, b.translation, t);
  result.rotation = glm::slerp(a.rotation, b.rotation,
                               t);  // glm::slerp takes the shortest arc
  result.scale = glm::mix(a.scale, b.scale, t);
  return result;
}

int Skeleton::find(const std::string& name) const {
  for (std::size_t i = 0; i < bones.size(); ++i) {
    if (bones[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

Pose Skeleton::bindPose() const {
  Pose pose;
  pose.reserve(bones.size());
  for (const Bone& bone : bones) {
    pose.push_back(bone.bindLocal);
  }
  return pose;
}

void Skeleton::modelMatrices(const Pose& pose, std::vector<glm::mat4>& result,
                             const glm::mat4& base) const {
  result.resize(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const glm::mat4& parent =
        bones[i].parent >= 0 ? result[bones[i].parent] : base * rootParent;
    result[i] = parent * pose[i].toMatrix();
  }
}

Pose Skeleton::localPose(const std::vector<glm::mat4>& matrices,
                         const glm::mat4& base) const {
  Pose pose(bones.size());
  glm::mat4 rootSpace = glm::inverse(base * rootParent);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    glm::mat4 parentInverse = bones[i].parent >= 0
                                  ? glm::inverse(matrices[bones[i].parent])
                                  : rootSpace;
    pose[i] = Transform::fromMatrix(parentInverse * matrices[i]);
  }
  return pose;
}

void Skeleton::skinMatrices(const std::vector<glm::mat4>& matrices,
                            std::vector<glm::mat4>& result) const {
  result.resize(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    result[i] = matrices[i] * bones[i].inverseBind;
  }
}

void Skeleton::computeInverseBind() {
  std::vector<glm::mat4> bind;
  modelMatrices(bindPose(), bind);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    bones[i].inverseBind = glm::inverse(bind[i]);
  }
}

glm::quat bibo::animation::rotationBetween(glm::vec3 from, glm::vec3 to) {
  float fromLength = glm::length(from), toLength = glm::length(to);
  if (fromLength < 1e-8f || toLength < 1e-8f) {
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }
  from /= fromLength;
  to /= toLength;
  float cosine = glm::dot(from, to);
  if (cosine < -0.99999f) {
    // Opposite directions: half a turn around any perpendicular axis
    glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
    if (glm::dot(axis, axis) < 1e-6f) {
      axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), from);
    }
    return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
  }
  glm::vec3 axis = glm::cross(from, to);
  return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

float bibo::animation::smoothingFactor(float halfLife, float timeDelta) {
  if (halfLife <= 0.0f) {
    return 1.0f;
  }
  return 1.0f - std::exp2(-timeDelta / halfLife);
}

void SpringPoint::update(glm::vec3 target, float stiffness, float damping,
                         glm::vec3 acceleration, float timeDelta) {
  // Substeps keep stiff springs stable when a frame takes long
  int steps = std::max(
      1, static_cast<int>(std::ceil(timeDelta * std::sqrt(stiffness) / 0.5f)));
  float step = timeDelta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    velocity +=
        (stiffness * (target - position) - damping * velocity + acceleration) *
        step;
    position += velocity * step;
  }
}

void SpringPoint::reset(glm::vec3 to) {
  position = to;
  velocity = glm::vec3(0.0f);
}

void SpringValue::update(float target, float stiffness, float damping,
                         float timeDelta) {
  int steps = std::max(
      1, static_cast<int>(std::ceil(timeDelta * std::sqrt(stiffness) / 0.5f)));
  float step = timeDelta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    velocity += (stiffness * (target - value) - damping * velocity) * step;
    value += velocity * step;
  }
}

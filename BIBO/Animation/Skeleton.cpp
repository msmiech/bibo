#include "Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace bibo::animation {

glm::mat4 Transform::ToMatrix() const {
  glm::mat4 matrix = glm::mat4_cast(rotation);
  matrix[0] *= scale.x;
  matrix[1] *= scale.y;
  matrix[2] *= scale.z;
  matrix[3] = glm::vec4(translation, 1.0f);
  return matrix;
}

Transform Transform::FromMatrix(const glm::mat4& matrix) {
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

Transform Transform::Mix(const Transform& a, const Transform& b, float t) {
  Transform result;
  result.translation = glm::mix(a.translation, b.translation, t);
  result.rotation = glm::slerp(a.rotation, b.rotation,
                               t);  // glm::slerp takes the shortest arc
  result.scale = glm::mix(a.scale, b.scale, t);
  return result;
}

int Skeleton::Find(const std::string& name) const {
  for (std::size_t i = 0; i < bones.size(); ++i) {
    if (bones[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

Pose Skeleton::BindPose() const {
  Pose pose;
  pose.reserve(bones.size());
  for (const Bone& bone : bones) {
    pose.push_back(bone.bind_local);
  }
  return pose;
}

void Skeleton::ModelMatrices(const Pose& pose, std::vector<glm::mat4>& result,
                             const glm::mat4& base) const {
  result.resize(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const glm::mat4& parent =
        bones[i].parent >= 0 ? result[bones[i].parent] : base * root_parent;
    result[i] = parent * pose[i].ToMatrix();
  }
}

Pose Skeleton::LocalPose(const std::vector<glm::mat4>& matrices,
                         const glm::mat4& base) const {
  Pose pose(bones.size());
  glm::mat4 root_space = glm::inverse(base * root_parent);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    glm::mat4 parent_inverse = bones[i].parent >= 0
                                   ? glm::inverse(matrices[bones[i].parent])
                                   : root_space;
    pose[i] = Transform::FromMatrix(parent_inverse * matrices[i]);
  }
  return pose;
}

void Skeleton::SkinMatrices(const std::vector<glm::mat4>& matrices,
                            std::vector<glm::mat4>& result) const {
  result.resize(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    result[i] = matrices[i] * bones[i].inverse_bind;
  }
}

void Skeleton::ComputeInverseBind() {
  std::vector<glm::mat4> bind;
  ModelMatrices(BindPose(), bind);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    bones[i].inverse_bind = glm::inverse(bind[i]);
  }
}

glm::quat RotationBetween(glm::vec3 from, glm::vec3 to) {
  float from_length = glm::length(from), to_length = glm::length(to);
  if (from_length < 1e-8f || to_length < 1e-8f) {
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }
  from /= from_length;
  to /= to_length;
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

float SmoothingFactor(float half_life, float time_delta) {
  if (half_life <= 0.0f) {
    return 1.0f;
  }
  return 1.0f - std::exp2(-time_delta / half_life);
}

void SpringPoint::Update(glm::vec3 target, float stiffness, float damping,
                         glm::vec3 acceleration, float time_delta) {
  // Substeps keep stiff springs stable when a frame takes long
  int steps = std::max(
      1, static_cast<int>(std::ceil(time_delta * std::sqrt(stiffness) / 0.5f)));
  float step = time_delta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    velocity +=
        (stiffness * (target - position) - damping * velocity + acceleration) *
        step;
    position += velocity * step;
  }
}

void SpringPoint::Reset(glm::vec3 to) {
  position = to;
  velocity = glm::vec3(0.0f);
}

void SpringValue::Update(float target, float stiffness, float damping,
                         float time_delta) {
  int steps = std::max(
      1, static_cast<int>(std::ceil(time_delta * std::sqrt(stiffness) / 0.5f)));
  float step = time_delta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    velocity += (stiffness * (target - value) - damping * velocity) * step;
    value += velocity * step;
  }
}

}  // namespace bibo::animation

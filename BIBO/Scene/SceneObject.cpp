#include "SceneObject.hpp"

#include <cmath>

namespace bibo::scene {

using glm::mat4;
using glm::vec3;
using glm::vec4;

SceneObject::SceneObject(GLFWwindow& window)
    : model_matrix(mat4(1.0f)), window_(&window) {}
SceneObject::SceneObject(mat4 model_matrix, GLFWwindow& window)
    : model_matrix(model_matrix), window_(&window) {}

vec3 SceneObject::GetPosition() { return vec3(model_matrix[3]); }

void SceneObject::SetPosition(vec3 position) {
  model_matrix[3] = vec4(position, model_matrix[3].w);
}

/**
 * Based on
 * https://www.opengl.org/discussion_boards/showthread.php/159215-Is-it-possible-to-extract-rotation-translation-scale-given-a-matrix
 */
vec3 SceneObject::GetDirection() {
  float sin_pitch, cos_pitch, sin_roll, cos_roll, sin_yaw, cos_yaw;

  sin_pitch = -model_matrix[2][0];
  cos_pitch = sqrt(1 - sin_pitch * sin_pitch);

  if (std::abs(cos_pitch) > 0.00001f) {
    sin_roll = model_matrix[2][1] / cos_pitch;
    cos_roll = model_matrix[2][2] / cos_pitch;
    sin_yaw = model_matrix[1][0] / cos_pitch;
    cos_yaw = model_matrix[0][0] / cos_pitch;
  } else {
    sin_roll = -model_matrix[1][2];
    cos_roll = model_matrix[1][1];
    sin_yaw = 0;
    cos_yaw = 1;
  }

  float yaw = atan2(sin_yaw, cos_yaw);
  float pitch = atan2(sin_pitch, cos_pitch);
  float roll = atan2(sin_roll, cos_roll);
  return vec3(yaw, pitch, roll);
}

}  // namespace bibo::scene

#ifndef BIBO_SCENE_SCENEOBJECT_HPP_
#define BIBO_SCENE_SCENEOBJECT_HPP_

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../Resources/Shader.hpp"

namespace bibo {
namespace scene {
class SceneObject {
 public:
  SceneObject(GLFWwindow& window);
  SceneObject(glm::mat4 model_matrix, GLFWwindow& window);

  virtual ~SceneObject() = default;

  virtual void Update(float time_delta) = 0;
  virtual void Draw(Shader* shader) = 0;

  virtual glm::vec3 GetPosition();
  virtual void SetPosition(glm::vec3 position);
  // Here the Euler angles of the model matrix (yaw, pitch, roll in radians),
  // the bear and the camera return the direction they face
  virtual glm::vec3 GetDirection();
  glm::mat4 model_matrix;

 protected:
  GLFWwindow* window_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_SCENEOBJECT_HPP_

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../Resources/Shader.hpp"

using namespace glm;

namespace bibo {
namespace scene {
class SceneObject {
 public:
  SceneObject(GLFWwindow& window);
  SceneObject(mat4 modelMatrix, GLFWwindow& window);

  virtual ~SceneObject();

  virtual void update(float time_delta) = 0;
  virtual void draw(Shader* shader) = 0;

  virtual vec3 getPosition();
  virtual void setPosition(vec3 position);
  virtual vec3 getDirection();
  mat4 modelMatrix;

 protected:
  GLFWwindow* window;
};
}  // namespace scene
}  // namespace bibo

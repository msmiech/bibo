#ifndef BIBO_RESOURCES_MATERIAL_HPP_
#define BIBO_RESOURCES_MATERIAL_HPP_
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bibo {
namespace resources {
struct Material {  // with default values
  glm::vec3 ambient = glm::vec3(0.6f, 0.6f, 0.6f);
  glm::vec3 diffuse = glm::vec3(0.7f, 0.8f, 0.6f);
  glm::vec3 specular = glm::vec3(0.8f, 0.75f, 0.8f);
  float shininess = 32.0f;
};
}  // namespace resources
}  // namespace bibo

#endif  // BIBO_RESOURCES_MATERIAL_HPP_

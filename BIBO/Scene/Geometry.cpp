#include "Geometry.hpp"

#include <algorithm>

#include "../Util/Console.hpp"

namespace bibo::scene {

using glm::mat4;
using glm::vec3;
using glm::vec4;
using resources::Model;
using std::make_unique;
using std::string;

Geometry::Geometry(mat4 model_matrix, Shader* shader, GLFWwindow& window,
                   const aiScene* assimp_scene, const char* node_name,
                   Texture* texture, Texture* normal_map_texture)
    : SceneObject(model_matrix, window),
      model_(make_unique<Model>(shader, assimp_scene, node_name, texture)),
      normal_map(normal_map_texture) {
  has_normal_map = normal_map_texture != nullptr;
  RequireMeshes(node_name);
  this->model_matrix = model_->GetTranformationMat() * model_matrix;
}

Geometry::Geometry(mat4 model_matrix, Shader* shader, GLFWwindow& window,
                   const char* file_path, const char* node_name,
                   Texture* texture)
    : Geometry(model_matrix, shader, window, file_path, node_name, texture,
               nullptr) {}

Geometry::Geometry(mat4 model_matrix, Shader* shader, GLFWwindow& window,
                   const char* file_path, const char* node_name,
                   Texture* texture, Texture* normal_map_texture)
    : SceneObject(model_matrix, window),
      model_(make_unique<Model>(shader, file_path, node_name, texture)),
      normal_map(normal_map_texture) {
  has_normal_map = normal_map_texture != nullptr;
  RequireMeshes(node_name);
  this->model_matrix = model_->GetTranformationMat() * model_matrix;
}

Geometry::Geometry(mat4 model_matrix, GLFWwindow& window)
    : SceneObject(model_matrix, window),
      normal_map(nullptr),
      has_normal_map(false) {}

void Geometry::RequireMeshes(const char* node_name) {
  if (model_->meshes.empty()) {
    util::FatalError(string("Could not load the node ") +
                     (node_name ? node_name : "(root)") + " of the model");
  }
}

Geometry::~Geometry() {}

void Geometry::Update(float time_delta) {
  if (!physics_world_ || physics_body_ == physics::kInvalidBody) return;
  model_matrix = physics_world_->GetTransform(physics_body_) * body_to_model_;
}

void Geometry::Draw(Shader* shader) {
  GLuint model_location = shader->GetUniformLocation("model");
  if (model_location != 0xFFFFFFFF) {
    glUniformMatrix4fv(model_location, 1, GL_FALSE, value_ptr(model_matrix));
  }
  GLint layer_location = shader->GetUniformLocation("materialLayer");
  if (layer_location >= 0) {
    glUniform1i(layer_location, material_layer);
    glUniform1f(shader->GetUniformLocation("materialScale"), material_scale);
  }
  if (has_normal_map) {
    normal_map->Bind();
  }
  model_->Draw(shader);
}

bool Geometry::IsVisible(const ViewFrustum& frustum) {
  vec3 center =
      vec3(model_matrix * vec4(model_->GetBoundingSphereCenter(), 1.0f));
  // The model matrix can scale the model (e.g. the terrain)
  float scale =
      std::max({length(vec3(model_matrix[0])), length(vec3(model_matrix[1])),
                length(vec3(model_matrix[2]))});
  return frustum.SphereInFrustum(center, model_->GetBoundingSphereRadius() *
                                             scale) != ViewFrustum::kOutside;
}

Model* Geometry::GetModel() { return model_.get(); }

void Geometry::SetPhysicsBody(physics::PhysicsWorld* physics_world,
                              physics::BodyHandle body) {
  this->physics_world_ = physics_world;
  this->physics_body_ = body;
  body_to_model_ =
      physics_world && body != physics::kInvalidBody
          ? inverse(physics_world->GetTransform(body)) * model_matrix
          : mat4(1.0f);
}

bool Geometry::GetHasNormalMap() { return has_normal_map; }

}  // namespace bibo::scene

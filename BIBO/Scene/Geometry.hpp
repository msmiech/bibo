#ifndef BIBO_SCENE_GEOMETRY_HPP_
#define BIBO_SCENE_GEOMETRY_HPP_
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

#include "../Physics/Physics.hpp"
#include "../Resources/Model.hpp"
#include "../Resources/Shader.hpp"
#include "ViewFrustum.hpp"

namespace bibo {
namespace scene {
/**
 * An object of the track (the finish line, the crate, ...): a node of the track
 * file with its texture, drawn where its model matrix puts it. It can follow a
 * physics body (e.g. the crate that the bear pushes around).
 */
class Geometry {
  std::unique_ptr<resources::Model> model_;
  physics::PhysicsWorld* physics_world_ = nullptr;
  physics::BodyHandle physics_body_ = physics::kInvalidBody;
  // From the body to the model: what the pose of the body does not have (e.g.
  // the scale of the model)
  glm::mat4 body_to_model_ = glm::mat4(1.0f);
  // The game uses the meshes (e.g. for the physics and the materials), it
  // cannot run without them
  void RequireMeshes(const char* node_name);

 public:
  /**
   * The program exits if the node does not exist or has no meshes.
   * @param assimp_scene Loaded once for all geometries, see
   * util::LoadSceneFromFile
   * @param normal_map_texture nullptr for geometries without normal map
   */
  Geometry(glm::mat4 model_matrix, Shader* shader, const aiScene* assimp_scene,
           const char* node_name, Texture* texture,
           Texture* normal_map_texture = nullptr);
  Geometry(glm::mat4 model_matrix, Shader* shader, const char* file,
           const char* node_name, Texture* texture,
           Texture* normal_map_texture = nullptr);

  ~Geometry();
  Geometry(const Geometry&) = delete;
  Geometry& operator=(const Geometry&) = delete;

  /**
   * Follows its physics body (if it has one)
   */
  void Update();
  void Draw(Shader* shader);
  /**
   * @return false if the geometry is completely outside of the view frustum and
   * does not need to be drawn
   */
  bool IsVisible(const ViewFrustum& frustum);
  glm::vec3 GetPosition() const { return glm::vec3(model_matrix[3]); }

  resources::Model* GetModel();
  /**
   * The geometry follows the position and rotation of the physics body from now
   * on. Its current model matrix relative to the body (e.g. its scale) is kept,
   * so the body may be anywhere in or around the model.
   */
  void SetPhysicsBody(physics::PhysicsWorld* physics_world,
                      physics::BodyHandle body);
  glm::mat4 model_matrix;
  Texture* normal_map;
  bool has_normal_map;
  // Layer of the generated materials (world::MaterialTextures) instead of the
  // texture, -1: the texture
  int material_layer = -1;
  // How often the generated material repeats per unit of the model
  float material_scale = 1.0f;
  bool GetHasNormalMap();
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_GEOMETRY_HPP_

#ifndef BIBO_RESOURCES_MODEL_HPP_
#define BIBO_RESOURCES_MODEL_HPP_
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <vector>

#include "Mesh.hpp"
#include "Shader.hpp"
#include "Texture.hpp"

namespace bibo {
namespace resources {
class Mesh;

/**
 * The meshes of a node of a model file in the graphics memory. Based on
 * http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/ and
 * http://ogldev.atspace.co.uk/www/tutorial22/tutorial22.html
 */
class Model {
  glm::mat4 tranformation_mat_;
  glm::vec3 bounding_sphere_center_ = glm::vec3(0.0f);
  float bounding_sphere_radius_ = 0.0f;
  void CreateMeshes(Shader* shader, const aiScene* scene, aiNode* node,
                    Texture* texture);
  void ComputeBoundingSphere();
  void Init(Shader* shader, const aiScene* scene, const char* node_name,
            Texture* texture);

 public:
  Model(Shader* shader, const char* filename, const char* node_name,
        Texture* tex);
  /**
   * Copies the meshes of a node of the scene (or of the root node if node_name
   * is nullptr) to the graphics memory. The scene is not needed anymore
   * afterwards. If the node does not exist, the model has no meshes.
   */
  Model(Shader* shader, const aiScene* scene, const char* node_name,
        Texture* tex);
  ~Model();
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  /**
   * @param result Global transformation of the node (its transformation
   * multiplied with the ones of all its parents)
   */
  static void TransformNode(aiMatrix4x4* result, aiNode* root_node);
  glm::mat4 GetTranformationMat() { return tranformation_mat_; }
  /**
   * Sphere around all vertices of the meshes (in model space), used for view
   * frustum culling
   */
  glm::vec3 GetBoundingSphereCenter() const { return bounding_sphere_center_; }
  float GetBoundingSphereRadius() const { return bounding_sphere_radius_; }
  void Draw(Shader* shader);

  std::vector<Mesh*> meshes;  // owned
};
}  // namespace resources
}  // namespace bibo

#endif  // BIBO_RESOURCES_MODEL_HPP_

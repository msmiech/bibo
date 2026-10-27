#ifndef BIBO_RESOURCES_MESH_HPP_
#define BIBO_RESOURCES_MESH_HPP_
#include <assimp/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Material.hpp"
#include "Shader.hpp"
#include "Texture.hpp"

namespace bibo {
namespace resources {
/**
 * A mesh of a Model in the graphics memory. Based on
 * http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/ and
 * http://ogldev.atspace.co.uk/www/tutorial22/tutorial22.html
 */
class Mesh {
  enum BUFFERS {
    kPosBuffer = 0,
    kNormalBuffer = 1,
    kUvBuffer = 2,
    kIndexBuffer = 3,
    kTangentBuffer = 4,
    kBitangentBuffer = 5,
    kBufferCount = 6
  };

  GLuint vao_ = 0;
  GLuint vbo_[kBufferCount] = {};

  Material material_;
  Texture* texture_;

 public:
  Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture);
  ~Mesh();
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  void Draw(Shader* shader);

  unsigned int num_vertices;
  unsigned int num_faces;
  float* vertices = nullptr;
  float* normals = nullptr;
  float* tex_coords = nullptr;
  float* tangents = nullptr;
  float* bitangents = nullptr;
  unsigned int* indices = nullptr;

  void SetTexture(Texture* texture);

  void SetMaterial(Material material);
  /**
   * Averages the normals of the vertices at the same position (a flat shaded
   * model looks round)
   */
  void SmoothNormals();
};
}  // namespace resources
}  // namespace bibo

#endif  // BIBO_RESOURCES_MESH_HPP_

#pragma once
#include <assimp/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Material.hpp"
#include "Shader.hpp"
#include "Texture.hpp"

using namespace std;
using namespace glm;

namespace bibo {
namespace resources {
/**
 * A mesh of a Model in the graphics memory. Based on
 * http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/ and
 * http://ogldev.atspace.co.uk/www/tutorial22/tutorial22.html
 */
class Mesh {
  enum BUFFERS {
    POS_BUFFER = 0,
    NORMAL_BUFFER = 1,
    UV_BUFFER = 2,
    INDEX_BUFFER = 3,
    TANGENT_BUFFER = 4,
    BITANGENT_BUFFER = 5,
    BUFFER_COUNT = 6
  };

  GLuint vao = 0;
  GLuint vbo[BUFFER_COUNT] = {};

  Material material;
  Texture* texture;

 public:
  Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture);
  ~Mesh();
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  void draw(Shader* shader);

  unsigned int numVertices;
  unsigned int numFaces;
  float* vertices = nullptr;
  float* normals = nullptr;
  float* texCoords = nullptr;
  float* tangents = nullptr;
  float* bitangents = nullptr;
  unsigned int* indices = nullptr;

  void setTexture(Texture* texture);

  void setMaterial(Material material);
  /**
   * Averages the normals of the vertices at the same position (a flat shaded
   * model looks round)
   */
  void smoothNormals();
};
}  // namespace resources
}  // namespace bibo

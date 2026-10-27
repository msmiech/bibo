#include "Mesh.hpp"

#include <cmath>
#include <map>
#include <tuple>

using namespace bibo;
using namespace resources;

Mesh::Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture)
    : material(material), texture(texture) {
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  numFaces = mesh->mNumFaces;
  numVertices = mesh->mNumVertices;

  if (mesh->HasPositions()) {
    vertices = new float[mesh->mNumVertices * 3];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      auto pos = mesh->mVertices[i];

      vertices[i * 3] = pos.x;
      vertices[i * 3 + 1] = pos.y;
      vertices[i * 3 + 2] = pos.z;
    }

    glGenBuffers(1, &vbo[POS_BUFFER]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[POS_BUFFER]);
    glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                 vertices, GL_STATIC_DRAW);

    auto positionIndex = shader->getAttribLocation("position");
    glVertexAttribPointer(positionIndex, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionIndex);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (mesh->HasNormals()) {
    normals = new float[mesh->mNumVertices * 3];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      auto normal = mesh->mNormals[i];

      normals[i * 3] = normal.x;
      normals[i * 3 + 1] = normal.y;
      normals[i * 3 + 2] = normal.z;
    }

    auto normalIndex = shader->getAttribLocation("normal");
    if (normalIndex != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo[NORMAL_BUFFER]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo[NORMAL_BUFFER]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   normals, GL_STATIC_DRAW);

      glVertexAttribPointer(normalIndex, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
      glEnableVertexAttribArray(normalIndex);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (mesh->HasTextureCoords(0)) {
    texCoords = new float[mesh->mNumVertices * 2];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      texCoords[i * 2] = mesh->mTextureCoords[0][i].x;
      texCoords[i * 2 + 1] = mesh->mTextureCoords[0][i].y;
    }

    glGenBuffers(1, &vbo[UV_BUFFER]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[UV_BUFFER]);
    glBufferData(GL_ARRAY_BUFFER, 2 * mesh->mNumVertices * sizeof(GLfloat),
                 texCoords, GL_STATIC_DRAW);

    auto uvIndex = shader->getAttribLocation("uv");
    glVertexAttribPointer(uvIndex, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(uvIndex);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (mesh->HasFaces()) {
    indices = new unsigned int[mesh->mNumFaces * 3];
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
      const aiFace& face = mesh->mFaces[i];
      // Points and lines are not rendered (degenerate triangle)
      bool isTriangle = face.mNumIndices == 3;
      indices[i * 3] = face.mIndices[0];
      indices[i * 3 + 1] = isTriangle ? face.mIndices[1] : face.mIndices[0];
      indices[i * 3 + 2] = isTriangle ? face.mIndices[2] : face.mIndices[0];
    }

    glGenBuffers(1, &vbo[INDEX_BUFFER]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[INDEX_BUFFER]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 3 * mesh->mNumFaces * sizeof(GLuint),
                 indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  // Tangents and bitangents for normal mapping
  if (mesh->HasPositions() && mesh->HasTextureCoords(0)) {
    tangents = new float[mesh->mNumVertices * 3];
    bitangents = new float[mesh->mNumVertices * 3];
    if (mesh->HasTangentsAndBitangents()) {
      for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        auto tangent = mesh->mTangents[i];
        auto biTangent = mesh->mBitangents[i];

        tangents[i * 3] = tangent.x;
        tangents[i * 3 + 1] = tangent.y;
        tangents[i * 3 + 2] = tangent.z;

        bitangents[i * 3] = biTangent.x;
        bitangents[i * 3 + 1] = biTangent.y;
        bitangents[i * 3 + 2] = biTangent.z;
      }
    } else {
      aiVector3D v0;
      aiVector3D v1;
      aiVector3D v2;

      aiVector3D Edge1;
      aiVector3D Edge2;

      aiVector3D uv0;
      aiVector3D uv1;
      aiVector3D uv2;

      aiVector3D Tangent;
      aiVector3D Bitangent;

      for (unsigned int i = 0; i < mesh->mNumVertices * 3; ++i) {
        tangents[i] = 0.0f;
        bitangents[i] = 0.0f;
      }

      // One tangent per triangle, assigned to all of its vertices
      for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces;
           ++faceIndex) {
        const aiFace& face = mesh->mFaces[faceIndex];
        if (face.mNumIndices != 3) {
          continue;
        }
        const unsigned int vertexIndices[3] = {
            face.mIndices[0], face.mIndices[1], face.mIndices[2]};

        v0 = mesh->mVertices[vertexIndices[0]];
        v1 = mesh->mVertices[vertexIndices[1]];
        v2 = mesh->mVertices[vertexIndices[2]];

        Edge1 = v1 - v0;
        Edge2 = v2 - v0;

        uv0 = mesh->mTextureCoords[0][vertexIndices[0]];
        uv1 = mesh->mTextureCoords[0][vertexIndices[1]];
        uv2 = mesh->mTextureCoords[0][vertexIndices[2]];

        float DeltaU1 = uv1.x - uv0.x;
        float DeltaV1 = uv1.y - uv0.y;
        float DeltaU2 = uv2.x - uv0.x;
        float DeltaV2 = uv2.y - uv0.y;

        float determinant = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;
        if (std::abs(determinant) < 1e-12f) {
          // the texture coordinates of the triangle are degenerate, they define
          // no tangent
          continue;
        }
        float f = 1.0f / determinant;

        // Solves Edge1 = DeltaU1 * T + DeltaV1 * B and Edge2 = DeltaU2 * T +
        // DeltaV2 * B
        Tangent.x = f * (DeltaV2 * Edge1.x - DeltaV1 * Edge2.x);
        Tangent.y = f * (DeltaV2 * Edge1.y - DeltaV1 * Edge2.y);
        Tangent.z = f * (DeltaV2 * Edge1.z - DeltaV1 * Edge2.z);

        Bitangent.x = f * (DeltaU1 * Edge2.x - DeltaU2 * Edge1.x);
        Bitangent.y = f * (DeltaU1 * Edge2.y - DeltaU2 * Edge1.y);
        Bitangent.z = f * (DeltaU1 * Edge2.z - DeltaU2 * Edge1.z);

        for (unsigned int i : vertexIndices) {
          tangents[i * 3] = Tangent.x;
          tangents[i * 3 + 1] = Tangent.y;
          tangents[i * 3 + 2] = Tangent.z;

          bitangents[i * 3] = Bitangent.x;
          bitangents[i * 3 + 1] = Bitangent.y;
          bitangents[i * 3 + 2] = Bitangent.z;
        }
      }
    }

    auto tangentLocation = shader->getAttribLocation("tangentIN");
    if (tangentLocation != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo[TANGENT_BUFFER]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo[TANGENT_BUFFER]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   tangents, GL_STATIC_DRAW);

      glVertexAttribPointer(tangentLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
      glEnableVertexAttribArray(tangentLocation);

      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    auto bitangentLocation = shader->getAttribLocation("bitangentIN");
    if (bitangentLocation != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo[BITANGENT_BUFFER]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo[BITANGENT_BUFFER]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   bitangents, GL_STATIC_DRAW);

      glVertexAttribPointer(bitangentLocation, 3, GL_FLOAT, GL_FALSE, 0,
                            nullptr);
      glEnableVertexAttribArray(bitangentLocation);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  glBindVertexArray(0);
}

Mesh::~Mesh() {
  glDeleteBuffers(BUFFER_COUNT, vbo);  // the ones that were not created are 0
  glDeleteVertexArrays(1, &vao);

  delete[] vertices;
  delete[] normals;
  delete[] texCoords;
  delete[] tangents;
  delete[] bitangents;
  delete[] indices;
}

void Mesh::draw(Shader* shader) {
  GLuint materialAmbientLocation =
      shader->getUniformLocation("material.ambient");
  GLuint materialDiffuseLocation =
      shader->getUniformLocation("material.diffuse");
  GLuint materialSpecularLocation =
      shader->getUniformLocation("material.specular");
  GLuint materialShininessLocation =
      shader->getUniformLocation("material.shininess");

  // Each one only if the shader uses it (unused uniforms are removed by the
  // compiler)
  if (materialAmbientLocation != 0xFFFFFFFF) {
    glUniform3f(materialAmbientLocation, 0.01f, 0.01f, 0.01f);
  }
  if (materialDiffuseLocation != 0xFFFFFFFF) {
    glUniform3f(materialDiffuseLocation, material.diffuse.r, material.diffuse.g,
                material.diffuse.b);
  }
  if (materialSpecularLocation != 0xFFFFFFFF) {
    glUniform3f(materialSpecularLocation, material.specular.r,
                material.specular.g, material.specular.b);
  }
  if (materialShininessLocation != 0xFFFFFFFF) {
    glUniform1f(materialShininessLocation, material.shininess);
  }

  GLuint textureLocation = shader->getUniformLocation("colorTextureSampler");

  if (textureLocation != 0xFFFFFFFF && texture) {
    texture->bind();
    glUniform1i(textureLocation, 0);
  }

  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, numFaces * 3, GL_UNSIGNED_INT,
                 nullptr);  // the index buffer contains 3 indices per face
  glBindVertexArray(0);
  if (texture) {
    texture->unbind();
  }
}

void Mesh::setTexture(Texture* texture) { this->texture = texture; }

void Mesh::setMaterial(Material material) { this->material = material; }

void Mesh::smoothNormals() {
  if (!vertices || !normals || !vbo[NORMAL_BUFFER]) {
    return;
  }
  // Vertices at the same position (to a thousandth) get the same normal: the
  // average of their normals
  auto key = [this](unsigned int i) {
    return std::make_tuple(std::lround(vertices[i * 3] * 1000.0f),
                           std::lround(vertices[i * 3 + 1] * 1000.0f),
                           std::lround(vertices[i * 3 + 2] * 1000.0f));
  };
  std::map<std::tuple<long, long, long>, glm::vec3> sums;
  for (unsigned int i = 0; i < numVertices; ++i) {
    sums[key(i)] +=
        glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
  }
  for (unsigned int i = 0; i < numVertices; ++i) {
    glm::vec3 sum = sums[key(i)];
    if (glm::length(sum) > 1e-6f) {
      glm::vec3 normal = glm::normalize(sum);
      normals[i * 3] = normal.x;
      normals[i * 3 + 1] = normal.y;
      normals[i * 3 + 2] = normal.z;
    }
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo[NORMAL_BUFFER]);
  glBufferSubData(GL_ARRAY_BUFFER, 0, 3 * numVertices * sizeof(GLfloat),
                  normals);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

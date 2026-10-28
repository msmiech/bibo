#include "Mesh.hpp"

#include <cmath>
#include <map>
#include <tuple>

namespace bibo::resources {

Mesh::Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture)
    : material_(material), texture_(texture) {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);

  num_faces = mesh->mNumFaces;
  num_vertices = mesh->mNumVertices;

  if (mesh->HasPositions()) {
    vertices = new float[mesh->mNumVertices * 3];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      auto pos = mesh->mVertices[i];

      vertices[i * 3] = pos.x;
      vertices[i * 3 + 1] = pos.y;
      vertices[i * 3 + 2] = pos.z;
    }

    glGenBuffers(1, &vbo_[kPosBuffer]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[kPosBuffer]);
    glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                 vertices, GL_STATIC_DRAW);

    auto position_index = shader->GetAttribLocation("position");
    glVertexAttribPointer(position_index, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(position_index);

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

    auto normal_index = shader->GetAttribLocation("normal");
    if (normal_index != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo_[kNormalBuffer]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo_[kNormalBuffer]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   normals, GL_STATIC_DRAW);

      glVertexAttribPointer(normal_index, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
      glEnableVertexAttribArray(normal_index);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (mesh->HasTextureCoords(0)) {
    tex_coords = new float[mesh->mNumVertices * 2];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      tex_coords[i * 2] = mesh->mTextureCoords[0][i].x;
      tex_coords[i * 2 + 1] = mesh->mTextureCoords[0][i].y;
    }

    glGenBuffers(1, &vbo_[kUvBuffer]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[kUvBuffer]);
    glBufferData(GL_ARRAY_BUFFER, 2 * mesh->mNumVertices * sizeof(GLfloat),
                 tex_coords, GL_STATIC_DRAW);

    auto uv_index = shader->GetAttribLocation("uv");
    glVertexAttribPointer(uv_index, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(uv_index);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (mesh->HasFaces()) {
    indices = new unsigned int[mesh->mNumFaces * 3];
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
      const aiFace& face = mesh->mFaces[i];
      // Points and lines are not rendered (degenerate triangle)
      bool is_triangle = face.mNumIndices == 3;
      indices[i * 3] = face.mIndices[0];
      indices[i * 3 + 1] = is_triangle ? face.mIndices[1] : face.mIndices[0];
      indices[i * 3 + 2] = is_triangle ? face.mIndices[2] : face.mIndices[0];
    }

    glGenBuffers(1, &vbo_[kIndexBuffer]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_[kIndexBuffer]);
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
        auto bi_tangent = mesh->mBitangents[i];

        tangents[i * 3] = tangent.x;
        tangents[i * 3 + 1] = tangent.y;
        tangents[i * 3 + 2] = tangent.z;

        bitangents[i * 3] = bi_tangent.x;
        bitangents[i * 3 + 1] = bi_tangent.y;
        bitangents[i * 3 + 2] = bi_tangent.z;
      }
    } else {
      aiVector3D v0;
      aiVector3D v1;
      aiVector3D v2;

      aiVector3D edge1;
      aiVector3D edge2;

      aiVector3D uv0;
      aiVector3D uv1;
      aiVector3D uv2;

      aiVector3D tangent;
      aiVector3D bitangent;

      for (unsigned int i = 0; i < mesh->mNumVertices * 3; ++i) {
        tangents[i] = 0.0f;
        bitangents[i] = 0.0f;
      }

      // One tangent per triangle, assigned to all of its vertices
      for (unsigned int face_index = 0; face_index < mesh->mNumFaces;
           ++face_index) {
        const aiFace& face = mesh->mFaces[face_index];
        if (face.mNumIndices != 3) {
          continue;
        }
        const unsigned int vertex_indices[3] = {
            face.mIndices[0], face.mIndices[1], face.mIndices[2]};

        v0 = mesh->mVertices[vertex_indices[0]];
        v1 = mesh->mVertices[vertex_indices[1]];
        v2 = mesh->mVertices[vertex_indices[2]];

        edge1 = v1 - v0;
        edge2 = v2 - v0;

        uv0 = mesh->mTextureCoords[0][vertex_indices[0]];
        uv1 = mesh->mTextureCoords[0][vertex_indices[1]];
        uv2 = mesh->mTextureCoords[0][vertex_indices[2]];

        float delta_u1 = uv1.x - uv0.x;
        float delta_v1 = uv1.y - uv0.y;
        float delta_u2 = uv2.x - uv0.x;
        float delta_v2 = uv2.y - uv0.y;

        float determinant = delta_u1 * delta_v2 - delta_u2 * delta_v1;
        if (std::abs(determinant) < 1e-12f) {
          // the texture coordinates of the triangle are degenerate, they define
          // no tangent
          continue;
        }
        float f = 1.0f / determinant;

        // Solves Edge1 = DeltaU1 * T + DeltaV1 * B and Edge2 = DeltaU2 * T +
        // DeltaV2 * B
        tangent.x = f * (delta_v2 * edge1.x - delta_v1 * edge2.x);
        tangent.y = f * (delta_v2 * edge1.y - delta_v1 * edge2.y);
        tangent.z = f * (delta_v2 * edge1.z - delta_v1 * edge2.z);

        bitangent.x = f * (delta_u1 * edge2.x - delta_u2 * edge1.x);
        bitangent.y = f * (delta_u1 * edge2.y - delta_u2 * edge1.y);
        bitangent.z = f * (delta_u1 * edge2.z - delta_u2 * edge1.z);

        for (unsigned int i : vertex_indices) {
          tangents[i * 3] = tangent.x;
          tangents[i * 3 + 1] = tangent.y;
          tangents[i * 3 + 2] = tangent.z;

          bitangents[i * 3] = bitangent.x;
          bitangents[i * 3 + 1] = bitangent.y;
          bitangents[i * 3 + 2] = bitangent.z;
        }
      }
    }

    auto tangent_location = shader->GetAttribLocation("tangentIN");
    if (tangent_location != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo_[kTangentBuffer]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo_[kTangentBuffer]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   tangents, GL_STATIC_DRAW);

      glVertexAttribPointer(tangent_location, 3, GL_FLOAT, GL_FALSE, 0,
                            nullptr);
      glEnableVertexAttribArray(tangent_location);

      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    auto bitangent_location = shader->GetAttribLocation("bitangentIN");
    if (bitangent_location != 0xFFFFFFFF) {
      glGenBuffers(1, &vbo_[kBitangentBuffer]);
      glBindBuffer(GL_ARRAY_BUFFER, vbo_[kBitangentBuffer]);
      glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat),
                   bitangents, GL_STATIC_DRAW);

      glVertexAttribPointer(bitangent_location, 3, GL_FLOAT, GL_FALSE, 0,
                            nullptr);
      glEnableVertexAttribArray(bitangent_location);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  glBindVertexArray(0);
}

Mesh::~Mesh() {
  glDeleteBuffers(kBufferCount, vbo_);  // the ones that were not created are 0
  glDeleteVertexArrays(1, &vao_);

  delete[] vertices;
  delete[] normals;
  delete[] tex_coords;
  delete[] tangents;
  delete[] bitangents;
  delete[] indices;
}

void Mesh::Draw(Shader* shader) {
  GLuint material_ambient_location =
      shader->GetUniformLocation("material.ambient");
  GLuint material_diffuse_location =
      shader->GetUniformLocation("material.diffuse");
  GLuint material_specular_location =
      shader->GetUniformLocation("material.specular");
  GLuint material_shininess_location =
      shader->GetUniformLocation("material.shininess");

  // Each one only if the shader uses it (unused uniforms are removed by the
  // compiler)
  if (material_ambient_location != 0xFFFFFFFF) {
    glUniform3f(material_ambient_location, 0.01f, 0.01f, 0.01f);
  }
  if (material_diffuse_location != 0xFFFFFFFF) {
    glUniform3f(material_diffuse_location, material_.diffuse.r,
                material_.diffuse.g, material_.diffuse.b);
  }
  if (material_specular_location != 0xFFFFFFFF) {
    glUniform3f(material_specular_location, material_.specular.r,
                material_.specular.g, material_.specular.b);
  }
  if (material_shininess_location != 0xFFFFFFFF) {
    glUniform1f(material_shininess_location, material_.shininess);
  }

  GLuint texture_location = shader->GetUniformLocation("colorTextureSampler");

  if (texture_location != 0xFFFFFFFF && texture_) {
    texture_->Bind();
    glUniform1i(texture_location, 0);
  }

  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, num_faces * 3, GL_UNSIGNED_INT,
                 nullptr);  // the index buffer contains 3 indices per face
  glBindVertexArray(0);
  if (texture_) {
    texture_->Unbind();
  }
}

void Mesh::SetTexture(Texture* texture) { this->texture_ = texture; }

void Mesh::SetMaterial(Material material) { this->material_ = material; }

void Mesh::SmoothNormals() {
  if (!vertices || !normals || !vbo_[kNormalBuffer]) {
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
  for (unsigned int i = 0; i < num_vertices; ++i) {
    sums[key(i)] +=
        glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
  }
  for (unsigned int i = 0; i < num_vertices; ++i) {
    glm::vec3 sum = sums[key(i)];
    if (glm::length(sum) > 1e-6f) {
      glm::vec3 normal = glm::normalize(sum);
      normals[i * 3] = normal.x;
      normals[i * 3 + 1] = normal.y;
      normals[i * 3 + 2] = normal.z;
    }
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_[kNormalBuffer]);
  glBufferSubData(GL_ARRAY_BUFFER, 0, 3 * num_vertices * sizeof(GLfloat),
                  normals);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace bibo::resources

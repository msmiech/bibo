#include "Model.hpp"

#include <iostream>

#include "../Util/Util.hpp"

namespace bibo::resources {

using glm::vec3;
using std::numeric_limits;
using util::ConvertMatrix;

Model::Model(Shader* shader, const char* path, const char* node_name,
             Texture* texture) {
  Assimp::Importer importer;
  auto scene =
      importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs |
                                  aiProcess_CalcTangentSpace);
  if (!scene) {
    printf("Unable to load model: %s\n", importer.GetErrorString());
    return;
  }

  Init(shader, scene, node_name, texture);
}

Model::Model(Shader* shader, const aiScene* scene, const char* node_name,
             Texture* texture) {
  Init(shader, scene, node_name, texture);
}

void Model::Init(Shader* shader, const aiScene* scene, const char* node_name,
                 Texture* texture) {
  auto node = scene->mRootNode;
  if (!node) {
    printf("There is no root node!");
    return;
  }
  if (node_name) {
    node = node->FindNode(node_name);
    if (!node) {
      printf("Could not find/load node: %s\n", node_name);
      return;
    }
  }

  auto transformation = node->mTransformation;
  TransformNode(&transformation, node);

  tranformation_mat_ = ConvertMatrix(transformation);

  CreateMeshes(shader, scene, node, texture);
  ComputeBoundingSphere();
}

void Model::ComputeBoundingSphere() {
  vec3 minimum(numeric_limits<float>::max());
  vec3 maximum(numeric_limits<float>::lowest());
  for (const Mesh* mesh : meshes) {
    for (unsigned int i = 0; mesh->vertices && i < mesh->num_vertices; ++i) {
      vec3 vertex(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1],
                  mesh->vertices[i * 3 + 2]);
      minimum = glm::min(minimum, vertex);
      maximum = glm::max(maximum, vertex);
    }
  }
  if (minimum.x > maximum.x) {
    return;  // no vertices
  }

  bounding_sphere_center_ = 0.5f * (minimum + maximum);
  bounding_sphere_radius_ = 0.0f;
  for (const Mesh* mesh : meshes) {
    for (unsigned int i = 0; mesh->vertices && i < mesh->num_vertices; ++i) {
      vec3 vertex(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1],
                  mesh->vertices[i * 3 + 2]);
      bounding_sphere_radius_ =
          std::max(bounding_sphere_radius_,
                   glm::distance(bounding_sphere_center_, vertex));
    }
  }
}

Model::~Model() {
  for (Mesh* mesh : meshes) {
    delete mesh;
  }
}

namespace {
/**
 * The colors of the material of a mesh (the defaults if the scene has no
 * materials)
 */
Material MaterialOf(const aiScene* scene, const aiMesh* mesh) {
  Material material;
  if (!scene->HasMaterials() || mesh->mMaterialIndex >= scene->mNumMaterials) {
    return material;
  }
  const aiMaterial* assimp_material = scene->mMaterials[mesh->mMaterialIndex];
  if (!assimp_material) {
    return material;
  }
  aiColor4D color;
  if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_AMBIENT, &color) ==
      AI_SUCCESS) {
    material.ambient = vec3(color.r, color.g, color.b);
  }
  if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_DIFFUSE, &color) ==
      AI_SUCCESS) {
    material.diffuse = vec3(color.r, color.g, color.b);
  }
  if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_SPECULAR, &color) ==
      AI_SUCCESS) {
    material.specular = vec3(color.r, color.g, color.b);
  }
  return material;
}
}  // namespace

void Model::CreateMeshes(Shader* shader, const aiScene* scene, aiNode* node,
                         Texture* texture) {
  // The meshes of the node itself and of its direct children (e.g. the objects
  // of the scene below its root node)
  auto add = [&](const aiNode* owner) {
    for (unsigned int i = 0; i < owner->mNumMeshes; ++i) {
      aiMesh* assimp_mesh = scene->mMeshes[owner->mMeshes[i]];
      if (assimp_mesh) {
        meshes.push_back(new Mesh(shader, assimp_mesh,
                                  MaterialOf(scene, assimp_mesh), texture));
      }
    }
  };
  add(node);
  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    add(node->mChildren[i]);
  }
}

void Model::Draw(Shader* shader) {
  for (Mesh* mesh : meshes) {
    mesh->Draw(shader);
  }
}

// From
// http://gamedev.stackexchange.com/questions/87239/how-to-properly-transform-vertices-for-a-model-loaded-using-assimp
void Model::TransformNode(aiMatrix4x4* result, aiNode* root_node) {
  if (root_node->mParent) {
    TransformNode(result, root_node->mParent);
    aiMultiplyMatrix4(result, &root_node->mTransformation);
  } else {
    *result = root_node->mTransformation;
  }
}

}  // namespace bibo::resources

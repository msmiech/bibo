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

void Model::CreateMeshes(Shader* shader, const aiScene* scene, aiNode* node,
                         Texture* texture) {
  if (node->mNumChildren == 0) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      aiMesh* assimp_mesh = scene->mMeshes[node->mMeshes[i]];
      Material mesh_material;
      if (scene->HasMaterials() && assimp_mesh) {
        aiMaterial* assimp_material =
            scene->mMaterials[assimp_mesh->mMaterialIndex];
        if (assimp_material) {
          aiColor4D ambient;
          if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_AMBIENT,
                                 &ambient) == AI_SUCCESS) {
            mesh_material.ambient = vec3(ambient.r, ambient.g, ambient.b);
          }

          aiColor4D diffuse;
          if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_DIFFUSE,
                                 &diffuse) == AI_SUCCESS) {
            mesh_material.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
          }

          aiColor4D specular;
          if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_SPECULAR,
                                 &specular) == AI_SUCCESS) {
            mesh_material.specular = vec3(specular.r, specular.g, specular.b);
          }
        }
      }
      meshes.push_back(new Mesh(shader, assimp_mesh, mesh_material, texture));
    }
  } else {
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
      aiNode* current_node = node->mChildren[i];
      for (unsigned int j = 0; j < current_node->mNumMeshes; ++j) {
        Material mesh_material;
        aiMesh* assimp_mesh = scene->mMeshes[current_node->mMeshes[j]];
        if (assimp_mesh) {
          aiMaterial* assimp_material =
              scene->mMaterials[assimp_mesh->mMaterialIndex];
          if (assimp_material) {
            aiColor4D ambient;
            if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_AMBIENT,
                                   &ambient) == AI_SUCCESS) {
              mesh_material.ambient = vec3(ambient.r, ambient.g, ambient.b);
            }

            aiColor4D diffuse;
            if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_DIFFUSE,
                                   &diffuse) == AI_SUCCESS) {
              mesh_material.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
            }

            aiColor4D specular;
            if (aiGetMaterialColor(assimp_material, AI_MATKEY_COLOR_SPECULAR,
                                   &specular) == AI_SUCCESS) {
              mesh_material.specular = vec3(specular.r, specular.g, specular.b);
            }
          }
        }
        meshes.push_back(new Mesh(shader, assimp_mesh, mesh_material, texture));
      }
    }
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

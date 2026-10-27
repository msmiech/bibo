#include "Model.hpp"

#include <iostream>

#include "../Util/Util.hpp"

using namespace bibo;
using namespace resources;
using namespace util;

Model::Model(Shader* shader, const char* path, const char* nodeName,
             Texture* texture) {
  Assimp::Importer importer;
  auto scene =
      importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs |
                                  aiProcess_CalcTangentSpace);
  if (!scene) {
    printf("Unable to load model: %s\n", importer.GetErrorString());
    return;
  }

  init(shader, scene, nodeName, texture);
}

Model::Model(Shader* shader, const aiScene* scene, const char* nodeName,
             Texture* texture) {
  init(shader, scene, nodeName, texture);
}

void Model::init(Shader* shader, const aiScene* scene, const char* nodeName,
                 Texture* texture) {
  auto node = scene->mRootNode;
  if (!node) {
    printf("There is no root node!");
    return;
  }
  if (nodeName) {
    node = node->FindNode(nodeName);
    if (!node) {
      printf("Could not find/load node: %s\n", nodeName);
      return;
    }
  }

  auto transformation = node->mTransformation;
  transformNode(&transformation, node);

  tranformationMat = ConvertMatrix(transformation);

  createMeshes(shader, scene, node, texture);
  computeBoundingSphere();
}

void Model::computeBoundingSphere() {
  vec3 minimum(numeric_limits<float>::max());
  vec3 maximum(numeric_limits<float>::lowest());
  for (const Mesh* mesh : meshes) {
    for (unsigned int i = 0; mesh->vertices && i < mesh->numVertices; ++i) {
      vec3 vertex(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1],
                  mesh->vertices[i * 3 + 2]);
      minimum = glm::min(minimum, vertex);
      maximum = glm::max(maximum, vertex);
    }
  }
  if (minimum.x > maximum.x) {
    return;  // no vertices
  }

  boundingSphereCenter = 0.5f * (minimum + maximum);
  boundingSphereRadius = 0.0f;
  for (const Mesh* mesh : meshes) {
    for (unsigned int i = 0; mesh->vertices && i < mesh->numVertices; ++i) {
      vec3 vertex(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1],
                  mesh->vertices[i * 3 + 2]);
      boundingSphereRadius = std::max(
          boundingSphereRadius, glm::distance(boundingSphereCenter, vertex));
    }
  }
}

Model::~Model() {
  for (Mesh* mesh : meshes) {
    delete mesh;
  }
}

void Model::createMeshes(Shader* shader, const aiScene* scene, aiNode* node,
                         Texture* texture) {
  if (node->mNumChildren == 0) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      aiMesh* assimpMesh = scene->mMeshes[node->mMeshes[i]];
      Material meshMaterial;
      if (scene->HasMaterials() && assimpMesh) {
        aiMaterial* assimpMaterial =
            scene->mMaterials[assimpMesh->mMaterialIndex];
        if (assimpMaterial) {
          aiColor4D ambient;
          if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_AMBIENT,
                                 &ambient) == AI_SUCCESS) {
            meshMaterial.ambient = vec3(ambient.r, ambient.g, ambient.b);
          }

          aiColor4D diffuse;
          if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE,
                                 &diffuse) == AI_SUCCESS) {
            meshMaterial.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
          }

          aiColor4D specular;
          if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_SPECULAR,
                                 &specular) == AI_SUCCESS) {
            meshMaterial.specular = vec3(specular.r, specular.g, specular.b);
          }
        }
      }
      meshes.push_back(new Mesh(shader, assimpMesh, meshMaterial, texture));
    }
  } else {
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
      aiNode* currentNode = node->mChildren[i];
      for (unsigned int j = 0; j < currentNode->mNumMeshes; ++j) {
        Material meshMaterial;
        aiMesh* assimpMesh = scene->mMeshes[currentNode->mMeshes[j]];
        if (assimpMesh) {
          aiMaterial* assimpMaterial =
              scene->mMaterials[assimpMesh->mMaterialIndex];
          if (assimpMaterial) {
            aiColor4D ambient;
            if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_AMBIENT,
                                   &ambient) == AI_SUCCESS) {
              meshMaterial.ambient = vec3(ambient.r, ambient.g, ambient.b);
            }

            aiColor4D diffuse;
            if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE,
                                   &diffuse) == AI_SUCCESS) {
              meshMaterial.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
            }

            aiColor4D specular;
            if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_SPECULAR,
                                   &specular) == AI_SUCCESS) {
              meshMaterial.specular = vec3(specular.r, specular.g, specular.b);
            }
          }
        }
        meshes.push_back(new Mesh(shader, assimpMesh, meshMaterial, texture));
      }
    }
  }
}

void Model::draw(Shader* shader) {
  for (Mesh* mesh : meshes) {
    mesh->draw(shader);
  }
}

// From
// http://gamedev.stackexchange.com/questions/87239/how-to-properly-transform-vertices-for-a-model-loaded-using-assimp
void Model::transformNode(aiMatrix4x4* result, aiNode* rootNode) {
  if (rootNode->mParent) {
    transformNode(result, rootNode->mParent);
    aiMultiplyMatrix4(result, &rootNode->mTransformation);
  } else {
    *result = rootNode->mTransformation;
  }
}

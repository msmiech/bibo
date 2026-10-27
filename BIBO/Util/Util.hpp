#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std;

namespace bibo {
namespace util {
struct SceneDeleter {
  void operator()(const aiScene* scene) const { aiReleaseImport(scene); }
};
using ScenePtr = unique_ptr<const aiScene, SceneDeleter>;

/**
 * Loads a model file once, so that several models can be created from its nodes
 * @return The loaded scene or nullptr
 */
ScenePtr loadSceneFromFile(const char* file);
glm::mat4 ConvertMatrix(aiMatrix4x4 in);
}  // namespace util
}  // namespace bibo

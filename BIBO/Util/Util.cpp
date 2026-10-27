#include "Util.hpp"

#include <assimp/postprocess.h>

#include <cstdio>

namespace bibo::util {

ScenePtr LoadSceneFromFile(const char* file) {
  Assimp::Importer importer;
  auto scene =
      importer.ReadFile(file, aiProcess_Triangulate | aiProcess_FlipUVs |
                                  aiProcess_CalcTangentSpace);
  if (!scene) {
    printf("Unable to load model %s: %s\n", file, importer.GetErrorString());
    return nullptr;
  }
  // The importer owns the scene and would delete it at the end of this function
  return ScenePtr(importer.GetOrphanedScene());
}

glm::mat4 ConvertMatrix(aiMatrix4x4 in) {
  return glm::mat4(in.a1, in.b1, in.c1, in.d1, in.a2, in.b2, in.c2, in.d2,
                   in.a3, in.b3, in.c3, in.d3, in.a4, in.b4, in.c4, in.d4);
}

}  // namespace bibo::util

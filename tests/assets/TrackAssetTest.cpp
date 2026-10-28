#include <assimp/scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cmath>

#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"
#include "World/WorldGenerator.hpp"

using bibo::test::GameNodeNames;
using bibo::test::NodeModelMatrix;
using bibo::test::NormalMappedNodeNames;
using bibo::test::SharedWorldGenerator;
using bibo::test::TrackFinishLine;
using bibo::test::TrackNode;
using bibo::test::TrackScene;

namespace rules = bibo::scene::rules;
using glm::vec3;

namespace {
/**
 * Meshes the game creates for a node (Model::CreateMeshes): the meshes of the
 * node, or of its children if it has any
 */
std::vector<const aiMesh*> MeshesOf(const aiNode& node) {
  const aiScene& scene = TrackScene();
  std::vector<const aiMesh*> meshes;
  if (node.mNumChildren == 0) {
    for (unsigned int i = 0; i < node.mNumMeshes; ++i) {
      meshes.push_back(scene.mMeshes[node.mMeshes[i]]);
    }
  } else {
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
      for (unsigned int j = 0; j < node.mChildren[i]->mNumMeshes; ++j) {
        meshes.push_back(scene.mMeshes[node.mChildren[i]->mMeshes[j]]);
      }
    }
  }
  return meshes;
}

vec3 StartPosition(const std::string& node_name) {
  return vec3(NodeModelMatrix(TrackNode(node_name))[3]);
}
}  // namespace

TEST_CASE("Every node the game loads is a triangle mesh in the track",
          "[assets]") {
  const std::string name = GENERATE(from_range(GameNodeNames()));
  CAPTURE(name);

  // The game uses meshes[0] of several of these nodes without checking
  std::vector<const aiMesh*> meshes = MeshesOf(TrackNode(name));
  REQUIRE_FALSE(meshes.empty());

  for (const aiMesh* mesh : meshes) {
    CAPTURE(mesh->mName.C_Str());
    CHECK(mesh->HasPositions());
    CHECK(mesh->HasNormals());
    CHECK(mesh->mNumFaces > 0);
    CHECK((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0);
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      for (unsigned int i = 0; i < face.mNumIndices; ++i) {
        if (face.mIndices[i] >= mesh->mNumVertices) {
          FAIL("Face " << f << " uses vertex " << face.mIndices[i] << " of "
                       << mesh->mNumVertices);
        }
      }
    }
  }
}

TEST_CASE("Normal mapped nodes have texture coordinates and tangents",
          "[assets]") {
  const std::string name = GENERATE(from_range(NormalMappedNodeNames()));
  CAPTURE(name);

  for (const aiMesh* mesh : MeshesOf(TrackNode(name))) {
    CAPTURE(mesh->mName.C_Str());
    CHECK(mesh->HasTextureCoords(0));
    CHECK(mesh->HasTangentsAndBitangents());
  }
}

TEST_CASE("The track contains the sun light", "[assets]") {
  // The scene uses the first light for lighting and shadows
  CHECK(TrackScene().mNumLights >= 1);
}

TEST_CASE(
    "The bear starts on the finish line, the lap is only complete after all "
    "checkpoints",
    "[assets][rules]") {
  const vec3 bear = StartPosition("BearBody");
  const rules::FinishLine finish_line = TrackFinishLine();

  CAPTURE(bear, finish_line.center, finish_line.half_width);
  CHECK(bear.y >
        SharedWorldGenerator().GetSeaLevel() + 2.0f);  // not in the water
  CHECK(rules::IsOnFinishLine(bear, finish_line));     // the race is a lap
  CHECK(rules::PassCheckpoint(bear, 0) == 0);  // no checkpoint at the start
  CHECK_FALSE(rules::HasCompletedLap(0, bear, finish_line));
  CHECK(rules::HasCompletedLap(rules::kCheckpointCount, bear, finish_line));
}

TEST_CASE("The finish line goes across the whole track", "[assets][rules]") {
  // The track is about 15 units wide at the finish line and goes along the z
  // axis (the bear starts looking along -z)
  const rules::FinishLine finish_line = TrackFinishLine();

  CAPTURE(finish_line.across, finish_line.half_width);
  CHECK(std::abs(finish_line.across.x) > 0.99f);
  CHECK(finish_line.half_width > 7.0f);
}

TEST_CASE("The checkpoints are apart from each other and from the start",
          "[assets][rules]") {
  // Otherwise the bear could pass two checkpoints at once or one at the start
  const vec3 bear = StartPosition("BearBody");
  const glm::vec2 start(bear.x, bear.z);
  for (std::size_t i = 0; i < rules::kCheckpointCount; ++i) {
    CAPTURE(i);
    CHECK(glm::distance(rules::kCheckpoints[i], start) >
          rules::kCheckpointRadius);
    if (i > 0) {
      CHECK(glm::distance(rules::kCheckpoints[i], rules::kCheckpoints[i - 1]) >
            2.0f * rules::kCheckpointRadius);
    }
  }
}

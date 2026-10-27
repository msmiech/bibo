#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Physics/Physics.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Scene/BearCharacter.hpp"
#include "Scene/BearRig.hpp"
#include "Scene/Fur.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
using bibo::resources::SkinnedModel;
using bibo::scene::BearCharacter;
using bibo::scene::BearRig;
using Catch::Matchers::WithinAbs;
using glm::mat4;
using glm::vec3;

namespace {
std::vector<std::size_t> verticesOf(const SkinnedModel& model,
                                    SkinnedModel::Surface surface) {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < model.surfaces.size(); ++i) {
    if (static_cast<int>(model.surfaces[i]) == surface) {
      result.push_back(i);
    }
  }
  return result;
}

vec3 lowest(const SkinnedModel& model) {
  vec3 minimum(1e9f);
  for (const vec3& p : model.positions) {
    minimum = glm::min(minimum, p);
  }
  return minimum;
}

vec3 highest(const SkinnedModel& model) {
  vec3 maximum(-1e9f);
  for (const vec3& p : model.positions) {
    maximum = glm::max(maximum, p);
  }
  return maximum;
}
}  // namespace

TEST_CASE("The bear model has every bone the animation needs",
          "[assets][bear]") {
  const SkinnedModel& model = bearModel();
  BearRig rig(model.skeleton);
  CAPTURE(rig.getMissingBones());
  CHECK(rig.isValid());
  CHECK(model.skeleton.bones.size() >= 30);
  CHECK(model.skeleton.find("root") ==
        0);  // the root bone comes first, every parent before its children
  for (std::size_t i = 0; i < model.skeleton.bones.size(); ++i) {
    CHECK(model.skeleton.bones[i].parent < static_cast<int>(i));
  }
}

TEST_CASE("Every vertex of the bear is skinned to at most four bones",
          "[assets][bear]") {
  const SkinnedModel& model = bearModel();
  REQUIRE(model.positions.size() > 10000);
  REQUIRE(model.joints.size() == model.positions.size());
  REQUIRE(model.weights.size() == model.positions.size());
  const int bones = static_cast<int>(model.skeleton.bones.size());
  for (std::size_t i = 0; i < model.positions.size(); ++i) {
    const glm::vec4& w = model.weights[i];
    CHECK_THAT(w.x + w.y + w.z + w.w, WithinAbs(1.0f, 1e-4f));
    CHECK(glm::all(glm::greaterThanEqual(model.joints[i], glm::ivec4(0))));
    CHECK(glm::all(glm::lessThan(model.joints[i], glm::ivec4(bones))));
    CHECK_THAT(glm::length(model.normals[i]), WithinAbs(1.0f, 1e-3f));
    CHECK(glm::all(glm::greaterThanEqual(model.colors[i], glm::vec4(0.0f))));
    CHECK(glm::all(glm::lessThanEqual(model.colors[i], glm::vec4(1.0f))));
  }
  for (unsigned int index : model.indices) {
    REQUIRE(index < model.positions.size());
  }
}

TEST_CASE("The skeleton of the bear matches the skinning in the model file",
          "[assets][bear]") {
  // The game computes the bind pose from the bone transforms of the file, which
  // must match the inverse bind matrices of the skin
  const SkinnedModel& model = bearModel();
  Assimp::Importer importer;
  const aiScene* scene =
      importer.ReadFile(assetPath(BEAR_FILE).string(), aiProcess_Triangulate);
  REQUIRE(scene);
  int compared = 0;
  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; ++b) {
      const aiBone* bone = scene->mMeshes[m]->mBones[b];
      int index = model.skeleton.find(bone->mName.C_Str());
      REQUIRE(index >= 0);
      const aiMatrix4x4& o = bone->mOffsetMatrix;
      mat4 offset(o.a1, o.b1, o.c1, o.d1, o.a2, o.b2, o.c2, o.d2, o.a3, o.b3,
                  o.c3, o.d3, o.a4, o.b4, o.c4, o.d4);
      CAPTURE(bone->mName.C_Str());
      CHECK_THAT(model.skeleton.bones[index].inverseBind,
                 IsNear(offset, 2e-3f));
      ++compared;
    }
  }
  CHECK(compared > 30);

  SECTION(
      "In the bind pose the skinned vertices are where the model has them") {
    std::vector<mat4> matrices, skin;
    model.skeleton.modelMatrices(model.skeleton.bindPose(), matrices);
    model.skeleton.skinMatrices(matrices, skin);
    std::vector<vec3> positions, normals;
    model.skin(skin, positions, normals);
    for (std::size_t i = 0; i < positions.size(); i += 7) {
      CHECK_THAT(positions[i], IsNear(model.positions[i], 1e-4f));
    }
  }
}

TEST_CASE(
    "The bear stands on the ground, looks along +z and fits its character "
    "capsule",
    "[assets][bear]") {
  const SkinnedModel& model = bearModel();
  vec3 low = lowest(model), high = highest(model);
  CAPTURE(low, high);
  CHECK_THAT(
      low.y,
      WithinAbs(0.0f, 0.03f));  // the paws (the claws may reach a bit lower)
  CHECK(high.y > 2.0f);         // ears
  CHECK(high.y < 2.6f);
  CHECK_THAT(low.x, WithinAbs(-high.x, 0.05f));  // symmetric
  CHECK(high.z - low.z > 2.5f);                  // nose to tail
  CHECK(high.z - low.z < 3.3f);
  // The center of the body is above the origin, under the center of the
  // character capsule (the muzzle reaches a bit further)
  CHECK(std::abs(0.5f * (low.z + high.z)) < 0.35f);

  SECTION("The nose is the front") {
    std::vector<std::size_t> nose = verticesOf(model, SkinnedModel::NOSE);
    REQUIRE_FALSE(nose.empty());
    for (std::size_t i : nose) {
      CHECK(model.positions[i].z > high.z - 0.15f);
    }
  }
  SECTION("The bear (with its fur) is at most a bit wider than the capsule") {
    const float radius =
        BearCharacter::characterControllerDesc(vec3(0.0f)).radius;
    CHECK(high.x + bibo::fur::MAX_LENGTH < radius);
  }
}

TEST_CASE(
    "The bear has fur, bare paw pads, eyes, nose, mouth, tongue, teeth and "
    "claws",
    "[assets][bear]") {
  const SkinnedModel& model = bearModel();
  for (SkinnedModel::Surface surface :
       {SkinnedModel::EYE, SkinnedModel::NOSE, SkinnedModel::MOUTH,
        SkinnedModel::TONGUE, SkinnedModel::TEETH, SkinnedModel::CLAW}) {
    CAPTURE(surface);
    std::vector<std::size_t> vertices = verticesOf(model, surface);
    CHECK(vertices.size() > 50);
    if (surface != SkinnedModel::EYE) {
      for (std::size_t i : vertices) {
        CHECK(model.colors[i].a == 0.0f);  // no fur
      }
    }
  }
  std::vector<std::size_t> fur = verticesOf(model, SkinnedModel::FUR);
  int furry = 0, bare = 0;
  for (std::size_t i : fur) {
    furry += model.colors[i].a > 0.3f;
    bare += model.colors[i].a < 0.02f;
  }
  CHECK(furry > static_cast<int>(fur.size()) / 2);
  CHECK(bare > 50);  // paw pads, lips, the inside of the eyelids

  SECTION(
      "The eyes store the direction from their center: unit vectors that look "
      "forward in the middle of the eye") {
    int looking = 0;
    for (std::size_t i : verticesOf(model, SkinnedModel::EYE)) {
      vec3 direction = vec3(model.colors[i]) * 2.0f - 1.0f;
      CHECK_THAT(glm::length(direction), WithinAbs(1.0f, 0.02f));
      looking += direction.z > 0.9f;
    }
    CHECK(looking > 20);
  }
}

TEST_CASE("The jaw opens the mouth and the tongue sticks out of it",
          "[assets][bear]") {
  const SkinnedModel& model = bearModel();
  bibo::physics::PhysicsWorld world;
  world.createStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(50.0f, 1.0f, 50.0f));
  BearCharacter bear(world, model, vec3(0.0f, 1.4f, 0.0f),
                     vec3(0.0f, 0.0f, 1.0f), nullptr);
  for (int i = 0; i < 30; ++i) {
    world.update(1.0f / 60.0f);
    bear.update(1.0f / 60.0f, {});
  }
  auto frontmost = [&](SkinnedModel::Surface surface) {
    float front = -1e9f;
    for (std::size_t i : verticesOf(model, surface)) {
      front = std::max(front, bear.getPositions()[i].z);
    }
    return front;
  };

  // At rest the tongue and the teeth are inside of the closed mouth, behind the
  // nose
  const float nose = frontmost(SkinnedModel::NOSE);
  CHECK(frontmost(SkinnedModel::TONGUE) < nose - 0.1f);
  CHECK(frontmost(SkinnedModel::TEETH) < nose - 0.05f);

  SECTION("Stretching: the tongue comes out over the lower lip") {
    bear.startIdle(BearRig::IDLE_STRETCH);
    float tongueFront = -1e9f;
    for (int i = 0; i < 6 * 60; ++i) {
      world.update(1.0f / 60.0f);
      bear.update(1.0f / 60.0f, {});
      if (bear.getRig().getFace().tongueOut > 0.99f) {
        tongueFront = std::max(tongueFront, frontmost(SkinnedModel::TONGUE));
      }
    }
    REQUIRE(tongueFront > -1e9f);
    CHECK(tongueFront > frontmost(SkinnedModel::MOUTH));  // out of the mouth
  }
  SECTION("Yawning opens the mouth wide") {
    bear.startIdle(BearRig::IDLE_YAWN);
    float widest = 0.0f;
    for (int i = 0; i < 4 * 60; ++i) {
      world.update(1.0f / 60.0f);
      bear.update(1.0f / 60.0f, {});
      widest = std::max(widest, bear.getRig().getFace().jawOpen);
    }
    CHECK(widest > 0.95f);
  }
}

#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

#include "Resources/Model.hpp"
#include "TestSupport.hpp"
#include "Util/Util.hpp"

namespace util = bibo::util;
using bibo::resources::Model;
using bibo::test::IsNear;
using glm::mat4;
using glm::vec3;
using glm::vec4;

namespace {
// Reproducible with --rng-seed
std::mt19937& RandomEngine() {
  static std::mt19937 engine(Catch::getSeed());
  return engine;
}

aiMatrix4x4 RandomMatrix(bool affine) {
  std::uniform_real_distribution<float> value(-10.0f, 10.0f);
  aiMatrix4x4 m;
  for (unsigned int row = 0; row < 4; ++row) {
    for (unsigned int column = 0; column < 4; ++column) {
      m[row][column] = value(RandomEngine());
    }
  }
  if (affine) {
    m.d1 = m.d2 = m.d3 = 0.0f;
    m.d4 = 1.0f;
  }
  return m;
}
}  // namespace

TEST_CASE("Assimp matrices are converted to GLM matrices", "[math]") {
  SECTION("Identity") {
    CHECK(util::ConvertMatrix(aiMatrix4x4()) == mat4(1.0f));
  }

  SECTION("The translation ends up in the last column of the GLM matrix") {
    aiMatrix4x4 translation;
    aiMatrix4x4::Translation(aiVector3D(1.0f, 2.0f, 3.0f), translation);
    mat4 converted = util::ConvertMatrix(translation);
    CHECK(converted[3] == vec4(1.0f, 2.0f, 3.0f, 1.0f));
    CHECK(converted == glm::translate(mat4(1.0f), vec3(1.0f, 2.0f, 3.0f)));
  }

  SECTION("A rotation rotates in the same direction") {
    aiMatrix4x4 rotation;
    aiMatrix4x4::RotationZ(glm::radians(90.0f), rotation);
    CHECK_THAT(
        vec3(util::ConvertMatrix(rotation) * vec4(1.0f, 0.0f, 0.0f, 1.0f)),
        IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-6f));
  }

  SECTION("Transforms points like the Assimp matrix") {
    std::uniform_real_distribution<float> coordinate(-100.0f, 100.0f);
    for (int i = 0; i < 100; ++i) {
      aiMatrix4x4 m = RandomMatrix(true);
      aiVector3D point(coordinate(RandomEngine()), coordinate(RandomEngine()),
                       coordinate(RandomEngine()));
      aiVector3D expected = m * point;

      vec3 actual =
          vec3(util::ConvertMatrix(m) * vec4(point.x, point.y, point.z, 1.0f));
      CAPTURE(i);
      REQUIRE_THAT(actual,
                   IsNear(vec3(expected.x, expected.y, expected.z), 1e-2f));
    }
  }

  SECTION("Converting a product is the product of the converted matrices") {
    for (int i = 0; i < 100; ++i) {
      aiMatrix4x4 a = RandomMatrix(false);
      aiMatrix4x4 b = RandomMatrix(false);
      CAPTURE(i);
      REQUIRE_THAT(
          util::ConvertMatrix(a * b),
          IsNear(util::ConvertMatrix(a) * util::ConvertMatrix(b), 1e-3f));
    }
  }
}

TEST_CASE(
    "The global transformation of a node includes the transformations of all "
    "its parents",
    "[math]") {
  aiNode root("root");
  aiMatrix4x4::Scaling(aiVector3D(2.0f), root.mTransformation);

  auto child = new aiNode("child");
  aiMatrix4x4::RotationZ(glm::radians(90.0f), child->mTransformation);
  root.addChildren(1, &child);

  auto grand_child = new aiNode("grandChild");
  aiMatrix4x4::Translation(aiVector3D(1.0f, 0.0f, 0.0f),
                           grand_child->mTransformation);
  child->addChildren(1, &grand_child);

  SECTION("Root node") {
    aiMatrix4x4 result = RandomMatrix(false);  // the previous value is ignored
    Model::TransformNode(&result, &root);
    CHECK(result == root.mTransformation);
  }

  SECTION("Nested node: parent transformations are applied after the own one") {
    aiMatrix4x4 result = RandomMatrix(false);
    Model::TransformNode(&result, grand_child);
    CHECK(result.Equal(root.mTransformation * child->mTransformation *
                           grand_child->mTransformation,
                       1e-6f));

    // Translated by 1 along x, rotated to the y axis and scaled by 2
    vec3 origin =
        vec3(util::ConvertMatrix(result) * vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK_THAT(origin, IsNear(vec3(0.0f, 2.0f, 0.0f), 1e-5f));
  }
}

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "Scene/Fur.hpp"
#include "TestSupport.hpp"

using bibo::fur::CombDirections;
using bibo::fur::Environment;
using bibo::fur::GenerateStrands;
using bibo::fur::GuideHairs;
using bibo::fur::SphereCollider;
using bibo::fur::Strand;

using Catch::Matchers::WithinAbs;
using glm::vec3;
using glm::vec4;

namespace {
const float kFrame = 1.0f / 60.0f;
const float kLength = 0.1f;

/**
 * Patch of skin: a grid of vertices in the xz plane with the same normal, every
 * vertex has a guide hair
 */
struct Patch {
  std::vector<vec3> positions, normals;
  std::vector<float> lengths;
  GuideHairs hairs;
  Environment environment;

  explicit Patch(vec3 normal = vec3(0.0f, 1.0f, 0.0f),
                 vec3 origin = vec3(0.0f)) {
    for (int x = 0; x < 5; ++x) {
      for (int z = 0; z < 5; ++z) {
        positions.push_back(origin + vec3(0.1f * x, 0.0f, 0.1f * z));
        normals.push_back(normal);
        lengths.push_back(kLength);
      }
    }
    hairs.Reset(positions);
  }

  void Move(vec3 offset) {
    for (vec3& position : positions) {
      position += offset;
    }
  }

  void Run(float seconds, vec3 velocity = vec3(0.0f)) {
    int frames = static_cast<int>(std::lround(seconds / kFrame));
    for (int i = 0; i < frames; ++i) {
      Move(velocity * kFrame);
      environment.time += kFrame;
      hairs.Update(kFrame, positions, normals, lengths, environment);
    }
  }

  vec3 AverageOffset() const {
    vec3 sum(0.0f);
    for (const vec3& offset : hairs.GetOffsets()) {
      sum += offset;
    }
    return sum / static_cast<float>(hairs.GetOffsets().size());
  }

  vec3 Tip(std::size_t i) const {
    return positions[i] + (normals[i] + hairs.GetOffsets()[i]) * lengths[i];
  }
};
}  // namespace

TEST_CASE("Fur on a resting body stands still", "[fur]") {
  Patch patch;
  patch.Run(2.0f);

  for (const vec3& offset : patch.hairs.GetOffsets()) {
    // on top of the body the fur does not hang to a side
    CHECK(glm::length(offset) < 0.05f);
  }

  SECTION("On a side of the body the fur hangs down") {
    Patch side(vec3(1.0f, 0.0f, 0.0f));
    side.Run(2.0f);
    CHECK(side.AverageOffset().y < -0.15f);
    CHECK(side.AverageOffset().y > -0.5f);
  }
}

TEST_CASE(
    "Fur lags behind when the body accelerates and swings back when it stops",
    "[fur]") {
  Patch patch;
  patch.Run(0.5f);

  // Speeding up to 10 units per second along +x in a quarter of a second, like
  // the bear
  for (int i = 0; i < 15; ++i) {
    patch.Run(kFrame, vec3(40.0f * kFrame * static_cast<float>(i), 0.0f, 0.0f));
  }
  CHECK(patch.AverageOffset().x < -0.2f);

  SECTION("At a constant speed it straightens again") {
    patch.Run(2.0f, vec3(10.0f, 0.0f, 0.0f));
    CHECK(std::abs(patch.AverageOffset().x) < 0.05f);

    SECTION("Stopping swings it forward") {
      patch.Run(kFrame, vec3(0.0f));
      patch.Run(0.05f);
      CHECK(patch.AverageOffset().x > 0.2f);
      patch.Run(3.0f);
      CHECK(glm::length(patch.AverageOffset()) < 0.05f);
    }
  }
}

TEST_CASE("Fur tips never go below the skin and do not bend too far", "[fur]") {
  Patch patch;
  patch.Run(0.2f);
  // A violent shake: huge accelerations in all directions
  for (int i = 0; i < 120; ++i) {
    float s = std::sin(static_cast<float>(i) * 1.7f);
    patch.Run(kFrame, vec3(60.0f * s, -80.0f * s,
                           30.0f * std::cos(static_cast<float>(i))));
    for (std::size_t v = 0; v < patch.positions.size(); ++v) {
      const vec3& offset = patch.hairs.GetOffsets()[v];
      REQUIRE(std::isfinite(offset.x));
      CHECK(1.0f + glm::dot(offset, patch.normals[v]) >= 0.149f);
      CHECK(glm::length(offset) <= 1.4001f);
    }
  }
}

TEST_CASE(
    "Fur lies on the ground and on obstacles instead of going through them",
    "[fur]") {
  SECTION("Belly on the ground: the fur points down and is flattened") {
    Patch patch(vec3(0.0f, -1.0f, 0.0f), vec3(0.0f, 0.04f, 0.0f));
    patch.environment.has_ground = true;
    patch.environment.ground_point = vec3(0.0f);
    patch.environment.ground_normal = vec3(0.0f, 1.0f, 0.0f);
    patch.Run(1.0f);
    for (std::size_t i = 0; i < patch.positions.size(); ++i) {
      CHECK(patch.Tip(i).y >= -1e-4f);
    }
  }
  SECTION("A rock next to the skin pushes the fur aside") {
    Patch patch;
    const SphereCollider rock{vec3(0.2f, 0.25f, 0.2f), 0.2f};
    patch.environment.spheres.push_back(rock);
    patch.Run(1.0f);
    for (std::size_t i = 0; i < patch.positions.size(); ++i) {
      CHECK(glm::distance(patch.Tip(i), rock.center) >= rock.radius - 1e-4f);
    }
  }
}

TEST_CASE("A hit ruffles the fur where it happens", "[fur]") {
  Patch patch;
  patch.Run(0.5f);
  patch.hairs.AddImpulse(vec3(0.0f), vec3(6.0f, 2.0f, 0.0f), 0.25f);
  patch.Run(2.0f * kFrame);

  // the corner where the hit was
  float near = glm::length(patch.hairs.GetOffsets()[0]);
  // the opposite corner, 0.57 away
  float far = glm::length(patch.hairs.GetOffsets().back());
  CHECK(near > 0.1f);
  CHECK(far < 0.01f);
}

TEST_CASE("Bare skin has no fur to move", "[fur]") {
  Patch patch;
  std::fill(patch.lengths.begin(), patch.lengths.end(), 0.0f);
  patch.Run(0.3f, vec3(20.0f, 0.0f, 0.0f));
  for (const vec3& offset : patch.hairs.GetOffsets()) {
    CHECK(offset == vec3(0.0f));
  }
}

TEST_CASE("Strands grow where there is fur, more on large triangles", "[fur]") {
  // Two triangles with fur (one four times as large) and one without
  const std::vector<vec3> positions = {vec3(0.0f),
                                       vec3(1.0f, 0.0f, 0.0f),
                                       vec3(0.0f, 0.0f, 1.0f),
                                       vec3(2.0f, 0.0f, 0.0f),
                                       vec3(4.0f, 0.0f, 0.0f),
                                       vec3(2.0f, 0.0f, 2.0f),
                                       vec3(5.0f, 0.0f, 0.0f),
                                       vec3(6.0f, 0.0f, 0.0f),
                                       vec3(5.0f, 0.0f, 1.0f)};
  std::vector<vec4> colors(positions.size(), vec4(0.4f, 0.3f, 0.2f, 0.8f));
  colors[6].a = colors[7].a = colors[8].a = 0.0f;
  const std::vector<unsigned int> triangles = {0, 1, 2, 3, 4, 5, 6, 7, 8};

  std::vector<Strand> strands =
      GenerateStrands(positions, colors, triangles, 10000, 42);

  REQUIRE(strands.size() == 10000);
  int on_small = 0, on_large = 0;
  for (const Strand& strand : strands) {
    CHECK(strand.vertices[0] != 6);  // no strands without fur
    on_small += strand.vertices[0] == 0;
    on_large += strand.vertices[0] == 3;
    CHECK(strand.barycentric[0] >= 0.0f);
    CHECK(strand.barycentric[1] >= 0.0f);
    CHECK(strand.barycentric[0] + strand.barycentric[1] <= 1.0f + 1e-6f);
    CHECK(strand.length_scale >= 0.7f);
    CHECK(strand.length_scale <= 1.15f);
  }
  CHECK_THAT(static_cast<double>(on_large) / on_small, WithinAbs(4.0, 0.4));

  SECTION(
      "The same seed gives the same strands (the fur does not change between "
      "runs)") {
    std::vector<Strand> again =
        GenerateStrands(positions, colors, triangles, 100, 42);
    for (std::size_t i = 0; i < again.size(); ++i) {
      CHECK(again[i].seed == strands[i].seed);
      CHECK(again[i].barycentric[0] == strands[i].barycentric[0]);
    }
  }
  SECTION(
      "The layout of a strand matches the shader storage buffer of the fur "
      "shader (uvec4 + vec4)") {
    STATIC_CHECK(sizeof(Strand) == 32);
  }
  SECTION("No fur at all") {
    std::vector<vec4> bald(positions.size(), vec4(0.4f, 0.3f, 0.2f, 0.0f));
    CHECK(GenerateStrands(positions, bald, triangles, 100, 1).empty());
  }
}

TEST_CASE("The fur is combed backwards and down along the surface", "[fur]") {
  const vec3 forward(0.0f, 0.0f, 1.0f), up(0.0f, 1.0f, 0.0f);
  std::vector<vec3> normals = {up,
                               vec3(1.0f, 0.0f, 0.0f),
                               vec3(-1.0f, 0.0f, 0.0f),
                               glm::normalize(vec3(0.3f, -0.8f, 0.5f)),
                               forward,
                               -forward};

  std::vector<vec3> combs = CombDirections(normals, forward, up);

  REQUIRE(combs.size() == normals.size());
  for (std::size_t i = 0; i < normals.size(); ++i) {
    CAPTURE(i);
    CHECK_THAT(glm::length(combs[i]), WithinAbs(1.0f, 1e-4f));
    CHECK_THAT(glm::dot(combs[i], normals[i]),
               WithinAbs(0.0f, 1e-4f));  // along the surface
  }
  CHECK(combs[0].z < -0.9f);  // on the back: backwards
  CHECK(combs[1].z < -0.5f);  // on the sides: backwards and down
  CHECK(combs[1].y < -0.2f);
  CHECK(combs[4].y < -0.9f);  // on the face: down
}

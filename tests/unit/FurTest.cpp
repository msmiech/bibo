#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "Scene/Fur.hpp"
#include "TestSupport.hpp"

using namespace bibo::fur;
using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::vec3;
using glm::vec4;

namespace {
const float FRAME = 1.0f / 60.0f;
const float LENGTH = 0.1f;

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
        lengths.push_back(LENGTH);
      }
    }
    hairs.reset(positions);
  }

  void move(vec3 offset) {
    for (vec3& position : positions) {
      position += offset;
    }
  }

  void run(float seconds, vec3 velocity = vec3(0.0f)) {
    int frames = static_cast<int>(std::lround(seconds / FRAME));
    for (int i = 0; i < frames; ++i) {
      move(velocity * FRAME);
      environment.time += FRAME;
      hairs.update(FRAME, positions, normals, lengths, environment);
    }
  }

  vec3 averageOffset() const {
    vec3 sum(0.0f);
    for (const vec3& offset : hairs.getOffsets()) {
      sum += offset;
    }
    return sum / static_cast<float>(hairs.getOffsets().size());
  }

  vec3 tip(std::size_t i) const {
    return positions[i] + (normals[i] + hairs.getOffsets()[i]) * lengths[i];
  }
};
}  // namespace

TEST_CASE("Fur on a resting body stands still", "[fur]") {
  Patch patch;
  patch.run(2.0f);

  for (const vec3& offset : patch.hairs.getOffsets()) {
    CHECK(glm::length(offset) <
          0.05f);  // on top of the body the fur does not hang to a side
  }

  SECTION("On a side of the body the fur hangs down") {
    Patch side(vec3(1.0f, 0.0f, 0.0f));
    side.run(2.0f);
    CHECK(side.averageOffset().y < -0.15f);
    CHECK(side.averageOffset().y > -0.5f);
  }
}

TEST_CASE(
    "Fur lags behind when the body accelerates and swings back when it stops",
    "[fur]") {
  Patch patch;
  patch.run(0.5f);

  // Speeding up to 10 units per second along +x in a quarter of a second, like
  // the bear
  for (int i = 0; i < 15; ++i) {
    patch.run(FRAME, vec3(40.0f * FRAME * static_cast<float>(i), 0.0f, 0.0f));
  }
  CHECK(patch.averageOffset().x < -0.2f);

  SECTION("At a constant speed it straightens again") {
    patch.run(2.0f, vec3(10.0f, 0.0f, 0.0f));
    CHECK(std::abs(patch.averageOffset().x) < 0.05f);

    SECTION("Stopping swings it forward") {
      patch.run(FRAME, vec3(0.0f));
      patch.run(0.05f);
      CHECK(patch.averageOffset().x > 0.2f);
      patch.run(3.0f);
      CHECK(glm::length(patch.averageOffset()) < 0.05f);
    }
  }
}

TEST_CASE("Fur tips never go below the skin and do not bend too far", "[fur]") {
  Patch patch;
  patch.run(0.2f);
  // A violent shake: huge accelerations in all directions
  for (int i = 0; i < 120; ++i) {
    float s = std::sin(static_cast<float>(i) * 1.7f);
    patch.run(FRAME, vec3(60.0f * s, -80.0f * s,
                          30.0f * std::cos(static_cast<float>(i))));
    for (std::size_t v = 0; v < patch.positions.size(); ++v) {
      const vec3& offset = patch.hairs.getOffsets()[v];
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
    patch.environment.hasGround = true;
    patch.environment.groundPoint = vec3(0.0f);
    patch.environment.groundNormal = vec3(0.0f, 1.0f, 0.0f);
    patch.run(1.0f);
    for (std::size_t i = 0; i < patch.positions.size(); ++i) {
      CHECK(patch.tip(i).y >= -1e-4f);
    }
  }
  SECTION("A rock next to the skin pushes the fur aside") {
    Patch patch;
    const SphereCollider rock{vec3(0.2f, 0.25f, 0.2f), 0.2f};
    patch.environment.spheres.push_back(rock);
    patch.run(1.0f);
    for (std::size_t i = 0; i < patch.positions.size(); ++i) {
      CHECK(glm::distance(patch.tip(i), rock.center) >= rock.radius - 1e-4f);
    }
  }
}

TEST_CASE("A hit ruffles the fur where it happens", "[fur]") {
  Patch patch;
  patch.run(0.5f);
  patch.hairs.addImpulse(vec3(0.0f), vec3(6.0f, 2.0f, 0.0f), 0.25f);
  patch.run(2.0f * FRAME);

  float near =
      glm::length(patch.hairs.getOffsets()[0]);  // the corner where the hit was
  float far = glm::length(
      patch.hairs.getOffsets().back());  // the opposite corner, 0.57 away
  CHECK(near > 0.1f);
  CHECK(far < 0.01f);
}

TEST_CASE("Bare skin has no fur to move", "[fur]") {
  Patch patch;
  std::fill(patch.lengths.begin(), patch.lengths.end(), 0.0f);
  patch.run(0.3f, vec3(20.0f, 0.0f, 0.0f));
  for (const vec3& offset : patch.hairs.getOffsets()) {
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
      generateStrands(positions, colors, triangles, 10000, 42);

  REQUIRE(strands.size() == 10000);
  int onSmall = 0, onLarge = 0;
  for (const Strand& strand : strands) {
    CHECK(strand.vertices[0] != 6);  // no strands without fur
    onSmall += strand.vertices[0] == 0;
    onLarge += strand.vertices[0] == 3;
    CHECK(strand.barycentric[0] >= 0.0f);
    CHECK(strand.barycentric[1] >= 0.0f);
    CHECK(strand.barycentric[0] + strand.barycentric[1] <= 1.0f + 1e-6f);
    CHECK(strand.lengthScale >= 0.7f);
    CHECK(strand.lengthScale <= 1.15f);
  }
  CHECK_THAT(static_cast<double>(onLarge) / onSmall, WithinAbs(4.0, 0.4));

  SECTION(
      "The same seed gives the same strands (the fur does not change between "
      "runs)") {
    std::vector<Strand> again =
        generateStrands(positions, colors, triangles, 100, 42);
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
    CHECK(generateStrands(positions, bald, triangles, 100, 1).empty());
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

  std::vector<vec3> combs = combDirections(normals, forward, up);

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

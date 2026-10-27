#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <vector>

#include "Render/FrameUniforms.hpp"
#include "Resources/Shader.hpp"
#include "World/FoliageInteraction.hpp"
#include "World/VegetationRenderer.hpp"

using bibo::world::FoliageInteraction;
using glm::vec3;

namespace {
const float kStep = 1.0f / 30.0f;

/**
 * A body walks along +x at a speed for a time
 */
void Walk(FoliageInteraction& foliage, vec3& position, float speed,
          float seconds, float radius = 1.0f) {
  for (float t = 0.0f; t < seconds; t += kStep) {
    position.x += speed * kStep;
    foliage.Update(kStep, {{position, radius}}, vec3(0.0f));
  }
}
}  // namespace

TEST_CASE(
    "Plants stay pressed down for a moment after a body left, then swing back "
    "past upright and settle",
    "[foliage]") {
  CHECK(FoliageInteraction::Push(-1.0f) == 1.0f);
  CHECK(FoliageInteraction::Push(0.0f) == 1.0f);
  CHECK(FoliageInteraction::Push(FoliageInteraction::kHoldTime - 0.01f) ==
        1.0f);
  CHECK(FoliageInteraction::Push(FoliageInteraction::kHoldTime +
                                 FoliageInteraction::kSpringTime) == 0.0f);
  CHECK(FoliageInteraction::Push(100.0f) == 0.0f);
  float lowest = 1.0f;
  float previous = 1.0f, largest_step = 0.0f;
  for (float age = 0.0f; age < FoliageInteraction::kHoldTime +
                                   FoliageInteraction::kSpringTime + 0.1f;
       age += 0.01f) {
    float push = FoliageInteraction::Push(age);
    CHECK(std::abs(push) <= 1.0f);
    lowest = std::min(lowest, push);
    largest_step = std::max(largest_step, std::abs(push - previous));
    previous = push;
  }
  // Past upright, but not by much, and without jumps
  CHECK(lowest < -0.05f);
  CHECK(lowest > -0.5f);
  CHECK(largest_step < 0.1f);
}

TEST_CASE(
    "A walking body leaves imprints behind that fade away, a body that stands "
    "leaves none",
    "[foliage]") {
  FoliageInteraction foliage;
  vec3 position(0.0f);
  Walk(foliage, position, 0.0f, 2.0f);
  CHECK(foliage.GetTrailLength() == 0);
  REQUIRE(foliage.GetSelected().size() == 1);
  CHECK(foliage.GetSelected()[0].age < 0.0f);  // the body itself

  Walk(foliage, position, 4.0f, 2.0f);
  // An imprint every half radius
  CHECK(foliage.GetTrailLength() >= 14);
  CHECK(foliage.GetTrailLength() <= 17);
  // The plants lean in the direction of the walk
  CHECK(foliage.GetSelected()[0].motion.x > 0.3f);
  CHECK_THAT(foliage.GetSelected()[0].motion.y,
             Catch::Matchers::WithinAbs(0.0f, 0.01f));
  // The body first, then the imprints, at most as many as the shaders take
  CHECK(foliage.GetSelected().size() ==
        static_cast<std::size_t>(bibo::render::kMaxFoliageInteractors));
  for (std::size_t i = 1; i < foliage.GetSelected().size(); ++i) {
    CHECK(foliage.GetSelected()[i].age >= 0.0f);
    CHECK(foliage.GetSelected()[i].center.x < position.x);
  }

  Walk(foliage, position, 0.0f,
       FoliageInteraction::kHoldTime + FoliageInteraction::kSpringTime + 0.1f);
  CHECK(foliage.GetTrailLength() == 0);
}

TEST_CASE(
    "A body that is moved far away in one step leaves no trail on the way",
    "[foliage]") {
  FoliageInteraction foliage;
  foliage.Update(kStep, {{vec3(0.0f), 1.0f}}, vec3(0.0f));
  foliage.Update(kStep, {{vec3(0.0f), 1.0f}}, vec3(0.0f));
  foliage.Update(kStep, {{vec3(30.0f, 0.0f, 0.0f), 1.0f}}, vec3(0.0f));
  foliage.Update(kStep, {{vec3(30.0f, 0.0f, 0.0f), 1.0f}}, vec3(0.0f));
  // Only where it was: the plants there spring back up
  REQUIRE(foliage.GetTrailLength() == 1);
  for (const FoliageInteraction::Imprint& imprint : foliage.GetSelected()) {
    if (imprint.age >= 0.0f) {
      CHECK(imprint.center == vec3(0.0f));
    } else {
      CHECK(imprint.motion == glm::vec2(0.0f));
    }
  }
}

TEST_CASE(
    "Only the bodies close to the camera bend the plants, they go to the "
    "shaders with the frame",
    "[foliage]") {
  FoliageInteraction foliage;
  std::vector<FoliageInteraction::Body> bodies = {
      {vec3(5.0f, 1.0f, 0.0f), 1.3f},
      {vec3(500.0f, 0.0f, 0.0f), 0.5f},
      {vec3(0.0f, 0.0f, 20.0f), 0.5f}};
  foliage.Update(kStep, bodies, vec3(0.0f));
  REQUIRE(foliage.GetSelected().size() == 2);

  bibo::render::FrameUniforms uniforms;
  uniforms.wind = glm::vec4(0.8f, 0.6f, 1.0f, 0.0f);
  foliage.FillFrameUniforms(uniforms);
  CHECK(uniforms.wind ==
        glm::vec4(0.8f, 0.6f, 1.0f,
                  2.0f));  // the number of bodies, the wind stays
  CHECK(uniforms.foliage_interactors[0] == glm::vec4(5.0f, 1.0f, 0.0f, 1.3f));
  CHECK(uniforms.foliage_interactors[1] == glm::vec4(0.0f, 0.0f, 20.0f, 0.5f));
  CHECK(uniforms.foliage_pushes[0].x == 1.0f);
}

TEST_CASE(
    "The vegetation fades between its levels of detail around the distances "
    "where they change",
    "[vegetation]") {
  const float distances[2] = {50.0f, 150.0f};
  using bibo::world::BlendLod;
  CHECK(BlendLod(10.0f, distances, 3, 0.2f).lod == 0);
  CHECK(BlendLod(10.0f, distances, 3, 0.2f).next == 0.0f);
  CHECK(BlendLod(44.9f, distances, 3, 0.2f).next == 0.0f);
  CHECK(BlendLod(50.0f, distances, 3, 0.2f).lod == 0);
  CHECK_THAT(BlendLod(50.0f, distances, 3, 0.2f).next,
             Catch::Matchers::WithinAbs(0.5f, 1e-5f));
  CHECK(BlendLod(56.0f, distances, 3, 0.2f).lod == 1);
  CHECK(BlendLod(56.0f, distances, 3, 0.2f).next == 0.0f);
  CHECK_THAT(BlendLod(150.0f, distances, 3, 0.2f).next,
             Catch::Matchers::WithinAbs(0.5f, 1e-5f));
  CHECK(BlendLod(1000.0f, distances, 3, 0.2f).lod == 2);
  // A model with fewer levels never fades into one it does not have
  CHECK(BlendLod(50.0f, distances, 1, 0.2f).lod == 0);
  CHECK(BlendLod(50.0f, distances, 1, 0.2f).next == 0.0f);
  CHECK(BlendLod(150.0f, distances, 2, 0.2f).lod == 1);
  CHECK(BlendLod(150.0f, distances, 2, 0.2f).next == 0.0f);
  // Continuous: the fade grows steadily through the band
  float previous = 0.0f;
  for (float distance = 45.0f; distance < 55.0f; distance += 0.5f) {
    bibo::world::LodBlend blend = BlendLod(distance, distances, 3, 0.2f);
    REQUIRE(blend.lod == 0);
    CHECK(blend.next >= previous);
    previous = blend.next;
  }
}

TEST_CASE("Variants of a shader get their defines after the version line",
          "[shader]") {
  std::string code = "#version 430 core\nvoid main() {}\n";
  CHECK(bibo::Shader::AddDefines(code, "") == code);
  CHECK(bibo::Shader::AddDefines(code, "LOD_DITHER") ==
        "#version 430 core\n#define LOD_DITHER\nvoid main() {}\n");
  CHECK(bibo::Shader::AddDefines(code, " A  B ") ==
        "#version 430 core\n#define A\n#define B\nvoid main() {}\n");
  CHECK(bibo::Shader::AddDefines("void main() {}\n", "A") ==
        "#define A\nvoid main() {}\n");
}

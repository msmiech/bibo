#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestSupport.hpp"
#include "Util/Input.hpp"

using bibo::input::ApplyDeadZone;
using bibo::input::CameraRelative;

using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::vec2;
using glm::vec3;

TEST_CASE(
    "Dead zone of the sticks: rest positions are ignored, the rest is "
    "stretched to the full range",
    "[input]") {
  const float inner = 0.15f, outer = 0.95f;

  CHECK(ApplyDeadZone(vec2(0.0f), inner, outer) == vec2(0.0f));
  // a stick at rest is never exactly at 0
  CHECK(ApplyDeadZone(vec2(0.1f, -0.05f), inner, outer) == vec2(0.0f));
  CHECK_THAT(ApplyDeadZone(vec2(0.55f, 0.0f), inner, outer),
             IsNear(vec2(0.5f, 0.0f), 1e-5f));
  CHECK_THAT(ApplyDeadZone(vec2(0.0f, -0.97f), inner, outer),
             IsNear(vec2(0.0f, -1.0f), 1e-5f));  // fully pushed
  CHECK_THAT(
      ApplyDeadZone(vec2(1.0f, 1.0f), inner, outer),
      IsNear(glm::normalize(vec2(1.0f, 1.0f)), 1e-5f));  // never longer than 1

  // The direction stays the same (radial dead zone)
  vec2 diagonal = ApplyDeadZone(vec2(0.3f, 0.4f), inner, outer);
  CHECK_THAT(diagonal.x / diagonal.y, WithinAbs(0.75f, 1e-5f));
}

TEST_CASE("The bear moves relative to the camera", "[input]") {
  const vec3 forward(0.0f, 0.0f, -1.0f),
      right(1.0f, 0.0f, 0.0f);  // the camera looks along -z

  CHECK_THAT(CameraRelative(vec2(0.0f, 1.0f), forward, right),
             IsNear(vec3(0.0f, 0.0f, -1.0f), 1e-6f));
  CHECK_THAT(CameraRelative(vec2(-1.0f, 0.0f), forward, right),
             IsNear(vec3(-1.0f, 0.0f, 0.0f), 1e-6f));
  CHECK_THAT(
      CameraRelative(vec2(0.0f, -0.5f), forward, right),
      IsNear(vec3(0.0f, 0.0f, 0.5f), 1e-6f));  // slowly towards the camera
  CHECK_THAT(glm::length(CameraRelative(glm::normalize(vec2(1.0f, 1.0f)),
                                        forward, right)),
             WithinAbs(1.0f, 1e-5f));
  CHECK(CameraRelative(vec2(0.0f), forward, right) == vec3(0.0f));
}

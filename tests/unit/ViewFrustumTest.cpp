#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

#include "Resources/Plane.hpp"
#include "Scene/ViewFrustum.hpp"
#include "TestSupport.hpp"

using bibo::test::Clipped;
using bibo::test::IsNear;

using bibo::resources::Plane;
using bibo::scene::ViewFrustum;
using bibo::test::Clip;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const vec3 kUp(0.0f, 1.0f, 0.0f);
// Camera of the game (Camera): 60 degrees vertical field of view, 0.1 to 200
// units
const float kFieldOfView = 60.0f, kAspectRatio = 4.0f / 3.0f, kNear = 0.1f,
            kFar = 200.0f;

ViewFrustum Frustum(vec3 position, vec3 look_at,
                    float aspect_ratio = kAspectRatio) {
  ViewFrustum frustum;
  frustum.SetCamInternals(kFieldOfView, aspect_ratio, kNear, kFar);
  frustum.SetCamDef(position, look_at, kUp);
  return frustum;
}
}  // namespace

TEST_CASE(
    "A plane through three points faces the side from which they are "
    "counter-clockwise",
    "[math][frustum]") {
  // Counter-clockwise seen from +z, in the plane z = 2
  Plane plane(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, 2.0f),
              vec3(1.0f, 0.0f, 2.0f));

  CHECK_THAT(plane.normal, IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-6f));
  CHECK_THAT(plane.Distance(vec3(5.0f, -3.0f, 3.0f)), WithinAbs(1.0f, 1e-6f));
  CHECK_THAT(plane.Distance(vec3(0.0f)), WithinAbs(-2.0f, 1e-6f));

  SECTION("Clockwise points face the other side") {
    Plane flipped(vec3(1.0f, 0.0f, 2.0f), vec3(0.0f, 0.0f, 2.0f),
                  vec3(0.0f, 1.0f, 2.0f));
    CHECK_THAT(flipped.normal, IsNear(vec3(0.0f, 0.0f, -1.0f), 1e-6f));
  }

  SECTION("The normal has unit length for any triangle") {
    Plane tilted(vec3(10.0f, 3.0f, -7.0f), vec3(-4.0f, 0.5f, 2.0f),
                 vec3(6.0f, -8.0f, 1.0f));
    CHECK_THAT(glm::length(tilted.normal), WithinAbs(1.0f, 1e-6f));
    // The points lie on the plane
    CHECK_THAT(tilted.Distance(vec3(10.0f, 3.0f, -7.0f)),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(tilted.Distance(vec3(6.0f, -8.0f, 1.0f)),
               WithinAbs(0.0f, 1e-4f));
  }
}

TEST_CASE("A plane through a point with a normal of any length",
          "[math][frustum]") {
  Plane plane;
  plane.SetNormalAndPoint(vec3(0.0f, 2.0f, 0.0f), vec3(4.0f, 3.0f, -1.0f));

  CHECK_THAT(plane.normal, IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-6f));
  CHECK_THAT(plane.Distance(vec3(0.0f, 5.0f, 0.0f)), WithinAbs(2.0f, 1e-6f));
}

TEST_CASE(
    "A plane from the coefficients of its equation measures euclidean "
    "distances",
    "[math][frustum]") {
  // a * x + b * y + c * z + d = 0 with a normal that is not normalized
  auto [a, b, c, d, point, expected_distance] =
      GENERATE(table<float, float, float, float, vec3, float>(
          {{0.0f, 0.0f, 2.0f, -4.0f, vec3(7.0f, 1.0f, 5.0f), 3.0f},  // z = 2
           {3.0f, 4.0f, 0.0f, 10.0f, vec3(0.0f), 2.0f},
           {1.0f, 0.0f, 0.0f, 0.0f, vec3(-2.5f, 9.0f, 9.0f), -2.5f}}));
  CAPTURE(a, b, c, d, point);
  Plane plane;

  plane.SetCoefficients(a, b, c, d);

  CHECK_THAT(glm::length(plane.normal), WithinAbs(1.0f, 1e-6f));
  CHECK_THAT(plane.Distance(point), WithinAbs(expected_distance, 1e-5f));
}

TEST_CASE("Points are inside of the view frustum between its six planes",
          "[math][frustum]") {
  const ViewFrustum view = Frustum(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f));
  // At a distance of 10 the visible area is 2 * 10 * tan(30 degrees) = 11.55
  // units high and 4/3 of that wide
  const float half_height = 10.0f * std::tan(glm::radians(30.0f));
  const float half_width = half_height * kAspectRatio;

  auto [point, expected] = GENERATE_COPY(table<vec3, ViewFrustum::Result>(
      {{vec3(0.0f, 0.0f, -10.0f), ViewFrustum::kInside},  // ahead
       {vec3(0.0f, 0.0f, 10.0f), ViewFrustum::kOutside},  // behind
       {vec3(0.0f, 0.0f, -0.05f),
        ViewFrustum::kOutside},  // in front of the near plane
       {vec3(0.0f, 0.0f, -199.0f), ViewFrustum::kInside},
       {vec3(0.0f, 0.0f, -201.0f),
        ViewFrustum::kOutside},  // behind the far plane
       {vec3(0.0f, 0.99f * half_height, -10.0f), ViewFrustum::kInside},
       {vec3(0.0f, 1.01f * half_height, -10.0f),
        ViewFrustum::kOutside},  // above
       {vec3(0.0f, -1.01f * half_height, -10.0f),
        ViewFrustum::kOutside},  // below
       {vec3(0.99f * half_width, 0.0f, -10.0f), ViewFrustum::kInside},
       {vec3(1.01f * half_width, 0.0f, -10.0f),
        ViewFrustum::kOutside},  // right
       {vec3(-0.99f * half_width, 0.0f, -10.0f), ViewFrustum::kInside},
       {vec3(-1.01f * half_width, 0.0f, -10.0f),
        ViewFrustum::kOutside}}));  // left
  CAPTURE(point);

  CHECK(view.PointInFrustum(point) == expected);
}

TEST_CASE("Spheres are inside, intersecting or outside of the view frustum",
          "[math][frustum]") {
  const ViewFrustum view = Frustum(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f));
  const float half_width = 10.0f * std::tan(glm::radians(30.0f)) * kAspectRatio;

  CHECK(view.SphereInFrustum(vec3(0.0f, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::kInside);
  // on the right border
  CHECK(view.SphereInFrustum(vec3(half_width, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::kIntersect);
  CHECK(view.SphereInFrustum(vec3(half_width + 2.0f, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::kOutside);
  // behind the camera
  CHECK(view.SphereInFrustum(vec3(0.0f, 0.0f, 5.0f), 4.0f) ==
        ViewFrustum::kOutside);
  // around the camera
  CHECK(view.SphereInFrustum(vec3(0.0f), 1.0f) == ViewFrustum::kIntersect);
  // larger than the frustum
  CHECK(view.SphereInFrustum(vec3(0.0f, 0.0f, -100.0f), 500.0f) ==
        ViewFrustum::kIntersect);
}

TEST_CASE(
    "The view frustum contains exactly what the view and projection matrices "
    "show",
    "[math][frustum]") {
  // Random cameras and points, reproducible with --rng-seed
  std::mt19937 random(Catch::getSeed());
  std::uniform_real_distribution<float> coordinate(-50.0f, 50.0f);
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  std::uniform_real_distribution<float> aspect_ratio(0.5f, 2.5f);
  int inside = 0, outside = 0;

  for (int camera = 0; camera < 50; ++camera) {
    vec3 position(coordinate(random), coordinate(random), coordinate(random));
    vec3 direction;
    do {
      direction = vec3(unit(random), unit(random), unit(random));
    } while (glm::length(direction) < 0.1f ||
             // not parallel to up
             std::abs(glm::normalize(direction).y) > 0.95f);
    float ratio = aspect_ratio(random);

    const ViewFrustum view = Frustum(position, position + direction, ratio);
    const glm::mat4 view_projection =
        glm::perspective(glm::radians(kFieldOfView), ratio, kNear, kFar) *
        glm::lookAt(position, position + direction, kUp);

    for (int i = 0; i < 1000; ++i) {
      // Mostly in the viewing direction, so that many points are inside
      vec3 point =
          position + glm::normalize(direction) * (coordinate(random) * 5.0f) +
          vec3(coordinate(random), coordinate(random), coordinate(random)) *
              2.0f;
      Clipped expected = Clip(view_projection, point);
      if (expected == Clipped::kBorder) {
        continue;
      }
      bool is_inside = view.PointInFrustum(point) == ViewFrustum::kInside;
      if (is_inside != (expected == Clipped::kInside)) {
        FAIL("Camera at "
             << glm::to_string(position) << " looking along "
             << glm::to_string(direction) << " (aspect ratio " << ratio
             << "): " << glm::to_string(point)
             << (is_inside ? " is inside of the frustum, but not on the screen"
                           : " is outside of the frustum, but on the screen"));
      }
      ++(is_inside ? inside : outside);
    }
  }

  // Both cases were tested
  CHECK(inside > 1000);
  CHECK(outside > 1000);
}

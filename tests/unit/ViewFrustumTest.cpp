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

using namespace bibo::test;
using bibo::resources::Plane;
using bibo::scene::ViewFrustum;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const vec3 UP(0.0f, 1.0f, 0.0f);
// Camera of the game (Camera): 60 degrees vertical field of view, 0.1 to 200
// units
const float FIELD_OF_VIEW = 60.0f, ASPECT_RATIO = 4.0f / 3.0f, NEAR = 0.1f,
            FAR = 200.0f;

ViewFrustum frustum(vec3 position, vec3 lookAt,
                    float aspectRatio = ASPECT_RATIO) {
  ViewFrustum frustum;
  frustum.setCamInternals(FIELD_OF_VIEW, aspectRatio, NEAR, FAR);
  frustum.setCamDef(position, lookAt, UP);
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
  CHECK_THAT(plane.distance(vec3(5.0f, -3.0f, 3.0f)), WithinAbs(1.0f, 1e-6f));
  CHECK_THAT(plane.distance(vec3(0.0f)), WithinAbs(-2.0f, 1e-6f));

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
    CHECK_THAT(tilted.distance(vec3(10.0f, 3.0f, -7.0f)),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(tilted.distance(vec3(6.0f, -8.0f, 1.0f)),
               WithinAbs(0.0f, 1e-4f));
  }
}

TEST_CASE("A plane through a point with a normal of any length",
          "[math][frustum]") {
  Plane plane;
  plane.setNormalAndPoint(vec3(0.0f, 2.0f, 0.0f), vec3(4.0f, 3.0f, -1.0f));

  CHECK_THAT(plane.normal, IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-6f));
  CHECK_THAT(plane.distance(vec3(0.0f, 5.0f, 0.0f)), WithinAbs(2.0f, 1e-6f));
}

TEST_CASE(
    "A plane from the coefficients of its equation measures euclidean "
    "distances",
    "[math][frustum]") {
  // a * x + b * y + c * z + d = 0 with a normal that is not normalized
  auto [a, b, c, d, point, expectedDistance] =
      GENERATE(table<float, float, float, float, vec3, float>(
          {{0.0f, 0.0f, 2.0f, -4.0f, vec3(7.0f, 1.0f, 5.0f), 3.0f},  // z = 2
           {3.0f, 4.0f, 0.0f, 10.0f, vec3(0.0f), 2.0f},
           {1.0f, 0.0f, 0.0f, 0.0f, vec3(-2.5f, 9.0f, 9.0f), -2.5f}}));
  CAPTURE(a, b, c, d, point);
  Plane plane;

  plane.setCoefficients(a, b, c, d);

  CHECK_THAT(glm::length(plane.normal), WithinAbs(1.0f, 1e-6f));
  CHECK_THAT(plane.distance(point), WithinAbs(expectedDistance, 1e-5f));
}

TEST_CASE("Points are inside of the view frustum between its six planes",
          "[math][frustum]") {
  const ViewFrustum view = frustum(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f));
  // At a distance of 10 the visible area is 2 * 10 * tan(30 degrees) = 11.55
  // units high and 4/3 of that wide
  const float halfHeight = 10.0f * std::tan(glm::radians(30.0f));
  const float halfWidth = halfHeight * ASPECT_RATIO;

  auto [point, expected] = GENERATE_COPY(table<vec3, ViewFrustum::Result>(
      {{vec3(0.0f, 0.0f, -10.0f), ViewFrustum::INSIDE},  // ahead
       {vec3(0.0f, 0.0f, 10.0f), ViewFrustum::OUTSIDE},  // behind
       {vec3(0.0f, 0.0f, -0.05f),
        ViewFrustum::OUTSIDE},  // in front of the near plane
       {vec3(0.0f, 0.0f, -199.0f), ViewFrustum::INSIDE},
       {vec3(0.0f, 0.0f, -201.0f),
        ViewFrustum::OUTSIDE},  // behind the far plane
       {vec3(0.0f, 0.99f * halfHeight, -10.0f), ViewFrustum::INSIDE},
       {vec3(0.0f, 1.01f * halfHeight, -10.0f), ViewFrustum::OUTSIDE},  // above
       {vec3(0.0f, -1.01f * halfHeight, -10.0f),
        ViewFrustum::OUTSIDE},  // below
       {vec3(0.99f * halfWidth, 0.0f, -10.0f), ViewFrustum::INSIDE},
       {vec3(1.01f * halfWidth, 0.0f, -10.0f), ViewFrustum::OUTSIDE},  // right
       {vec3(-0.99f * halfWidth, 0.0f, -10.0f), ViewFrustum::INSIDE},
       {vec3(-1.01f * halfWidth, 0.0f, -10.0f),
        ViewFrustum::OUTSIDE}}));  // left
  CAPTURE(point);

  CHECK(view.pointInFrustum(point) == expected);
}

TEST_CASE("Spheres are inside, intersecting or outside of the view frustum",
          "[math][frustum]") {
  const ViewFrustum view = frustum(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f));
  const float halfWidth = 10.0f * std::tan(glm::radians(30.0f)) * ASPECT_RATIO;

  CHECK(view.sphereInFrustum(vec3(0.0f, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::INSIDE);
  CHECK(view.sphereInFrustum(vec3(halfWidth, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::INTERSECT);  // on the right border
  CHECK(view.sphereInFrustum(vec3(halfWidth + 2.0f, 0.0f, -10.0f), 1.0f) ==
        ViewFrustum::OUTSIDE);
  CHECK(view.sphereInFrustum(vec3(0.0f, 0.0f, 5.0f), 4.0f) ==
        ViewFrustum::OUTSIDE);  // behind the camera
  CHECK(view.sphereInFrustum(vec3(0.0f), 1.0f) ==
        ViewFrustum::INTERSECT);  // around the camera
  CHECK(view.sphereInFrustum(vec3(0.0f, 0.0f, -100.0f), 500.0f) ==
        ViewFrustum::INTERSECT);  // larger than the frustum
}

TEST_CASE(
    "The view frustum contains exactly what the view and projection matrices "
    "show",
    "[math][frustum]") {
  // Random cameras and points, reproducible with --rng-seed
  std::mt19937 random(Catch::getSeed());
  std::uniform_real_distribution<float> coordinate(-50.0f, 50.0f);
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  std::uniform_real_distribution<float> aspectRatio(0.5f, 2.5f);
  int inside = 0, outside = 0;

  for (int camera = 0; camera < 50; ++camera) {
    vec3 position(coordinate(random), coordinate(random), coordinate(random));
    vec3 direction;
    do {
      direction = vec3(unit(random), unit(random), unit(random));
    } while (glm::length(direction) < 0.1f ||
             std::abs(glm::normalize(direction).y) >
                 0.95f);  // not parallel to up
    float ratio = aspectRatio(random);

    const ViewFrustum view = frustum(position, position + direction, ratio);
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(FIELD_OF_VIEW), ratio, NEAR, FAR) *
        glm::lookAt(position, position + direction, UP);

    for (int i = 0; i < 1000; ++i) {
      // Mostly in the viewing direction, so that many points are inside
      vec3 point =
          position + glm::normalize(direction) * (coordinate(random) * 5.0f) +
          vec3(coordinate(random), coordinate(random), coordinate(random)) *
              2.0f;
      Clipped expected = clip(viewProjection, point);
      if (expected == Clipped::BORDER) {
        continue;
      }
      bool isInside = view.pointInFrustum(point) == ViewFrustum::INSIDE;
      if (isInside != (expected == Clipped::INSIDE)) {
        FAIL("Camera at "
             << glm::to_string(position) << " looking along "
             << glm::to_string(direction) << " (aspect ratio " << ratio
             << "): " << glm::to_string(point)
             << (isInside ? " is inside of the frustum, but not on the screen"
                          : " is outside of the frustum, but on the screen"));
      }
      ++(isInside ? inside : outside);
    }
  }

  // Both cases were tested
  CHECK(inside > 1000);
  CHECK(outside > 1000);
}

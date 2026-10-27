#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

#include "Animation/InverseKinematics.hpp"
#include "Animation/Skeleton.hpp"
#include "TestSupport.hpp"

using namespace bibo::animation;
using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::mat4;
using glm::quat;
using glm::vec3;

namespace {
vec3 randomVector(std::mt19937& random, float range) {
  std::uniform_real_distribution<float> distribution(-range, range);
  return vec3(distribution(random), distribution(random), distribution(random));
}

quat randomRotation(std::mt19937& random) {
  vec3 axis = randomVector(random, 1.0f);
  if (glm::length(axis) < 0.01f) {
    axis = vec3(0.0f, 1.0f, 0.0f);
  }
  return glm::angleAxis(
      std::uniform_real_distribution<float>(-3.0f, 3.0f)(random),
      glm::normalize(axis));
}

/**
 * A chain of three bones (like a leg) below a rotated parent
 */
Skeleton chain() {
  Skeleton skeleton;
  skeleton.rootParent =
      glm::translate(mat4(1.0f), vec3(1.0f, 2.0f, 3.0f)) *
      glm::mat4_cast(glm::angleAxis(0.5f, vec3(0.0f, 1.0f, 0.0f)));
  Bone hip{"hip", -1, {}, mat4(1.0f)};
  Bone knee{"knee", 0, {}, mat4(1.0f)};
  knee.bindLocal.translation = vec3(0.0f, -0.5f, 0.1f);
  knee.bindLocal.rotation = glm::angleAxis(0.3f, vec3(1.0f, 0.0f, 0.0f));
  Bone ankle{"ankle", 1, {}, mat4(1.0f)};
  ankle.bindLocal.translation = vec3(0.0f, -0.4f, 0.0f);
  skeleton.bones = {hip, knee, ankle};
  skeleton.computeInverseBind();
  return skeleton;
}
}  // namespace

TEST_CASE("A transform survives the round trip through a matrix",
          "[animation]") {
  std::mt19937 random(1);
  for (int i = 0; i < 50; ++i) {
    Transform transform;
    transform.translation = randomVector(random, 10.0f);
    transform.rotation = randomRotation(random);
    transform.scale = vec3(0.2f) + glm::abs(randomVector(random, 2.0f));

    Transform result = Transform::fromMatrix(transform.toMatrix());

    CHECK_THAT(result.translation, IsNear(transform.translation, 1e-4f));
    CHECK_THAT(result.scale, IsNear(transform.scale, 1e-4f));
    CHECK_THAT(glm::mat3_cast(result.rotation),
               IsNear(glm::mat3_cast(transform.rotation), 1e-4f));
  }
}

TEST_CASE("Transforms are interpolated along the shortest arc", "[animation]") {
  Transform a, b;
  b.translation = vec3(2.0f, 0.0f, 0.0f);
  b.rotation =
      -glm::angleAxis(glm::radians(90.0f),
                      vec3(0.0f, 1.0f, 0.0f));  // same rotation, other sign

  Transform half = Transform::mix(a, b, 0.5f);

  CHECK_THAT(half.translation, IsNear(vec3(1.0f, 0.0f, 0.0f), 1e-5f));
  CHECK_THAT(glm::degrees(glm::angle(half.rotation)), WithinAbs(45.0f, 1e-2f));
}

TEST_CASE("Forward kinematics and local poses belong together", "[animation]") {
  Skeleton skeleton = chain();
  std::mt19937 random(2);
  Pose pose = skeleton.bindPose();
  for (Transform& bone : pose) {
    bone.rotation = randomRotation(random);
  }
  const mat4 base = glm::translate(mat4(1.0f), vec3(-4.0f, 0.5f, 7.0f));

  std::vector<mat4> matrices;
  skeleton.modelMatrices(pose, matrices, base);
  Pose result = skeleton.localPose(matrices, base);

  REQUIRE(result.size() == pose.size());
  for (std::size_t i = 0; i < pose.size(); ++i) {
    CAPTURE(i);
    CHECK_THAT(result[i].toMatrix(), IsNear(pose[i].toMatrix(), 1e-4f));
  }
  SECTION("A child bone is placed relative to its parent") {
    CHECK_THAT(vec3(matrices[1][3]),
               IsNear(vec3(matrices[0] * glm::vec4(pose[1].translation, 1.0f)),
                      1e-5f));
  }
}

TEST_CASE("In the bind pose the skinning does not move the vertices",
          "[animation]") {
  Skeleton skeleton = chain();
  std::vector<mat4> matrices, skin;
  skeleton.modelMatrices(skeleton.bindPose(), matrices);
  skeleton.skinMatrices(matrices, skin);

  for (const mat4& matrix : skin) {
    CHECK_THAT(matrix, IsNear(mat4(1.0f), 1e-5f));
  }
}

TEST_CASE("The rotation between two directions turns one into the other",
          "[animation]") {
  std::mt19937 random(3);
  for (int i = 0; i < 50; ++i) {
    vec3 from = randomVector(random, 3.0f), to = randomVector(random, 3.0f);
    CHECK_THAT(rotationBetween(from, to) * glm::normalize(from),
               IsNear(glm::normalize(to), 1e-4f));
  }
  SECTION("Opposite directions") {
    const vec3 up(0.0f, 1.0f, 0.0f);
    CHECK_THAT(rotationBetween(up, -up) * up, IsNear(-up, 1e-5f));
    CHECK_THAT(
        rotationBetween(vec3(1.0f, 0.0f, 0.0f), vec3(-2.0f, 0.0f, 0.0f)) *
            vec3(1.0f, 0.0f, 0.0f),
        IsNear(vec3(-1.0f, 0.0f, 0.0f), 1e-5f));
  }
  SECTION("No direction") {
    CHECK(rotationBetween(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f)) ==
          quat(1.0f, 0.0f, 0.0f, 0.0f));
  }
}

TEST_CASE("Two bone IK reaches the target and keeps the lengths of the bones",
          "[animation][ik]") {
  const vec3 hip(0.0f, 1.0f, 0.0f);
  const float thigh = 0.54f, shin = 0.34f;
  std::mt19937 random(4);
  int reached = 0;
  for (int i = 0; i < 200; ++i) {
    vec3 target = hip + randomVector(random, 0.9f);
    vec3 pole = hip + vec3(0.0f, -0.4f, 1.0f);
    CAPTURE(target);

    TwoBoneSolution solution = solveTwoBoneIK(hip, thigh, shin, target, pole);

    CHECK_THAT(glm::distance(hip, solution.joint), WithinAbs(thigh, 1e-4f));
    CHECK_THAT(glm::distance(solution.joint, solution.end),
               WithinAbs(shin, 1e-4f));
    float distance = glm::distance(hip, target);
    if (distance < thigh + shin - 0.01f && distance > thigh - shin + 0.01f) {
      CHECK(solution.reached);
      CHECK_THAT(solution.end, IsNear(target, 1e-4f));
      ++reached;
    } else if (!solution.reached) {
      // Out of reach: the chain points at the target
      CHECK(glm::dot(glm::normalize(solution.end - hip),
                     glm::normalize(target - hip)) > 0.9999f);
    }
  }
  CHECK(reached > 50);
}

TEST_CASE("The middle joint of the IK chain bends towards the pole",
          "[animation][ik]") {
  const vec3 hip(0.0f, 1.0f, 0.0f), foot(0.0f, 0.2f, 0.0f);
  const vec3 forward(0.0f, 0.0f, 1.0f);

  SECTION("Knee forward") {
    TwoBoneSolution solution =
        solveTwoBoneIK(hip, 0.54f, 0.34f, foot, hip + forward);
    CHECK(solution.joint.z > 0.1f);
  }
  SECTION("Elbow backward") {
    TwoBoneSolution solution =
        solveTwoBoneIK(hip, 0.54f, 0.34f, foot, hip - forward);
    CHECK(solution.joint.z < -0.1f);
  }
  SECTION("Pole on the line between root and target: still a valid bend") {
    TwoBoneSolution solution = solveTwoBoneIK(hip, 0.54f, 0.34f, foot, foot);
    CHECK_THAT(glm::distance(hip, solution.joint), WithinAbs(0.54f, 1e-4f));
    CHECK_THAT(solution.end, IsNear(foot, 1e-4f));
  }
}

TEST_CASE("Springs settle at their target, also with long frames",
          "[animation]") {
  const float timeDelta = GENERATE(1.0f / 144.0f, 1.0f / 60.0f, 0.1f);
  CAPTURE(timeDelta);
  SpringPoint point;
  SpringValue value;
  const vec3 target(1.0f, 2.0f, -3.0f);
  float maxDistance = 0.0f;
  for (float time = 0.0f; time < 4.0f; time += timeDelta) {
    point.update(target, 260.0f, 10.0f, vec3(0.0f), timeDelta);
    value.update(1.0f, 170.0f, 13.0f, timeDelta);
    maxDistance = std::max(maxDistance, glm::distance(point.position, target));
  }
  CHECK_THAT(point.position, IsNear(target, 1e-3f));
  CHECK_THAT(value.value, WithinAbs(1.0f, 1e-3f));
  CHECK(maxDistance <
        2.0f * glm::length(target));  // overshoots a bit, but does not explode
}

TEST_CASE("Smoothing does not depend on the frame rate", "[animation]") {
  float fast = 0.0f, slow = 0.0f;
  for (int i = 0; i < 120; ++i) {
    fast += (1.0f - fast) * smoothingFactor(0.2f, 1.0f / 120.0f);
  }
  for (int i = 0; i < 30; ++i) {
    slow += (1.0f - slow) * smoothingFactor(0.2f, 1.0f / 30.0f);
  }
  CHECK_THAT(fast, WithinAbs(slow, 1e-4f));
  CHECK_THAT(fast, WithinAbs(1.0f - std::exp2(-5.0f),
                             1e-4f));  // 5 half-lives in a second
}

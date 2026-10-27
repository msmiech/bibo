#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "Physics/Physics.hpp"
#include "TestSupport.hpp"

using bibo::physics::BodyHandle;
using bibo::physics::CharacterController;
using bibo::physics::CharacterControllerDesc;
using bibo::physics::PhysicsWorld;
using bibo::physics::Ragdoll;
using bibo::physics::RagdollJoint;
using bibo::physics::RagdollPart;
using bibo::physics::RayHit;

using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::mat4;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;

/**
 * A body with a head and two legs, standing upright at the origin (y up)
 */
std::vector<RagdollPart> Puppet() {
  std::vector<RagdollPart> parts(4);
  // body
  parts[0].bind_transform = glm::translate(mat4(1.0f), vec3(0.0f, 1.0f, 0.0f));
  parts[0].capsule_start = vec3(0.0f, 0.8f, 0.0f);
  parts[0].capsule_end = vec3(0.0f, 1.3f, 0.0f);
  parts[0].radius = 0.25f;
  parts[0].mass = 8.0f;

  parts[1].parent = 0;  // head
  parts[1].bind_transform = glm::translate(mat4(1.0f), vec3(0.0f, 1.6f, 0.0f));
  parts[1].capsule_start = parts[1].capsule_end = vec3(0.0f, 1.75f, 0.0f);
  parts[1].radius = 0.2f;
  parts[1].mass = 3.0f;
  parts[1].joint.position = vec3(0.0f, 1.55f, 0.0f);
  parts[1].joint.twist_axis = vec3(0.0f, 1.0f, 0.0f);
  parts[1].joint.plane_axis = vec3(0.0f, 0.0f, 1.0f);

  for (int leg = 0; leg < 2; ++leg) {
    float x = leg == 0 ? 0.15f : -0.15f;
    RagdollPart& part = parts[2 + leg];
    part.parent = 0;
    part.bind_transform = glm::translate(mat4(1.0f), vec3(x, 0.6f, 0.0f));
    part.capsule_start = vec3(x, 0.55f, 0.0f);
    part.capsule_end = vec3(x, 0.15f, 0.0f);
    part.radius = 0.1f;
    part.mass = 2.0f;
    part.joint.type = RagdollJoint::kHinge;
    part.joint.position = vec3(x, 0.6f, 0.0f);
    part.joint.twist_axis = vec3(1.0f, 0.0f, 0.0f);
    part.joint.plane_axis = vec3(0.0f, -1.0f, 0.0f);
    part.joint.min_angle = -1.2f;
    part.joint.max_angle = 1.2f;
  }
  return parts;
}

std::vector<mat4> BindTransforms(const std::vector<RagdollPart>& parts,
                                 vec3 offset) {
  std::vector<mat4> transforms;
  for (const RagdollPart& part : parts) {
    transforms.push_back(glm::translate(mat4(1.0f), offset) *
                         part.bind_transform);
  }
  return transforms;
}
}  // namespace

TEST_CASE("A ragdoll falls over and comes to rest on the ground",
          "[physics][ragdoll]") {
  PhysicsWorld world;
  world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(50.0f, 1.0f, 50.0f));
  const std::vector<RagdollPart> parts = Puppet();
  Ragdoll ragdoll(world, parts);
  REQUIRE(ragdoll.GetPartCount() == parts.size());
  REQUIRE_FALSE(ragdoll.IsActive());

  // Thrown up and forward with a spin, like the bear after a hit
  std::vector<vec3> velocities(parts.size(), vec3(0.0f, 3.0f, 4.0f));
  ragdoll.Activate(BindTransforms(parts, vec3(0.0f, 0.5f, 0.0f)), velocities,
                   vec3(3.0f, 0.0f, 0.0f));
  REQUIRE(ragdoll.IsActive());
  float rest_time = 0.0f;
  for (int i = 0; i < 6 * 60 && rest_time < 0.5f; ++i) {
    world.Update(kFrame);
    bool resting = ragdoll.GetMaxLinearSpeed() < 0.35f &&
                   ragdoll.GetMaxAngularSpeed() < 1.2f;
    rest_time = resting ? rest_time + kFrame : 0.0f;
  }
  CHECK(rest_time >= 0.5f);

  std::vector<mat4> transforms;
  ragdoll.GetTransforms(transforms);
  REQUIRE(transforms.size() == parts.size());
  CHECK(transforms[0][3].z > 0.5f);  // it flew forward
  for (std::size_t i = 0; i < parts.size(); ++i) {
    CAPTURE(i);
    CHECK(transforms[i][3].y > -0.05f);  // on the ground, not in it
    CHECK(transforms[i][3].y < 1.0f);    // lying, not standing
  }

  SECTION("The parts stay connected at their joints") {
    for (std::size_t i = 1; i < parts.size(); ++i) {
      CAPTURE(i);
      const mat4 in_child = glm::inverse(parts[i].bind_transform);
      const mat4 in_parent =
          glm::inverse(parts[parts[i].parent].bind_transform);
      vec3 joint_from_child = vec3(transforms[i] * in_child *
                                   glm::vec4(parts[i].joint.position, 1.0f));
      vec3 joint_from_parent = vec3(transforms[parts[i].parent] * in_parent *
                                    glm::vec4(parts[i].joint.position, 1.0f));
      CHECK(glm::distance(joint_from_child, joint_from_parent) < 0.03f);
    }
  }
}

TEST_CASE("Only an active ragdoll is part of the physics world",
          "[physics][ragdoll]") {
  PhysicsWorld world;
  const std::vector<RagdollPart> parts = Puppet();
  Ragdoll ragdoll(world, parts);
  RayHit hit;
  auto body_is_there = [&]() {
    return world.CastRay(vec3(0.0f, 1.0f, 5.0f), vec3(0.0f, 0.0f, -10.0f), hit);
  };

  CHECK_FALSE(body_is_there());
  ragdoll.Activate(BindTransforms(parts, vec3(0.0f)),
                   std::vector<vec3>(parts.size(), vec3(0.0f)), vec3(0.0f));
  CHECK(body_is_there());
  CHECK_THAT(hit.position.z,
             WithinAbs(0.25f, 0.01f));  // the front of the body capsule
  SECTION("Static only: the ragdoll is not found") {
    CHECK_FALSE(world.CastRay(vec3(0.0f, 1.0f, 5.0f), vec3(0.0f, 0.0f, -10.0f),
                              hit, true));
  }
  ragdoll.Deactivate();
  CHECK_FALSE(body_is_there());
}

TEST_CASE("A ray finds the first body with its surface normal", "[physics]") {
  PhysicsWorld world;
  world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(10.0f, 1.0f, 10.0f));
  BodyHandle box =
      world.CreateDynamicBox(vec3(3.0f, 0.5f, 0.0f), vec3(0.5f), 1.0f);
  RayHit hit;

  REQUIRE(world.CastRay(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), hit));
  CHECK_THAT(hit.position, IsNear(vec3(0.0f), 1e-3f));
  CHECK_THAT(hit.normal, IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-3f));
  CHECK_THAT(hit.fraction, WithinAbs(0.5f, 1e-3f));

  SECTION("Dynamic bodies are found, unless only static bodies are wanted") {
    REQUIRE(
        world.CastRay(vec3(3.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), hit));
    CHECK(hit.body == box);
    CHECK_THAT(hit.position.y, WithinAbs(1.0f, 1e-3f));
    REQUIRE(world.CastRay(vec3(3.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), hit,
                          true));
    CHECK(hit.body != box);
    CHECK_THAT(hit.position.y, WithinAbs(0.0f, 1e-3f));
  }
  SECTION("Nothing on the way") {
    CHECK_FALSE(
        world.CastRay(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, 2.0f, 0.0f), hit));
  }
}

TEST_CASE(
    "A hit tells where the character touches the body and from which side",
    "[physics][bear]") {
  PhysicsWorld world;
  world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(50.0f, 1.0f, 50.0f));
  BodyHandle rock = world.CreateStaticSphere(vec3(0.0f, 0.7f, -4.0f), 0.7f);
  std::vector<CharacterController::Hit> hits;
  CharacterControllerDesc desc;
  desc.position = vec3(0.0f, 1.3f, 0.0f);
  desc.radius = 1.0f;
  desc.height = 0.6f;
  CharacterController character(
      world, desc,
      [&](const CharacterController::Hit& hit) { hits.push_back(hit); });

  for (int i = 0; i < 60; ++i) {
    world.Update(kFrame);
    character.Move(vec3(0.0f, -5.0f, -10.0f) * kFrame, kFrame);
  }

  auto rock_hit =
      std::ranges::find(hits, rock, &CharacterController::Hit::body);
  REQUIRE(rock_hit != hits.end());
  // pointing from the rock back to the character
  CHECK(rock_hit->normal.z > 0.5f);
  // on the surface of the rock
  CHECK(glm::distance(rock_hit->position, vec3(0.0f, 0.7f, -4.0f)) < 0.75f);
}

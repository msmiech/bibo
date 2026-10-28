#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include "Physics/Physics.hpp"
#include "TestSupport.hpp"

using bibo::physics::BodyHandle;
using bibo::physics::CollisionShape;
using bibo::physics::kInvalidBody;
using bibo::physics::PhysicsWorld;
using bibo::physics::RayHit;
using bibo::physics::ShapePart;

using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::mat3;
using glm::mat4;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;
const float kGravity = 9.81f;
// The ground of the game is a large box whose top face is at y = -5
// (Scene::InitPhysicsBodies)
const float kGroundTop = -5.0f;

void Simulate(PhysicsWorld& world, float seconds, float time_delta = kFrame) {
  int frames = static_cast<int>(std::lround(seconds / time_delta));
  for (int i = 0; i < frames; ++i) {
    world.Update(time_delta);
  }
}

vec3 PositionOf(const PhysicsWorld& world, BodyHandle body) {
  return vec3(world.GetTransform(body)[3]);
}

BodyHandle CreateGround(PhysicsWorld& world) {
  return world.CreateStaticBox(vec3(0.0f, kGroundTop - 1.0f, 0.0f),
                               vec3(1000.0f, 1.0f, 1000.0f));
}

// Square of 2 triangles in the XY plane from -1 to 1, the front side faces +Z
// (up in Blender/Collada)
const float kSquareVertices[] = {-1.0f, -1.0f, 0.0f, 1.0f,  -1.0f, 0.0f,
                                 1.0f,  1.0f,  0.0f, -1.0f, 1.0f,  0.0f};
const unsigned int kSquareIndices[] = {0, 1, 2, 0, 2, 3};
}  // namespace

TEST_CASE("A dynamic box falls with the gravity of the earth", "[physics]") {
  PhysicsWorld world;
  const vec3 start(1.0f, 10.0f, -2.0f);
  BodyHandle box = world.CreateDynamicBox(start, vec3(0.4f), 1.0f);

  Simulate(world, 1.0f);

  vec3 position = PositionOf(world, box);
  CHECK_THAT(start.y - position.y,
             WithinAbs(0.5f * kGravity, 0.25f));  // s = g * t^2 / 2
  CHECK_THAT(position.x, WithinAbs(start.x, 1e-4f));
  CHECK_THAT(position.z, WithinAbs(start.z, 1e-4f));
}

TEST_CASE("A dynamic box comes to rest on the ground", "[physics]") {
  PhysicsWorld world;
  CreateGround(world);
  const float half_extent = 0.4f;
  BodyHandle box = world.CreateDynamicBox(vec3(0.0f), vec3(half_extent), 1.0f);

  Simulate(world, 3.0f);
  mat4 resting = world.GetTransform(box);
  CHECK_THAT(resting[3].y, WithinAbs(kGroundTop + half_extent, 0.05f));
  CHECK_THAT(mat3(resting), IsNear(mat3(1.0f), 0.01f));  // still upright

  Simulate(world, 1.0f);
  CHECK_THAT(PositionOf(world, box), IsNear(vec3(resting[3]), 1e-3f));
}

TEST_CASE("Long frames are split into several steps and stay stable",
          "[physics]") {
  // e.g. while the window is dragged the game loop stalls
  PhysicsWorld world;
  CreateGround(world);
  BodyHandle box =
      world.CreateDynamicBox(vec3(0.0f, 5.0f, 0.0f), vec3(0.4f), 1.0f);

  Simulate(world, 4.0f, 0.5f);

  CHECK_THAT(PositionOf(world, box).y, WithinAbs(kGroundTop + 0.4f, 0.1f));
}

TEST_CASE("Time does not advance for frames without duration", "[physics]") {
  PhysicsWorld world;
  const vec3 start(0.0f, 10.0f, 0.0f);
  BodyHandle box = world.CreateDynamicBox(start, vec3(0.4f), 1.0f);

  world.Update(0.0f);
  world.Update(-1.0f);

  CHECK(PositionOf(world, box) == start);
}

TEST_CASE("Static bodies stay where they were created", "[physics]") {
  PhysicsWorld world;
  const vec3 box_position(0.0f, -1.0f, 0.0f);
  const vec3 sphere_position(5.0f, 0.0f, 0.0f);
  BodyHandle static_box =
      world.CreateStaticBox(box_position, vec3(2.0f, 1.0f, 2.0f));
  BodyHandle static_sphere = world.CreateStaticSphere(sphere_position, 0.7f);
  // Falls onto both
  world.CreateDynamicBox(vec3(0.0f, 3.0f, 0.0f), vec3(0.5f), 1.0f);
  world.CreateDynamicBox(vec3(5.0f, 3.0f, 0.0f), vec3(0.5f), 1.0f);

  Simulate(world, 2.0f);

  CHECK(world.GetTransform(static_box) ==
        glm::translate(mat4(1.0f), box_position));
  CHECK(world.GetTransform(static_sphere) ==
        glm::translate(mat4(1.0f), sphere_position));
}

TEST_CASE("Every body gets its own valid handle", "[physics]") {
  PhysicsWorld world;
  std::set<BodyHandle> handles;
  for (int i = 0; i < 5; ++i) {
    handles.insert(
        world.CreateDynamicBox(vec3(3.0f * i, 0.0f, 0.0f), vec3(0.4f), 1.0f));
    handles.insert(world.CreateStaticSphere(vec3(3.0f * i, 5.0f, 0.0f), 0.7f));
  }
  // The wall of the scene is only 2 cm thick (thinner than the default convex
  // radius of Jolt)
  handles.insert(
      world.CreateStaticBox(vec3(0.0f, 0.0f, 3.0f), vec3(7.0f, 7.0f, 0.01f)));

  CHECK(handles.size() == 11);
  CHECK_FALSE(handles.contains(kInvalidBody));
}

TEST_CASE("A triangle mesh is scaled, rotated and placed", "[physics]") {
  // Modelled with Z-up (like in Blender), rotated to Y-up and scaled
  PhysicsWorld world;
  const vec3 position(0.0f, 2.0f, 0.0f);
  const glm::quat rotation =
      glm::angleAxis(glm::radians(-90.0f), vec3(1.0f, 0.0f, 0.0f));
  BodyHandle mesh = world.CreateStaticMesh(kSquareVertices, 4, kSquareIndices,
                                           2, 10.0f, position, rotation);
  REQUIRE(mesh != kInvalidBody);

  SECTION("The transformation of the body is the rotation and the position") {
    mat4 expected =
        glm::translate(mat4(1.0f), position) * glm::mat4_cast(rotation);
    CHECK_THAT(world.GetTransform(mesh), IsNear(expected, 1e-5f));
  }

  SECTION("A box falling onto the scaled square lands on it") {
    // Outside of the unscaled square, inside of the scaled one
    BodyHandle box =
        world.CreateDynamicBox(vec3(5.0f, 6.0f, 5.0f), vec3(0.4f), 1.0f);
    Simulate(world, 3.0f);
    CHECK_THAT(PositionOf(world, box).y, WithinAbs(position.y + 0.4f, 0.05f));
  }

  SECTION("A box next to the scaled square falls past it") {
    BodyHandle box =
        world.CreateDynamicBox(vec3(15.0f, 6.0f, 15.0f), vec3(0.4f), 1.0f);
    Simulate(world, 2.0f);
    CHECK(PositionOf(world, box).y < position.y - 5.0f);
  }
}

TEST_CASE(
    "Shapes built in other threads become static bodies that can be removed "
    "again",
    "[physics]") {
  // Like the streamed world: the worker threads build the shapes, the game adds
  // and removes the bodies
  PhysicsWorld world;
  std::shared_ptr<CollisionShape> ground, trunks;
  std::thread builder([&] {
    std::vector<vec3> vertices = {{-10.0f, 0.0f, -10.0f},
                                  {10.0f, 0.0f, -10.0f},
                                  {10.0f, 0.0f, 10.0f},
                                  {-10.0f, 0.0f, 10.0f}};
    ground = PhysicsWorld::BuildMeshShape(vertices, {0, 2, 1, 0, 3, 2});
    ShapePart trunk;
    trunk.type = ShapePart::kCapsule;
    trunk.position = vec3(3.0f, 2.0f, 0.0f);
    trunk.half_height = 1.5f;
    trunk.radius = 0.3f;
    ShapePart stone;
    stone.position = vec3(-3.0f, 0.5f, 0.0f);
    stone.radius = 0.8f;
    trunks = PhysicsWorld::BuildCompoundShape({trunk, stone});
  });
  builder.join();
  REQUIRE(ground);
  REQUIRE(trunks);
  CHECK(ground->IsValid());
  CHECK_FALSE(PhysicsWorld::BuildMeshShape({}, {}));
  CHECK_FALSE(PhysicsWorld::BuildCompoundShape({}));

  std::size_t before = world.GetBodyCount();
  BodyHandle ground_body = world.AddStaticBody(*ground);
  BodyHandle trunks_body = world.AddStaticBody(*trunks);
  REQUIRE(ground_body != kInvalidBody);
  REQUIRE(trunks_body != kInvalidBody);
  CHECK(world.GetBodyCount() == before + 2);

  // The shapes are in world space (the bodies are at the origin)
  RayHit hit;
  REQUIRE(world.CastRay(vec3(1.0f, 5.0f, 1.0f), vec3(0.0f, -10.0f, 0.0f), hit,
                        true));
  CHECK(hit.body == ground_body);
  CHECK_THAT(hit.position.y, WithinAbs(0.0f, 1e-3f));
  REQUIRE(world.CastRay(vec3(3.0f, 2.0f, -5.0f), vec3(0.0f, 0.0f, 10.0f), hit,
                        true));
  CHECK(hit.body == trunks_body);
  CHECK_THAT(hit.position.z, WithinAbs(-0.3f, 1e-3f));
  REQUIRE(world.CastRay(vec3(-3.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), hit,
                        true));
  CHECK_THAT(hit.position.y, WithinAbs(1.3f, 1e-3f));

  world.RemoveBody(trunks_body);
  world.RemoveBody(kInvalidBody);  // nothing happens
  CHECK(world.GetBodyCount() == before + 1);
  REQUIRE(world.CastRay(vec3(3.0f, 2.0f, -5.0f), vec3(0.0f, 0.0f, 10.0f), hit,
                        true) == false);
  world.RemoveBody(ground_body);
  CHECK(world.GetBodyCount() == before);
  CHECK_FALSE(world.CastRay(vec3(1.0f, 5.0f, 1.0f), vec3(0.0f, -10.0f, 0.0f),
                            hit, true));
}

TEST_CASE("Physics worlds can be created again and simulate the same way",
          "[physics]") {
  // PhysicsWorld registers and unregisters the global Jolt factory
  std::vector<mat4> results;
  for (int i = 0; i < 3; ++i) {
    PhysicsWorld world;
    CreateGround(world);
    BodyHandle box = world.CreateDynamicBox(vec3(0.0f), vec3(0.4f), 1.0f);
    world.CreateDynamicBox(vec3(0.3f, 2.0f, 0.0f), vec3(0.4f),
                           1.0f);  // falls onto the first one
    Simulate(world, 1.0f);
    results.push_back(world.GetTransform(box));
  }

  CHECK(results[1] == results[0]);
  CHECK(results[2] == results[0]);
}

TEST_CASE("A sphere cast stops where the sphere touches the first body",
          "[physics]") {
  PhysicsWorld world;
  world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f),
                        vec3(50.0f, 1.0f, 50.0f));  // top at y = 0
  world.OptimizeBroadPhase();
  const float radius = 0.5f;
  RayHit hit;

  SECTION("Down onto the ground") {
    REQUIRE(world.CastSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f),
                             radius, hit));
    CHECK_THAT(hit.fraction, WithinAbs((5.0f - radius) / 10.0f, 1e-3f));
    CHECK_THAT(hit.position, IsNear(vec3(0.0f), 1e-3f));
    CHECK_THAT(hit.normal, IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-3f));
  }

  SECTION("Along the ground without touching it") {
    CHECK_FALSE(world.CastSphere(vec3(0.0f, 1.0f, 0.0f),
                                 vec3(10.0f, 0.0f, 0.0f), radius, hit));
  }

  SECTION("A ray would pass a body that the sphere touches") {
    world.CreateStaticSphere(vec3(5.0f, 1.0f, 0.7f), 0.4f);
    RayHit ray_hit;
    CHECK_FALSE(world.CastRay(vec3(0.0f, 1.0f, 0.0f), vec3(10.0f, 0.0f, 0.0f),
                              ray_hit));
    REQUIRE(world.CastSphere(vec3(0.0f, 1.0f, 0.0f), vec3(10.0f, 0.0f, 0.0f),
                             radius, hit));
    CHECK(hit.fraction > 0.3f);
    CHECK(hit.fraction < 0.5f);
  }

  SECTION("Only static bodies") {
    world.CreateDynamicBox(vec3(0.0f, 3.0f, 0.0f), vec3(0.4f), 1.0f);
    REQUIRE(world.CastSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f),
                             radius, hit));
    CHECK(hit.fraction < 0.2f);  // the box
    REQUIRE(world.CastSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f),
                             radius, hit, true));
    CHECK_THAT(hit.fraction,
               WithinAbs((5.0f - radius) / 10.0f, 1e-3f));  // the ground
  }
}

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "Physics/Physics.hpp"
#include "Scene/Bear.hpp"
#include "TestSupport.hpp"

using bibo::physics::BodyHandle;
using bibo::physics::CharacterController;
using bibo::physics::CharacterControllerDesc;
using bibo::physics::PhysicsWorld;
using bibo::physics::RayHit;

using bibo::scene::Bear;
using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;
const float kGroundTop = -5.0f;
// Pushes the character onto the ground every frame (like the gravity in
// Bear::Update)
const vec3 kDown(0.0f, -5.0f, 0.0f);
// Speeds of the bear (Bear.cpp)
const float kForwardSpeed = 10.0f;
const float kBackwardSpeed = 7.5f;

bool Contains(const std::vector<BodyHandle>& bodies, BodyHandle body) {
  return std::ranges::find(bodies, body) != bodies.end();
}

/**
 * Physics world with the ground of the game and the bear standing on it at the
 * origin
 */
struct BearOnGround {
  PhysicsWorld world;
  BodyHandle ground;
  CharacterControllerDesc desc =
      Bear::ControllerDesc(vec3(0.0f, kGroundTop + 3.0f, 0.0f));
  std::vector<BodyHandle> touched_bodies;
  CharacterController bear{world, desc,
                           [this](const CharacterController::Hit& hit) {
                             touched_bodies.push_back(hit.body);
                           }};
  // Height of the center of the capsule when the bear stands on the ground
  const float standing_height = kGroundTop + 0.5f * desc.height + desc.radius;

  BearOnGround()
      : ground(world.CreateStaticBox(vec3(0.0f, kGroundTop - 1.0f, 0.0f),
                                     vec3(1000.0f, 1.0f, 1000.0f))) {}

  /**
   * Moves the bear every frame with the given velocity, like the game loop
   * (Scene::Update, Bear::Update)
   * @return true if the bear stands on the ground afterwards
   */
  bool MoveFor(float seconds, vec3 velocity) {
    bool supported = false;
    int frames = static_cast<int>(std::lround(seconds / kFrame));
    for (int i = 0; i < frames; ++i) {
      world.Update(kFrame);
      supported = bear.Move(velocity * kFrame, kFrame);
    }
    return supported;
  }

  void Land() { REQUIRE(MoveFor(1.0f, kDown)); }
};
}  // namespace

TEST_CASE_METHOD(BearOnGround, "The bear lands on the ground",
                 "[physics][bear]") {
  CHECK(MoveFor(2.0f, kDown));
  CHECK_THAT(bear.GetPosition(), IsNear(vec3(0.0f, standing_height, 0.0f),
                                        desc.contact_offset + 0.01f));

  SECTION(
      "The ground is reported as touched body (Scene::onBearHit only reacts to "
      "rocks)") {
    CHECK(Contains(touched_bodies, ground));
  }

  SECTION(
      "Pushing down on flat ground neither moves the bear nor lets it sink "
      "in") {
    vec3 standing = bear.GetPosition();
    CHECK(MoveFor(2.0f, kDown));
    CHECK_THAT(bear.GetPosition(), IsNear(standing, 1e-3f));
  }
}

TEST_CASE_METHOD(BearOnGround, "The bear walks with its speed on flat ground",
                 "[physics][bear]") {
  Land();
  vec3 start = bear.GetPosition();

  CHECK(MoveFor(1.0f, vec3(0.0f, 0.0f, -kForwardSpeed) + kDown));

  vec3 walked = bear.GetPosition() - start;
  CHECK_THAT(walked.z, WithinAbs(-kForwardSpeed, 0.3f));
  CHECK_THAT(walked.x, WithinAbs(0.0f, 1e-3f));
  CHECK_THAT(walked.y, WithinAbs(0.0f, 0.01f));
}

TEST_CASE_METHOD(BearOnGround,
                 "A frame without duration does not move the bear",
                 "[physics][bear]") {
  Land();
  vec3 start = bear.GetPosition();

  CHECK(bear.Move(vec3(5.0f, 0.0f, 5.0f), 0.0f));  // still reports the ground
  CHECK(bear.GetPosition() == start);
}

TEST_CASE_METHOD(BearOnGround, "The bear cannot walk through a thin wall",
                 "[physics][bear]") {
  // 14 x 14 wall, 2 cm thick, 3 units behind the bear (like the wall that once
  // blocked the way back from the start)
  const float wall_z = 3.0f;
  world.CreateStaticBox(vec3(0.0f, standing_height, wall_z),
                        vec3(7.0f, 7.0f, 0.01f));
  Land();

  // would walk 15 units without the wall
  MoveFor(2.0f, vec3(0.0f, 0.0f, kBackwardSpeed) + kDown);

  float front_of_bear = bear.GetPosition().z + desc.radius;
  CHECK(front_of_bear < wall_z);
  CHECK(front_of_bear > wall_z - 0.2f);
}

TEST_CASE_METHOD(BearOnGround,
                 "Touching a rock is reported with the body of the rock",
                 "[physics][bear]") {
  // Scene::InitPhysicsBodies: rocks are static spheres with a radius of 0.7
  const float rock_radius = 0.7f;

  SECTION("Rock in the way") {
    BodyHandle rock = world.CreateStaticSphere(
        vec3(0.0f, kGroundTop + rock_radius, -5.0f), rock_radius);
    Land();
    REQUIRE_FALSE(Contains(touched_bodies, rock));

    MoveFor(1.0f, vec3(0.0f, 0.0f, -kForwardSpeed) + kDown);

    CHECK(Contains(touched_bodies, rock));
  }

  SECTION("Rock next to the way") {
    BodyHandle rock = world.CreateStaticSphere(
        vec3(3.0f, kGroundTop + rock_radius, -5.0f), rock_radius);
    Land();

    MoveFor(1.0f, vec3(0.0f, 0.0f, -kForwardSpeed) + kDown);

    CHECK_FALSE(Contains(touched_bodies, rock));
    CHECK(bear.GetPosition().z < -9.0f);
  }
}

TEST_CASE("The bear can be created without a hit callback", "[physics][bear]") {
  PhysicsWorld world;
  world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(10.0f, 1.0f, 10.0f));
  CharacterController bear(world, Bear::ControllerDesc(vec3(0.0f, 2.0f, 0.0f)),
                           nullptr);

  bool supported = false;
  for (int i = 0; i < 60; ++i) {
    world.Update(kFrame);
    supported = bear.Move(kDown * kFrame, kFrame);
  }
  CHECK(supported);
}

TEST_CASE_METHOD(BearOnGround,
                 "The bear climbs obstacles up to its step offset",
                 "[physics][bear]") {
  Land();

  SECTION("Step lower than the step offset") {
    const float step_height = 0.5f * desc.step_offset;
    world.CreateStaticBox(vec3(0.0f, kGroundTop + 0.5f * step_height, -13.0f),
                          vec3(5.0f, 0.5f * step_height, 10.0f));

    MoveFor(1.5f, vec3(0.0f, 0.0f, -kForwardSpeed) + kDown);

    CHECK(bear.GetPosition().z < -5.0f);
    CHECK_THAT(bear.GetPosition().y,
               WithinAbs(standing_height + step_height, 0.1f));
  }

  SECTION("Obstacle higher than the step offset") {
    const float obstacle_height = 1.5f * desc.step_offset;
    const float obstacle_front = -3.0f;
    world.CreateStaticBox(
        vec3(0.0f, kGroundTop + 0.5f * obstacle_height, obstacle_front - 1.0f),
        vec3(5.0f, 0.5f * obstacle_height, 1.0f));

    MoveFor(1.5f, vec3(0.0f, 0.0f, -kForwardSpeed) + kDown);

    CHECK(bear.GetPosition().z - desc.radius > obstacle_front - 0.2f);
    CHECK_THAT(bear.GetPosition().y, WithinAbs(standing_height, 0.1f));
  }
}

TEST_CASE("The bear stands on gentle slopes and slides down steep ones",
          "[physics][bear]") {
  PhysicsWorld world;
  // Square of 40 x 40 with the front side facing up, tilted around the x axis
  const float vertices[] = {-20.0f, 0.0f, -20.0f, -20.0f, 0.0f, 20.0f,
                            20.0f,  0.0f, 20.0f,  20.0f,  0.0f, -20.0f};
  const unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  const CharacterControllerDesc desc =
      Bear::ControllerDesc(vec3(0.0f, 3.0f, 0.0f));

  auto slide_on_slope = [&](float slope_degrees) {
    world.CreateStaticMesh(
        vertices, 4, indices, 2, 1.0f, vec3(0.0f),
        glm::angleAxis(glm::radians(slope_degrees), vec3(1.0f, 0.0f, 0.0f)));
    CharacterController bear(world, desc, nullptr);
    for (int i = 0; i < 60; ++i)  // land
    {
      world.Update(kFrame);
      bear.Move(kDown * kFrame, kFrame);
    }
    vec3 landed = bear.GetPosition();
    for (int i = 0; i < 120; ++i) {
      world.Update(kFrame);
      bear.Move(kDown * kFrame, kFrame);
    }
    return glm::distance(landed, bear.GetPosition());
  };

  SECTION("Slope below the slope limit") {
    REQUIRE(glm::radians(30.0f) < desc.slope_limit);
    CHECK(slide_on_slope(30.0f) < 0.05f);
  }

  SECTION("Slope above the slope limit") {
    REQUIRE(glm::radians(60.0f) > desc.slope_limit);
    CHECK(slide_on_slope(60.0f) > 2.0f);
  }
}

TEST_CASE_METHOD(BearOnGround, "Jumping lifts the bear off the ground",
                 "[physics][bear]") {
  Land();
  const float jump_speed = 10.0f - 2.0f * 0.981f,
              max_jump_time = 0.5f;  // Bear.cpp (jumpVelocity)

  bool supported = MoveFor(max_jump_time, vec3(0.0f, jump_speed, 0.0f));

  CHECK_FALSE(supported);
  CHECK_THAT(bear.GetPosition().y - standing_height,
             WithinAbs(jump_speed * max_jump_time, 0.3f));
}

TEST_CASE_METHOD(BearOnGround,
                 "A kinematic sphere (a hedgehog) that walks into the standing "
                 "bear touches it, ray casts for the ground do not find it",
                 "[physics][bear][animals]") {
  Land();
  const float radius = 1.4f;
  vec3 position(6.0f, standing_height, 0.0f);
  BodyHandle hedgehog = world.CreateKinematicSphere(position, radius);
  for (int i = 0; i < 4 * 60 && !Contains(touched_bodies, hedgehog); ++i) {
    position.x -= 1.5f * kFrame;  // walks towards the bear
    world.MoveKinematic(hedgehog, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        kFrame);
    world.Update(kFrame);
    bear.Move(kDown * kFrame, kFrame);
  }
  CHECK(Contains(touched_bodies, hedgehog));
  CHECK_THAT(world.GetTransform(hedgehog)[3].x,
             WithinAbs(position.x, 0.01f));  // where it was moved to

  RayHit hit;
  CHECK(world.CastRay(position + vec3(0.0f, 5.0f, 0.0f),
                      vec3(0.0f, -20.0f, 0.0f), hit, true));
  CHECK(hit.body == ground);
  CHECK(world.CastRay(position + vec3(0.0f, 5.0f, 0.0f),
                      vec3(0.0f, -20.0f, 0.0f), hit, false));
  CHECK(hit.body == hedgehog);

  SECTION("Moving it without time puts it there at once") {
    world.MoveKinematic(hedgehog, vec3(20.0f, 0.0f, 0.0f),
                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
    CHECK_THAT(world.GetTransform(hedgehog)[3].x, WithinAbs(20.0f, 1e-4f));
  }
}

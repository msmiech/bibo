#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include <vector>

#include "Physics/Physics.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Scene/BearCharacter.hpp"
#include "Scene/RestingBear.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using namespace bibo::test;
using bibo::scene::BearCharacter;
using bibo::scene::BearInput;
using bibo::scene::BearRig;
using bibo::scene::FaceControls;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const float FRAME = 1.0f / 60.0f;
using State = BearCharacter::State;

/**
 * The bear on flat ground (top at y = 0), looking along +z, optionally with a
 * rock in its way
 */
struct BearOnGround {
  PhysicsWorld world;
  BodyHandle rock = INVALID_BODY;
  std::unique_ptr<BearCharacter> bear;
  std::vector<State> states;

  explicit BearOnGround(bool withRock = false) {
    world.createStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(200.0f, 1.0f, 200.0f));
    if (withRock) {
      rock = world.createStaticSphere(
          vec3(0.0f, 0.7f, 9.0f),
          0.7f);  // a small rock (the rocks of the track are about 1 m)
    }
    world.optimizeBroadPhase();
    bear = std::make_unique<BearCharacter>(
        world, bearModel(), vec3(0.0f, 1.4f, 0.0f), vec3(0.0f, 0.0f, 1.0f),
        [this](const CharacterController::Hit& hit) {
          if (hit.body == rock) {
            bear->knockDown(hit);
          }
        });
  }

  void run(float seconds, BearInput input = {}) {
    int frames = static_cast<int>(std::lround(seconds / FRAME));
    for (int i = 0; i < frames; ++i) {
      world.update(FRAME);
      bear->update(FRAME, input);
      if (states.empty() || states.back() != bear->getState()) {
        states.push_back(bear->getState());
      }
    }
  }

  /**
   * @return How far the paw of a leg is from where it should be on the ground
   */
  float pawError(BearRig::Leg leg) const {
    const BearRig& rig = bear->getRig();
    const char* const paws[] = {"hand.L", "hand.R", "foot.L", "foot.R"};
    vec3 paw = rig.getBonePosition(rig.getBone(paws[leg]));
    return glm::distance(
        paw, rig.getFootTarget(leg) + vec3(0.0f, rig.getPawHeight(leg), 0.0f));
  }
};

BearInput forward() {
  BearInput input;
  input.forward = 1.0f;
  return input;
}
}  // namespace

TEST_CASE("The bear stands with its paws on the ground",
          "[assets][bear][animation]") {
  BearOnGround scene;
  scene.run(1.0f);

  CHECK(scene.bear->getState() == State::CONTROL);
  for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
    CAPTURE(leg);
    CHECK(scene.bear->getRig().isFootPlanted(static_cast<BearRig::Leg>(leg)));
    CHECK_THAT(
        scene.bear->getRig().getFootTarget(static_cast<BearRig::Leg>(leg)).y,
        WithinAbs(0.0f, 0.01f));
    CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.02f);
  }
  SECTION("The skinned bear is where the character is") {
    vec3 low(1e9f), high(-1e9f);
    for (const vec3& p : scene.bear->getPositions()) {
      low = glm::min(low, p);
      high = glm::max(high, p);
    }
    vec3 center = 0.5f * (low + high);
    vec3 position = scene.bear->getPosition();
    CHECK(glm::distance(vec3(center.x, 0.0f, center.z),
                        vec3(position.x, 0.0f, position.z)) < 0.4f);
    CHECK(center.y > 0.6f);
    CHECK(center.y < 1.5f);
  }
}

TEST_CASE(
    "Running: the paws stay where they were planted and the bear speeds up "
    "smoothly",
    "[assets][bear][animation]") {
  BearOnGround scene;
  scene.run(0.5f);
  vec3 start = scene.bear->getPosition();

  scene.run(0.1f, forward());
  float afterTenthSecond = scene.bear->getPosition().z - start.z;
  CHECK(afterTenthSecond > 0.05f);
  CHECK(afterTenthSecond < 1.0f);  // not at full speed at once

  std::array<vec3, BearRig::LEG_COUNT> planted;
  std::array<bool, BearRig::LEG_COUNT> wasPlanted{};
  int plantedFrames = 0;
  for (int frame = 0; frame < 120; ++frame) {
    scene.run(FRAME, forward());
    for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
      BearRig::Leg l = static_cast<BearRig::Leg>(leg);
      bool isPlanted = scene.bear->getRig().isFootPlanted(l);
      if (isPlanted && wasPlanted[leg]) {
        // A planted paw does not slide
        CHECK(glm::distance(scene.bear->getRig().getFootTarget(l),
                            planted[leg]) < 1e-4f);
        CHECK(scene.pawError(l) < 0.12f);
        ++plantedFrames;
      }
      planted[leg] = scene.bear->getRig().getFootTarget(l);
      wasPlanted[leg] = isPlanted;
    }
  }
  CHECK(plantedFrames > 50);
  CHECK_THAT(scene.bear->getPosition().z - start.z,
             WithinAbs(0.1f * 3.0f + 2.0f * 10.0f, 2.0f));
}

TEST_CASE(
    "Running into a rock knocks the bear down, it tumbles, gets up and can run "
    "again",
    "[assets][bear][ragdoll]") {
  BearOnGround scene(true);
  scene.run(0.5f);
  scene.run(2.0f, forward());
  REQUIRE(scene.bear->getState() == State::RAGDOLL);

  SECTION("The fur is ruffled") {
    float maxBend = 0.0f;
    for (int i = 0; i < 20; ++i) {
      scene.run(FRAME);
      for (const vec3& offset : scene.bear->getFurOffsets()) {
        maxBend = std::max(maxBend, glm::length(offset));
      }
    }
    CHECK(maxBend > 0.3f);
  }
  SECTION("Gets up after it lies still") {
    for (int i = 0; i < 8 * 60 && !(scene.states.size() > 2 &&
                                    scene.bear->getState() == State::CONTROL);
         ++i) {
      scene.run(FRAME);
    }
    REQUIRE(scene.bear->getState() == State::CONTROL);
    CHECK(scene.states ==
          std::vector<State>({State::CONTROL, State::RAGDOLL, State::GETTING_UP,
                              State::CONTROL}));
    // On its feet again, not inside of the rock
    vec3 position = scene.bear->getPosition();
    CHECK(glm::distance(position, vec3(0.0f, 0.7f, 9.0f)) > 0.7f + 0.9f);
    CHECK_THAT(position.y, WithinAbs(1.35f, 0.1f));
    for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
      CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.05f);
    }
    CHECK(scene.bear->isProtected());

    SECTION("Right after getting up a hit does not knock it down again") {
      CharacterController::Hit hit;
      hit.normal = vec3(0.0f, 0.0f, -1.0f);
      scene.bear->knockDown(hit);
      scene.run(FRAME);
      CHECK(scene.bear->getState() == State::CONTROL);
    }
    SECTION("It runs again (away from the rock)") {
      BearInput turnAround;
      turnAround.turn = 1.0f;
      scene.run(2.0f, turnAround);
      vec3 before = scene.bear->getPosition();
      scene.run(1.0f, forward());
      CHECK(glm::distance(before, scene.bear->getPosition()) > 5.0f);
    }
  }
  SECTION("A restart brings it back to the start, standing") {
    scene.bear->reset();
    CHECK(scene.bear->getState() == State::CONTROL);
    CHECK_THAT(scene.bear->getPosition(),
               IsNear(vec3(0.0f, 1.4f, 0.0f), 1e-3f));
    scene.run(0.5f);
    CHECK(scene.bear->getState() == State::CONTROL);
  }
}

TEST_CASE("The bones follow the ragdoll while the bear tumbles",
          "[assets][bear][ragdoll]") {
  BearOnGround scene(true);
  scene.run(0.5f);
  scene.run(2.0f, forward());
  REQUIRE(scene.bear->getState() == State::RAGDOLL);
  scene.run(0.5f);

  const BearRig& rig = scene.bear->getRig();
  CHECK(rig.getFace().lids > 0.99f);  // eyes shut
  std::vector<glm::mat4> parts;
  rig.ragdollTransforms(parts);
  // The bones with a ragdoll part are exactly where the part is (positions come
  // from the joints, so they match closely)
  CHECK(glm::distance(vec3(parts[BearRig::PART_PELVIS][3]),
                      rig.getBonePosition(rig.getBone("pelvis"))) < 1e-4f);
  CHECK(glm::distance(rig.getCenter(), vec3(parts[BearRig::PART_PELVIS][3])) <
        0.5f);
}

TEST_CASE("A bear lying on its back rolls over before it gets up",
          "[assets][bear][animation]") {
  const bibo::resources::SkinnedModel& model = bearModel();
  BearRig rig(model.skeleton);
  REQUIRE(rig.isValid());
  auto ground = [](vec3 from, vec3 to, vec3& point, vec3& normal) {
    if (from.y < 0.0f || to.y > 0.0f) {
      return false;
    }
    point = glm::mix(from, to, from.y / (from.y - to.y));
    normal = vec3(0.0f, 1.0f, 0.0f);
    return true;
  };
  bibo::scene::LocomotionInput input;
  rig.reset(input, ground);
  std::vector<glm::mat4> parts;
  rig.ragdollTransforms(parts);
  const float standingDuration = [&] {
    BearRig upright(model.skeleton);
    upright.reset(input, ground);
    upright.updateFromRagdoll(FRAME, parts);
    upright.beginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));
    return upright.getGetUpDuration();
  }();

  // Upside down: turned half around the length axis of the body, lying on the
  // ground
  glm::mat4 onBack =
      glm::translate(glm::mat4(1.0f), vec3(0.0f, 1.0f, 0.0f)) *
      glm::rotate(glm::mat4(1.0f), glm::pi<float>(), vec3(0.0f, 0.0f, 1.0f)) *
      glm::translate(glm::mat4(1.0f), vec3(0.0f, -1.0f, 0.0f));
  for (glm::mat4& part : parts) {
    part = onBack * part;
  }
  rig.updateFromRagdoll(FRAME, parts);
  BearRig::GetUpStart start = rig.beginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));

  // the roll takes extra time
  CHECK(rig.getGetUpDuration() > standingDuration + 0.5f);
  CHECK_THAT(start.direction, IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-3f));
  CHECK_THAT(start.rootPosition.y, WithinAbs(0.0f, 1e-3f));
  bool done = false;
  for (int i = 0; i < 600 && !done; ++i) {
    done = rig.updateGetUp(FRAME, ground, start.rootPosition);
  }
  CHECK(done);
  // Standing upright at the end: the head is above the pelvis
  CHECK(rig.getBonePosition(rig.getBone("head")).y >
        rig.getBonePosition(rig.getBone("pelvis")).y + 0.3f);
}

TEST_CASE(
    "After standing still for a while the bear plays its idle animations, "
    "moving interrupts them",
    "[assets][bear][animation]") {
  BearOnGround scene;
  scene.run(5.0f);
  CHECK_FALSE(scene.bear->getRig().isIdling());
  scene.run(2.0f);
  REQUIRE(scene.bear->getRig().isIdling());
  CHECK(scene.bear->getRig().getIdle() == BearRig::IDLE_STRETCH);

  SECTION("It sits down: the pelvis is low and the chest is above it") {
    scene.run(1.5f);
    const BearRig& rig = scene.bear->getRig();
    CHECK(rig.getBonePosition(rig.getBone("pelvis")).y < 0.6f);
    CHECK(rig.getBonePosition(rig.getBone("chest")).y >
          rig.getBonePosition(rig.getBone("pelvis")).y + 0.4f);
  }
  SECTION("The stretch ends standing, the yawn comes next") {
    scene.run(scene.bear->getRig().getIdleDuration(BearRig::IDLE_STRETCH));
    CHECK_FALSE(scene.bear->getRig().isIdling());
    scene.run(6.5f);
    REQUIRE(scene.bear->getRig().isIdling());
    CHECK(scene.bear->getRig().getIdle() == BearRig::IDLE_YAWN);
  }
  SECTION("Moving ends it at once and the bear runs off") {
    scene.run(2.0f);
    vec3 before = scene.bear->getPosition();
    scene.run(FRAME, forward());
    CHECK_FALSE(scene.bear->getRig().isIdling());
    scene.run(1.0f, forward());
    CHECK(scene.bear->getPosition().z - before.z > 5.0f);
    for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
      CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.15f);
    }
  }
}

TEST_CASE("The eyes blink and look around", "[assets][bear][animation]") {
  BearOnGround scene;
  float maxLids = 0.0f;
  float minGaze = 1.0f, maxGaze = -1.0f;
  for (int i = 0; i < 5 * 60; ++i) {
    scene.run(FRAME);
    const bibo::scene::FaceControls& face = scene.bear->getRig().getFace();
    maxLids = std::max(maxLids, face.lids);
    minGaze = std::min(minGaze, face.gazeYaw);
    maxGaze = std::max(maxGaze, face.gazeYaw);
  }
  CHECK(maxLids > 0.9f);             // blinked
  CHECK(maxGaze - minGaze > 0.05f);  // saccades
}

namespace {
// Height of the center of the capsule above the ground when the bear stands
// (BearCharacter::characterControllerDesc)
const float STANDING_HEIGHT = 1.35f;

BearInput jumpInput(bool held, BearInput input = {}) {
  input.jump = held;
  return input;
}

/**
 * Jumps on flat ground with the button held for the given time
 * @return Highest point above the standing height
 */
float jumpHeight(BearOnGround& scene, float holdSeconds) {
  float ground = scene.bear->getPosition().y;
  float highest = 0.0f;
  for (int frame = 0; frame < 120; ++frame) {
    scene.run(FRAME, jumpInput(frame * FRAME < holdSeconds));
    highest = std::max(highest, scene.bear->getPosition().y - ground);
  }
  return highest;
}
}  // namespace

TEST_CASE("Jumping: the longer the button is held, the higher the bear jumps",
          "[assets][bear][jump]") {
  BearOnGround scene;
  scene.run(1.0f);
  REQUIRE_FALSE(scene.bear->isAirborne());
  REQUIRE_THAT(scene.bear->getPosition().y, WithinAbs(STANDING_HEIGHT, 0.05f));

  float held = jumpHeight(scene, 1.0f);
  CHECK_THAT(held, WithinAbs(BearCharacter::JUMP_HEIGHT, 0.3f));
  // Lands again
  CHECK_FALSE(scene.bear->isAirborne());
  CHECK_THAT(scene.bear->getPosition().y, WithinAbs(STANDING_HEIGHT, 0.05f));

  float tapped = jumpHeight(scene, FRAME);
  CHECK(tapped > 1.0f);  // still clears a rock
  CHECK(tapped < 0.6f * held);
  float half = jumpHeight(scene, 0.5f * BearCharacter::JUMP_RISE_TIME);
  CHECK(half > tapped);
  CHECK(half < held);
}

TEST_CASE("Jumping: holding the button does not jump again after landing",
          "[assets][bear][jump]") {
  BearOnGround scene;
  scene.run(1.0f);
  int takeoffs = 0;
  bool wasAirborne = false;
  for (int frame = 0; frame < 3 * 60; ++frame) {
    scene.run(FRAME, jumpInput(true));
    if (scene.bear->isAirborne() && !wasAirborne) {
      ++takeoffs;
    }
    wasAirborne = scene.bear->isAirborne();
  }
  CHECK(takeoffs == 1);
  CHECK_FALSE(scene.bear->isAirborne());
}

TEST_CASE(
    "Jumping: pressed shortly before landing, the bear jumps again when it "
    "lands",
    "[assets][bear][jump]") {
  BearOnGround scene;
  scene.run(1.0f);
  float ground = scene.bear->getPosition().y;
  scene.run(FRAME, jumpInput(true));
  // Falls until it is almost on the ground, then the button is pressed again
  bool falling = false;
  for (int frame = 0;
       frame < 120 && !(falling && scene.bear->getPosition().y - ground < 0.6f);
       ++frame) {
    scene.run(FRAME);
    falling = scene.bear->getVelocity().y < 0.0f;
  }
  REQUIRE(scene.bear->isAirborne());
  float highest = 0.0f;
  bool landed = false;
  for (int frame = 0; frame < 60; ++frame) {
    scene.run(FRAME, jumpInput(true));
    landed = landed || !scene.bear->isAirborne();
    if (landed) {
      highest = std::max(highest, scene.bear->getPosition().y - ground);
    }
  }
  CHECK(highest > 1.0f);
}

TEST_CASE("Jumping: shortly after running over an edge the bear can still jump",
          "[assets][bear][jump]") {
  PhysicsWorld world;
  world.createStaticBox(
      vec3(0.0f, -1.0f, 0.0f),
      vec3(5.0f, 1.0f, 5.0f));  // platform with its top at y = 0, ends at z = 5
  world.createStaticBox(vec3(0.0f, -11.0f, 0.0f),
                        vec3(200.0f, 1.0f, 200.0f));  // 10 below
  world.optimizeBroadPhase();
  BearCharacter bear(world, bearModel(), vec3(0.0f, 1.4f, 0.0f),
                     vec3(0.0f, 0.0f, 1.0f), nullptr);
  auto run = [&](BearInput input) {
    world.update(FRAME);
    bear.update(FRAME, input);
  };
  BearInput forward;
  forward.forward = 1.0f;
  for (int frame = 0; frame < 60; ++frame) {
    run(BearInput());
  }
  for (int frame = 0; frame < 120 && !bear.isAirborne(); ++frame) {
    run(forward);
  }
  REQUIRE(bear.isAirborne());
  CHECK(bear.getPosition().z > 4.0f);

  SECTION("Right after leaving the edge") {
    run(forward);
    run(jumpInput(true, forward));
    run(jumpInput(true, forward));
    CHECK(bear.getVelocity().y > 5.0f);
  }
  SECTION("Too late") {
    for (int frame = 0; frame < 15; ++frame) {
      run(forward);
    }
    run(jumpInput(true, forward));
    run(jumpInput(true, forward));
    CHECK(bear.getVelocity().y < 0.0f);
  }
  SECTION("Falling off an edge starts slowly (no sudden drop)") {
    float speed = bear.getVelocity().y;
    // gravity of a frame or two (earlier versions fell at 59 m/s at once)
    CHECK(speed > -3.0f);
    run(forward);
    CHECK(bear.getVelocity().y < speed);
  }
}

TEST_CASE(
    "Jumping: the legs tuck in, reach for the ground before landing and the "
    "paws land where they reached",
    "[assets][bear][jump][animation]") {
  BearOnGround scene;
  scene.run(1.0f);
  scene.run(1.0f, forward());  // at full speed
  const BearRig& rig = scene.bear->getRig();
  auto frontPawHeight = [&]() {
    float root = scene.bear->getPosition().y - STANDING_HEIGHT;
    return 0.5f * (rig.getFootTarget(BearRig::FRONT_LEFT).y +
                   rig.getFootTarget(BearRig::FRONT_RIGHT).y) -
           root;
  };

  float atTop = -1.0f, beforeLanding = -1.0f;
  std::array<vec3, BearRig::LEG_COUNT> before{};
  float largestStep = 0.0f;
  bool landed = false;
  int framesAfterLanding = 0;
  for (int frame = 0; frame < 120 && framesAfterLanding < 15; ++frame) {
    for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
      before[leg] = rig.getFootTarget(static_cast<BearRig::Leg>(leg));
    }
    float vertical = scene.bear->getVelocity().y;
    scene.run(FRAME, jumpInput(frame < 30, forward()));
    if (vertical > 0.0f && scene.bear->getVelocity().y <= 0.0f) {
      atTop = frontPawHeight();
    }
    landed = landed || (frame > 5 && !scene.bear->isAirborne());
    if (!landed) {
      beforeLanding = frontPawHeight();
      continue;
    }
    // Landing: the paws are put on the ground where they are and go on into the
    // steps, they do not jump
    ++framesAfterLanding;
    for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
      vec3 now = rig.getFootTarget(static_cast<BearRig::Leg>(leg));
      // Sideways and up or down (forward the bear moves 1/6 in a frame)
      largestStep = std::max(
          largestStep,
          glm::length(glm::vec2(now.x - before[leg].x, now.y - before[leg].y)));
    }
  }
  REQUIRE(landed);
  CHECK(atTop > 0.2f);          // tucked in at the highest point
  CHECK(beforeLanding < 0.1f);  // stretched towards the ground
  CHECK(largestStep < 0.12f);

  // Runs on with all paws on the ground
  scene.run(0.5f, forward());
  for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg) {
    BearRig::Leg l = static_cast<BearRig::Leg>(leg);
    if (rig.isFootPlanted(l)) {
      CHECK(scene.pawError(l) < 0.12f);
    }
  }
}

TEST_CASE("Steering: the bear turns towards where it should go and runs there",
          "[assets][bear]") {
  BearOnGround scene;  // looks along +z
  scene.run(0.5f);
  BearInput right;
  // the model looks along +z, its right is -x
  right.move = vec3(-1.0f, 0.0f, 0.0f);

  scene.run(0.2f, right);
  vec3 turning = scene.bear->getDirection();
  CHECK(turning.x < -0.2f);  // turns right away
  CHECK(turning.z > 0.0f);   // but not at once

  scene.run(1.0f, right);
  CHECK(scene.bear->getDirection().x < -0.999f);
  CHECK(scene.bear->getVelocity().x < -8.0f);

  SECTION("Turning around slows the bear down, it does not run sideways") {
    BearInput back;
    back.move = vec3(1.0f, 0.0f, 0.0f);
    scene.run(0.15f, back);
    CHECK(glm::length(scene.bear->getVelocity()) < 8.0f);
    scene.run(1.5f, back);
    CHECK(scene.bear->getDirection().x > 0.999f);
    CHECK(scene.bear->getVelocity().x > 8.0f);
  }
  SECTION("Half way on the stick: slower") {
    BearInput slow;
    slow.move = vec3(-0.5f, 0.0f, 0.0f);
    scene.run(1.0f, slow);
    CHECK_THAT(scene.bear->getVelocity().x,
               WithinAbs(-0.5f * BearCharacter::FORWARD_SPEED, 0.5f));
  }
}

TEST_CASE(
    "A caught fish stays in the mouth in the air, on the ground the bear sits "
    "down and eats it",
    "[assets][bear][animation][animals]") {
  BearOnGround scene;
  scene.run(1.0f);
  BearInput jump = forward();
  jump.jump = true;
  scene.run(0.1f, jump);
  REQUIRE(scene.bear->isAirborne());
  REQUIRE(scene.bear->canCatchFish());
  REQUIRE(scene.bear->catchFish());
  CHECK_FALSE(scene.bear->canCatchFish());  // only one at a time
  CHECK_FALSE(scene.bear->catchFish());

  // Carried in the mouth while it flies on
  scene.run(0.1f, jump);
  REQUIRE(scene.bear->getFish() == BearCharacter::Fish::CARRIED);
  BearCharacter::HeldFish held;
  REQUIRE(scene.bear->getHeldFish(held));
  CHECK(glm::distance(held.center, scene.bear->getMouthPosition()) < 0.2f);
  CHECK(held.bites == 0);

  // Lands: eats it, the player cannot run off before the last bite
  for (int i = 0;
       i < 120 && scene.bear->getFish() == BearCharacter::Fish::CARRIED; ++i) {
    scene.run(FRAME, forward());
  }
  REQUIRE(scene.bear->getFish() == BearCharacter::Fish::EATING);
  CHECK(scene.bear->getRig().getIdle() == BearRig::IDLE_EAT);
  vec3 start = scene.bear->getPosition();
  scene.run(BearRig::EAT_BITES[0] + 0.05f, forward());
  // only slows down
  CHECK(glm::distance(scene.bear->getPosition(), start) < 1.0f);
  REQUIRE(scene.bear->getHeldFish(held));
  CHECK(held.bites == 1);
  CHECK(glm::distance(held.center, scene.bear->getMouthPosition()) < 0.6f);
  {
    // It sits: the pelvis is low, and holds the fish with its front paws
    const BearRig& rig = scene.bear->getRig();
    CHECK(rig.getBonePosition(rig.getBone("pelvis")).y < 0.6f);
    vec3 paws = 0.5f * (rig.getBonePosition(rig.getBone("hand.L")) +
                        rig.getBonePosition(rig.getBone("hand.R")));
    CHECK(glm::distance(paws, held.center) < 0.8f);
  }
  CHECK(scene.bear->takeEatenFish() == 0);  // not eaten up yet
  scene.run(BearRig::EAT_BITES[2] - BearRig::EAT_BITES[0], forward());
  CHECK_FALSE(scene.bear->getHeldFish(held));  // all three bites taken
  CHECK(scene.bear->takeEatenFish() == 1);
  CHECK(scene.bear->takeEatenFish() == 0);  // counted once

  SECTION("After the last bite the player can run on") {
    vec3 before = scene.bear->getPosition();
    scene.run(1.0f, forward());
    CHECK(scene.bear->getFish() == BearCharacter::Fish::NONE);
    CHECK(glm::distance(scene.bear->getPosition(), before) > 4.0f);
    CHECK(scene.bear->canCatchFish());
  }
  SECTION("Without input it licks its lips and gets up") {
    scene.run(scene.bear->getRig().getIdleDuration(BearRig::IDLE_EAT) -
              BearRig::EAT_BITES[2] + 0.1f);
    CHECK_FALSE(scene.bear->getRig().isIdling());
    CHECK(scene.bear->getFish() == BearCharacter::Fish::NONE);
  }
}

TEST_CASE(
    "Mishka lies on its belly, watches what it is shown and talks with its "
    "mouth",
    "[assets][bear][animals]") {
  const vec3 UP(0.0f, 1.0f, 0.0f);
  // Flat ground at y = 0
  bibo::scene::GroundProbe probe = [&](vec3 from, vec3 to, vec3& point,
                                       vec3& normal) {
    if (from.y < 0.0f || to.y > 0.0f) {
      return false;
    }
    point = vec3(to.x, 0.0f, to.z);
    normal = UP;
    return true;
  };
  bibo::scene::RestingBear mishka(bearModel(), vec3(0.0f),
                                  vec3(0.0f, 0.0f, 1.0f), probe);
  BearRig standing(bearModel().skeleton);
  standing.reset(bibo::scene::LocomotionInput(), probe);

  const BearRig& rig = mishka.getRig();
  CHECK(rig.getIdle() == BearRig::IDLE_LIE);
  auto checkLying = [&]() {
    CHECK(rig.getCenter().y < standing.getCenter().y - 0.25f);
    for (const char* paw : {"hand.L", "hand.R", "foot.L", "foot.R"}) {
      CAPTURE(paw);
      CHECK(rig.getBonePosition(rig.getBone(paw)).y < 0.3f);  // on the ground
    }
    float lowest = std::numeric_limits<float>::max();
    for (const vec3& position : mishka.getPositions()) {
      lowest = std::min(lowest, position.y);
    }
    CHECK(lowest > -0.12f);  // not into the ground
    CHECK(lowest < 0.15f);
  };
  checkLying();
  // It does not get up
  for (int i = 0; i < 40 * 60; ++i) {
    mishka.update(FRAME, nullptr);
  }
  REQUIRE(rig.getIdle() == BearRig::IDLE_LIE);
  checkLying();

  // The head follows what it watches (to its left: +x)
  auto watch = [&](vec3 target) {
    for (int i = 0; i < 3 * 60; ++i) {
      mishka.update(FRAME, &target);
    }
    return mishka.getFacePosition();
  };
  vec3 left = watch(vec3(4.0f, 1.0f, 4.0f));
  vec3 right = watch(vec3(-4.0f, 1.0f, 4.0f));
  CHECK(left.x > right.x + 0.06f);
  checkLying();

  // Bigger than the cub: everything of the rig is scaled around the point on
  // the ground under it
  CHECK(mishka.getFacePosition() == mishka.scaled(rig.getHeadPosition()));
  CHECK(mishka.getFacePosition().y > 1.2f * rig.getHeadPosition().y);
  CHECK(mishka.getBoundsCenter() == mishka.scaled(rig.getCenter()));

  mishka.speak();
  mishka.update(0.1f, nullptr);
  CHECK(mishka.getMouthOpen() > 0.2f);
  mishka.update(0.15f, nullptr);
  CHECK(mishka.getMouthOpen() == 0.0f);
}

TEST_CASE(
    "Mishka has the coat of a moon bear: black, a cream crescent on the chest, "
    "a tan muzzle",
    "[assets][bear][animals]") {
  const bibo::resources::SkinnedModel& cub = bearModel();
  bibo::resources::SkinnedModel model = cub;
  bibo::scene::RestingBear::paintCoat(model);
  const BearRig rig(cub.skeleton);
  vec3 eye = vec3(
      glm::inverse(cub.skeleton.bones[rig.getBone("eye.L")].inverseBind)[3]);
  vec3 neck = vec3(
      glm::inverse(cub.skeleton.bones[rig.getBone("neck")].inverseBind)[3]);
  auto brightness = [](glm::vec4 color) {
    return (color.r + color.g + color.b) / 3.0f;
  };
  int fur = 0, darker = 0, crescent = 0, muzzle = 0;
  for (std::size_t i = 0; i < model.colors.size(); ++i) {
    CHECK(model.colors[i].a == cub.colors[i].a);  // the length of the fur
    if (static_cast<int>(model.surfaces[i]) !=
        bibo::resources::SkinnedModel::FUR) {
      CHECK(model.colors[i] == cub.colors[i]);  // skin, eyes, nose, claws
      continue;
    }
    ++fur;
    darker +=
        brightness(model.colors[i]) < 0.5f * brightness(cub.colors[i]) ? 1 : 0;
    vec3 point = model.positions[i];
    if (brightness(model.colors[i]) > 0.4f) {
      ++crescent;
      // On the front of the chest, below the neck
      CHECK(point.y < neck.y);
      CHECK(point.z > 0.3f);
    }
    muzzle +=
        point.z > eye.z && point.y < eye.y && model.colors[i].r > 0.2f ? 1 : 0;
  }
  CAPTURE(fur, darker, crescent, muzzle);
  CHECK(darker > fur / 2);  // black, not brown
  CHECK(crescent > 50);
  CHECK(muzzle > 20);
}

TEST_CASE("A knocked down bear drops its fish",
          "[assets][bear][ragdoll][animals]") {
  BearOnGround scene;
  scene.run(0.5f);
  BearInput jump = forward();
  jump.jump = true;
  scene.run(0.1f, jump);
  REQUIRE(scene.bear->catchFish());
  CharacterController::Hit hit;
  hit.position = scene.bear->getPosition() + vec3(0.0f, 0.0f, 1.0f);
  hit.normal = vec3(0.0f, 0.0f, -1.0f);
  scene.bear->knockDown(hit);
  scene.run(FRAME, jump);

  REQUIRE(scene.bear->getState() == State::RAGDOLL);
  CHECK(scene.bear->getFish() == BearCharacter::Fish::NONE);
  BearCharacter::HeldFish held;
  CHECK_FALSE(scene.bear->getHeldFish(held));
  CHECK_FALSE(scene.bear->canCatchFish());  // not while it tumbles
  scene.run(5.0f);
  CHECK(scene.bear->takeEatenFish() == 0);  // it did not eat it
}

namespace {
/**
 * The bear on a shore (top at y = 0) in front of deep water from z = 8 to z =
 * 22 (its floor at y = -3), another shore behind it. The water surface is a bit
 * below the shores.
 */
struct BearAtWater {
  static constexpr float WATER_LEVEL = -0.4f;
  PhysicsWorld world;
  std::unique_ptr<BearCharacter> bear;

  BearAtWater() {
    world.createStaticBox(vec3(0.0f, -1.0f, -12.0f),
                          vec3(50.0f, 1.0f, 20.0f));  // z up to 8
    world.createStaticBox(vec3(0.0f, -4.0f, 15.0f),
                          vec3(50.0f, 1.0f, 7.0f));  // the floor of the water
    world.createStaticBox(vec3(0.0f, -1.0f, 42.0f),
                          vec3(50.0f, 1.0f, 20.0f));  // from z = 22
    world.optimizeBroadPhase();
    bear = std::make_unique<BearCharacter>(world, bearModel(),
                                           vec3(0.0f, 1.4f, 0.0f),
                                           vec3(0.0f, 0.0f, 1.0f), nullptr);
    bear->setWaterLevel(WATER_LEVEL);
  }

  void run(float seconds, BearInput input = {}) {
    int frames = static_cast<int>(std::lround(seconds / FRAME));
    for (int i = 0; i < frames; ++i) {
      world.update(FRAME);
      bear->update(FRAME, input);
    }
  }

  /**
   * Runs forward until the bear swims
   */
  void runIntoWater() {
    for (int i = 0; i < 4 * 60 && !bear->isSwimming(); ++i) {
      run(FRAME, forward());
    }
    REQUIRE(bear->isSwimming());
  }
};
}  // namespace

TEST_CASE(
    "In deep water the bear swims: it floats with its head out of the water "
    "and paddles",
    "[assets][bear][swimming]") {
  BearAtWater scene;
  scene.run(0.5f);
  CHECK_FALSE(scene.bear->isSwimming());  // on the shore
  CHECK(scene.bear->getWaterDepth() < 0.0f);
  scene.runIntoWater();
  CHECK(scene.bear->getPosition().z > 8.0f);
  CHECK(scene.bear->takeSplashes() == 1);
  CHECK(scene.bear->takeSplashes() == 0);
  CHECK_FALSE(scene.bear->isAirborne());

  // Slower than on land, it floats at its depth (before it reaches the other
  // shore)
  scene.run(1.5f, forward());
  REQUIRE(scene.bear->isSwimming());
  glm::vec3 velocity = scene.bear->getVelocity();
  CAPTURE(velocity, scene.bear->getWaterDepth());
  CHECK(glm::length(glm::vec2(velocity.x, velocity.z)) <=
        BearCharacter::SWIM_SPEED + 0.1f);
  CHECK(glm::length(glm::vec2(velocity.x, velocity.z)) >
        0.5f * BearCharacter::SWIM_SPEED);
  CHECK_THAT(scene.bear->getWaterDepth(),
             WithinAbs(BearCharacter::FLOAT_DEPTH, 0.15f));
  const BearRig& rig = scene.bear->getRig();
  CHECK(rig.getBonePosition(rig.getBone("head")).y >
        BearAtWater::WATER_LEVEL + 0.2f);
  CHECK(rig.getBonePosition(rig.getBone("pelvis")).y <
        BearAtWater::WATER_LEVEL + 0.4f);

  SECTION("The paws paddle in circles, they do not stand on anything") {
    vec3 before =
        rig.getFootTarget(BearRig::FRONT_LEFT) - scene.bear->getPosition();
    scene.run(0.3f);
    vec3 after =
        rig.getFootTarget(BearRig::FRONT_LEFT) - scene.bear->getPosition();
    CHECK(glm::distance(before, after) > 0.05f);
    CHECK_FALSE(rig.isIdling());
  }
  SECTION("Without input it stays floating where it is (no idle animations)") {
    scene.run(10.0f);
    CHECK(scene.bear->isSwimming());
    CHECK_FALSE(scene.bear->getRig().isIdling());
    CHECK(glm::length(scene.bear->getVelocity()) < 0.5f);
  }
  SECTION("A jump at the surface takes it out of the water onto the shore") {
    BearInput swimAndJump = forward();
    for (int i = 0; i < 12 * 60 && scene.bear->getPosition().z < 24.0f; ++i) {
      swimAndJump.jump = (i / 15) % 2 == 0;  // presses again and again
      scene.run(FRAME, swimAndJump);
    }
    scene.run(1.0f, forward());
    CAPTURE(scene.bear->getPosition());
    CHECK(scene.bear->getPosition().z > 24.0f);
    CHECK_FALSE(scene.bear->isSwimming());
    CHECK(scene.bear->getWaterDepth() < 0.0f);
  }
}

TEST_CASE("Without a water level the bear never swims",
          "[assets][bear][swimming]") {
  BearAtWater scene;
  scene.bear->setWaterLevel(std::numeric_limits<float>::lowest());
  scene.run(3.0f, forward());
  CHECK_FALSE(scene.bear->isSwimming());
  CHECK(scene.bear->getPosition().y < -1.0f);  // at the bottom of the water
}

TEST_CASE("The bear can be placed somewhere else, standing", "[assets][bear]") {
  BearOnGround scene;
  scene.run(1.0f, forward());
  scene.bear->placeAt(vec3(20.0f, 1.4f, -5.0f), vec3(-1.0f, 0.0f, 0.0f));
  CHECK(scene.bear->getState() == State::CONTROL);
  CHECK(glm::distance(scene.bear->getPosition(), vec3(20.0f, 1.4f, -5.0f)) <
        1e-3f);
  CHECK(glm::distance(scene.bear->getDirection(), vec3(-1.0f, 0.0f, 0.0f)) <
        1e-4f);
  CHECK(glm::length(scene.bear->getVelocity()) < 1e-4f);
  scene.run(1.0f);
  CHECK(glm::distance(scene.bear->getPosition(), vec3(20.0f, 1.4f, -5.0f)) <
        0.3f);

  SECTION("reset goes back to the start, which can be changed") {
    scene.bear->setStart(vec3(-3.0f, 1.4f, 2.0f), vec3(0.0f, 0.0f, -1.0f));
    scene.bear->reset();
    CHECK(glm::distance(scene.bear->getPosition(), vec3(-3.0f, 1.4f, 2.0f)) <
          1e-3f);
    CHECK(glm::distance(scene.bear->getDirection(), vec3(0.0f, 0.0f, -1.0f)) <
          1e-4f);
  }
}

TEST_CASE(
    "Looking at a point: the head and the eyes turn to it, the bear does not "
    "start an idle animation meanwhile",
    "[assets][bear][animation]") {
  BearOnGround scene;
  scene.run(1.0f);
  const vec3 head = scene.bear->getHeadPosition();
  // The bear looks along +z, its left is +x. The eyes are there first, the head
  // follows.
  FaceControls face;
  auto lookAt = [&](vec3 offset) {
    scene.bear->lookAt(head + offset);
    scene.run(0.15f);
    face = scene.bear->getRig().getFace();
    scene.run(1.35f);
    return scene.bear->getMouthPosition() - scene.bear->getHeadPosition();
  };
  vec3 left = lookAt(vec3(3.0f, 0.0f, 4.0f));
  CHECK(face.gazeYaw > 0.1f);
  vec3 right = lookAt(vec3(-3.0f, 0.0f, 4.0f));
  CHECK(face.gazeYaw < -0.1f);
  vec3 up = lookAt(vec3(0.0f, 3.0f, 3.0f));
  CHECK(face.gazePitch > 0.1f);
  vec3 down = lookAt(vec3(0.0f, -1.0f, 4.0f));
  CAPTURE(left, right, up, down);
  CHECK(left.x > right.x + 0.15f);
  CHECK(up.y > down.y + 0.1f);

  // Paying attention: no stretching or yawning
  scene.run(10.0f);
  CHECK_FALSE(scene.bear->getRig().isIdling());
  SECTION("Afterwards it looks around and plays its idle animations again") {
    scene.bear->stopLooking();
    scene.run(8.0f);
    CHECK(scene.bear->getRig().isIdling());
  }
}

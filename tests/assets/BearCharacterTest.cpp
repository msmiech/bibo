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

using bibo::physics::BodyHandle;
using bibo::physics::CharacterController;
using bibo::physics::kInvalidBody;
using bibo::physics::PhysicsWorld;
using bibo::test::BearModel;
using bibo::test::IsNear;

using bibo::scene::BearCharacter;
using bibo::scene::BearInput;
using bibo::scene::BearRig;
using bibo::scene::FaceControls;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;
using State = BearCharacter::State;

/**
 * The bear on flat ground (top at y = 0), looking along +z, optionally with a
 * rock in its way
 */
struct BearOnGround {
  PhysicsWorld world;
  BodyHandle rock = kInvalidBody;
  std::unique_ptr<BearCharacter> bear;
  std::vector<State> states;

  explicit BearOnGround(bool with_rock = false) {
    world.CreateStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(200.0f, 1.0f, 200.0f));
    if (with_rock) {
      rock = world.CreateStaticSphere(
          vec3(0.0f, 0.7f, 9.0f),
          0.7f);  // a small rock (the rocks of the track are about 1 m)
    }
    world.OptimizeBroadPhase();
    bear = std::make_unique<BearCharacter>(
        world, BearModel(), vec3(0.0f, 1.4f, 0.0f), vec3(0.0f, 0.0f, 1.0f),
        [this](const CharacterController::Hit& hit) {
          if (hit.body == rock) {
            bear->KnockDown(hit);
          }
        });
  }

  void Run(float seconds, BearInput input = {}) {
    int frames = static_cast<int>(std::lround(seconds / kFrame));
    for (int i = 0; i < frames; ++i) {
      world.Update(kFrame);
      bear->Update(kFrame, input);
      if (states.empty() || states.back() != bear->GetState()) {
        states.push_back(bear->GetState());
      }
    }
  }

  /**
   * @return How far the paw of a leg is from where it should be on the ground
   */
  float PawError(BearRig::Leg leg) const {
    const BearRig& rig = bear->GetRig();
    const char* const paws[] = {"hand.L", "hand.R", "foot.L", "foot.R"};
    vec3 paw = rig.GetBonePosition(rig.GetBone(paws[leg]));
    return glm::distance(
        paw, rig.GetFootTarget(leg) + vec3(0.0f, rig.GetPawHeight(leg), 0.0f));
  }
};

BearInput Forward() {
  BearInput input;
  input.forward = 1.0f;
  return input;
}
}  // namespace

TEST_CASE("The bear stands with its paws on the ground",
          "[assets][bear][animation]") {
  BearOnGround scene;
  scene.Run(1.0f);

  CHECK(scene.bear->GetState() == State::kControl);
  for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
    CAPTURE(leg);
    CHECK(scene.bear->GetRig().IsFootPlanted(static_cast<BearRig::Leg>(leg)));
    CHECK_THAT(
        scene.bear->GetRig().GetFootTarget(static_cast<BearRig::Leg>(leg)).y,
        WithinAbs(0.0f, 0.01f));
    CHECK(scene.PawError(static_cast<BearRig::Leg>(leg)) < 0.02f);
  }
  SECTION("The skinned bear is where the character is") {
    vec3 low(1e9f), high(-1e9f);
    for (const vec3& p : scene.bear->GetPositions()) {
      low = glm::min(low, p);
      high = glm::max(high, p);
    }
    vec3 center = 0.5f * (low + high);
    vec3 position = scene.bear->GetPosition();
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
  scene.Run(0.5f);
  vec3 start = scene.bear->GetPosition();

  scene.Run(0.1f, Forward());
  float after_tenth_second = scene.bear->GetPosition().z - start.z;
  CHECK(after_tenth_second > 0.05f);
  CHECK(after_tenth_second < 1.0f);  // not at full speed at once

  std::array<vec3, BearRig::kLegCount> planted;
  std::array<bool, BearRig::kLegCount> was_planted{};
  int planted_frames = 0;
  for (int frame = 0; frame < 120; ++frame) {
    scene.Run(kFrame, Forward());
    for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
      BearRig::Leg l = static_cast<BearRig::Leg>(leg);
      bool is_planted = scene.bear->GetRig().IsFootPlanted(l);
      if (is_planted && was_planted[leg]) {
        // A planted paw does not slide
        CHECK(glm::distance(scene.bear->GetRig().GetFootTarget(l),
                            planted[leg]) < 1e-4f);
        CHECK(scene.PawError(l) < 0.12f);
        ++planted_frames;
      }
      planted[leg] = scene.bear->GetRig().GetFootTarget(l);
      was_planted[leg] = is_planted;
    }
  }
  CHECK(planted_frames > 50);
  CHECK_THAT(scene.bear->GetPosition().z - start.z,
             WithinAbs(0.1f * 3.0f + 2.0f * 10.0f, 2.0f));
}

TEST_CASE(
    "Running into a rock knocks the bear down, it tumbles, gets up and can run "
    "again",
    "[assets][bear][ragdoll]") {
  BearOnGround scene(true);
  scene.Run(0.5f);
  scene.Run(2.0f, Forward());
  REQUIRE(scene.bear->GetState() == State::kRagdoll);

  SECTION("The fur is ruffled") {
    float max_bend = 0.0f;
    for (int i = 0; i < 20; ++i) {
      scene.Run(kFrame);
      for (const vec3& offset : scene.bear->GetFurOffsets()) {
        max_bend = std::max(max_bend, glm::length(offset));
      }
    }
    CHECK(max_bend > 0.3f);
  }
  SECTION("Gets up after it lies still") {
    for (int i = 0; i < 8 * 60 && !(scene.states.size() > 2 &&
                                    scene.bear->GetState() == State::kControl);
         ++i) {
      scene.Run(kFrame);
    }
    REQUIRE(scene.bear->GetState() == State::kControl);
    CHECK(scene.states ==
          std::vector<State>({State::kControl, State::kRagdoll,
                              State::kGettingUp, State::kControl}));
    // On its feet again, not inside of the rock
    vec3 position = scene.bear->GetPosition();
    CHECK(glm::distance(position, vec3(0.0f, 0.7f, 9.0f)) > 0.7f + 0.9f);
    CHECK_THAT(position.y, WithinAbs(1.35f, 0.1f));
    for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
      CHECK(scene.PawError(static_cast<BearRig::Leg>(leg)) < 0.05f);
    }
    CHECK(scene.bear->IsProtected());

    SECTION("Right after getting up a hit does not knock it down again") {
      CharacterController::Hit hit;
      hit.normal = vec3(0.0f, 0.0f, -1.0f);
      scene.bear->KnockDown(hit);
      scene.Run(kFrame);
      CHECK(scene.bear->GetState() == State::kControl);
    }
    SECTION("It runs again (away from the rock)") {
      BearInput turn_around;
      turn_around.turn = 1.0f;
      scene.Run(2.0f, turn_around);
      vec3 before = scene.bear->GetPosition();
      scene.Run(1.0f, Forward());
      CHECK(glm::distance(before, scene.bear->GetPosition()) > 5.0f);
    }
  }
  SECTION("A restart brings it back to the start, standing") {
    scene.bear->Reset();
    CHECK(scene.bear->GetState() == State::kControl);
    CHECK_THAT(scene.bear->GetPosition(),
               IsNear(vec3(0.0f, 1.4f, 0.0f), 1e-3f));
    scene.Run(0.5f);
    CHECK(scene.bear->GetState() == State::kControl);
  }
}

TEST_CASE("The bones follow the ragdoll while the bear tumbles",
          "[assets][bear][ragdoll]") {
  BearOnGround scene(true);
  scene.Run(0.5f);
  scene.Run(2.0f, Forward());
  REQUIRE(scene.bear->GetState() == State::kRagdoll);
  scene.Run(0.5f);

  const BearRig& rig = scene.bear->GetRig();
  CHECK(rig.GetFace().lids > 0.99f);  // eyes shut
  std::vector<glm::mat4> parts;
  rig.RagdollTransforms(parts);
  // The bones with a ragdoll part are exactly where the part is (positions come
  // from the joints, so they match closely)
  CHECK(glm::distance(vec3(parts[BearRig::kPartPelvis][3]),
                      rig.GetBonePosition(rig.GetBone("pelvis"))) < 1e-4f);
  CHECK(glm::distance(rig.GetCenter(), vec3(parts[BearRig::kPartPelvis][3])) <
        0.5f);
}

TEST_CASE("A bear lying on its back rolls over before it gets up",
          "[assets][bear][animation]") {
  const bibo::resources::SkinnedModel& model = BearModel();
  BearRig rig(model.skeleton);
  REQUIRE(rig.IsValid());
  auto ground = [](vec3 from, vec3 to, vec3& point, vec3& normal) {
    if (from.y < 0.0f || to.y > 0.0f) {
      return false;
    }
    point = glm::mix(from, to, from.y / (from.y - to.y));
    normal = vec3(0.0f, 1.0f, 0.0f);
    return true;
  };
  bibo::scene::LocomotionInput input;
  rig.Reset(input, ground);
  std::vector<glm::mat4> parts;
  rig.RagdollTransforms(parts);
  const float standing_duration = [&] {
    BearRig upright(model.skeleton);
    upright.Reset(input, ground);
    upright.UpdateFromRagdoll(kFrame, parts);
    upright.BeginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));
    return upright.GetGetUpDuration();
  }();

  // Upside down: turned half around the length axis of the body, lying on the
  // ground
  glm::mat4 on_back =
      glm::translate(glm::mat4(1.0f), vec3(0.0f, 1.0f, 0.0f)) *
      glm::rotate(glm::mat4(1.0f), glm::pi<float>(), vec3(0.0f, 0.0f, 1.0f)) *
      glm::translate(glm::mat4(1.0f), vec3(0.0f, -1.0f, 0.0f));
  for (glm::mat4& part : parts) {
    part = on_back * part;
  }
  rig.UpdateFromRagdoll(kFrame, parts);
  BearRig::GetUpStart start = rig.BeginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));

  // the roll takes extra time
  CHECK(rig.GetGetUpDuration() > standing_duration + 0.5f);
  CHECK_THAT(start.direction, IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-3f));
  CHECK_THAT(start.root_position.y, WithinAbs(0.0f, 1e-3f));
  bool done = false;
  for (int i = 0; i < 600 && !done; ++i) {
    done = rig.UpdateGetUp(kFrame, ground, start.root_position);
  }
  CHECK(done);
  // Standing upright at the end: the head is above the pelvis
  CHECK(rig.GetBonePosition(rig.GetBone("head")).y >
        rig.GetBonePosition(rig.GetBone("pelvis")).y + 0.3f);
}

TEST_CASE(
    "After standing still for a while the bear plays its idle animations, "
    "moving interrupts them",
    "[assets][bear][animation]") {
  BearOnGround scene;
  scene.Run(5.0f);
  CHECK_FALSE(scene.bear->GetRig().IsIdling());
  scene.Run(2.0f);
  REQUIRE(scene.bear->GetRig().IsIdling());
  CHECK(scene.bear->GetRig().GetIdle() == BearRig::kIdleStretch);

  SECTION("It sits down: the pelvis is low and the chest is above it") {
    scene.Run(1.5f);
    const BearRig& rig = scene.bear->GetRig();
    CHECK(rig.GetBonePosition(rig.GetBone("pelvis")).y < 0.6f);
    CHECK(rig.GetBonePosition(rig.GetBone("chest")).y >
          rig.GetBonePosition(rig.GetBone("pelvis")).y + 0.4f);
  }
  SECTION("The stretch ends standing, the yawn comes next") {
    scene.Run(scene.bear->GetRig().GetIdleDuration(BearRig::kIdleStretch));
    CHECK_FALSE(scene.bear->GetRig().IsIdling());
    scene.Run(6.5f);
    REQUIRE(scene.bear->GetRig().IsIdling());
    CHECK(scene.bear->GetRig().GetIdle() == BearRig::kIdleYawn);
  }
  SECTION("Moving ends it at once and the bear runs off") {
    scene.Run(2.0f);
    vec3 before = scene.bear->GetPosition();
    scene.Run(kFrame, Forward());
    CHECK_FALSE(scene.bear->GetRig().IsIdling());
    scene.Run(1.0f, Forward());
    CHECK(scene.bear->GetPosition().z - before.z > 5.0f);
    for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
      CHECK(scene.PawError(static_cast<BearRig::Leg>(leg)) < 0.15f);
    }
  }
}

TEST_CASE("The eyes blink and look around", "[assets][bear][animation]") {
  BearOnGround scene;
  float max_lids = 0.0f;
  float min_gaze = 1.0f, max_gaze = -1.0f;
  for (int i = 0; i < 5 * 60; ++i) {
    scene.Run(kFrame);
    const bibo::scene::FaceControls& face = scene.bear->GetRig().GetFace();
    max_lids = std::max(max_lids, face.lids);
    min_gaze = std::min(min_gaze, face.gaze_yaw);
    max_gaze = std::max(max_gaze, face.gaze_yaw);
  }
  CHECK(max_lids > 0.9f);              // blinked
  CHECK(max_gaze - min_gaze > 0.05f);  // saccades
}

namespace {
// Height of the center of the capsule above the ground when the bear stands
// (BearCharacter::ControllerDesc)
const float kStandingHeight = 1.35f;

BearInput JumpInput(bool held, BearInput input = {}) {
  input.jump = held;
  return input;
}

/**
 * Jumps on flat ground with the button held for the given time
 * @return Highest point above the standing height
 */
float JumpHeight(BearOnGround& scene, float hold_seconds) {
  float ground = scene.bear->GetPosition().y;
  float highest = 0.0f;
  for (int frame = 0; frame < 120; ++frame) {
    scene.Run(kFrame, JumpInput(frame * kFrame < hold_seconds));
    highest = std::max(highest, scene.bear->GetPosition().y - ground);
  }
  return highest;
}
}  // namespace

TEST_CASE("Jumping: the longer the button is held, the higher the bear jumps",
          "[assets][bear][jump]") {
  BearOnGround scene;
  scene.Run(1.0f);
  REQUIRE_FALSE(scene.bear->IsAirborne());
  REQUIRE_THAT(scene.bear->GetPosition().y, WithinAbs(kStandingHeight, 0.05f));

  float held = JumpHeight(scene, 1.0f);
  CHECK_THAT(held, WithinAbs(BearCharacter::kJumpHeight, 0.3f));
  // Lands again
  CHECK_FALSE(scene.bear->IsAirborne());
  CHECK_THAT(scene.bear->GetPosition().y, WithinAbs(kStandingHeight, 0.05f));

  float tapped = JumpHeight(scene, kFrame);
  CHECK(tapped > 1.0f);  // still clears a rock
  CHECK(tapped < 0.6f * held);
  float half = JumpHeight(scene, 0.5f * BearCharacter::kJumpRiseTime);
  CHECK(half > tapped);
  CHECK(half < held);
}

TEST_CASE("Jumping: holding the button does not jump again after landing",
          "[assets][bear][jump]") {
  BearOnGround scene;
  scene.Run(1.0f);
  int takeoffs = 0;
  bool was_airborne = false;
  for (int frame = 0; frame < 3 * 60; ++frame) {
    scene.Run(kFrame, JumpInput(true));
    if (scene.bear->IsAirborne() && !was_airborne) {
      ++takeoffs;
    }
    was_airborne = scene.bear->IsAirborne();
  }
  CHECK(takeoffs == 1);
  CHECK_FALSE(scene.bear->IsAirborne());
}

TEST_CASE(
    "Jumping: pressed shortly before landing, the bear jumps again when it "
    "lands",
    "[assets][bear][jump]") {
  BearOnGround scene;
  scene.Run(1.0f);
  float ground = scene.bear->GetPosition().y;
  scene.Run(kFrame, JumpInput(true));
  // Falls until it is almost on the ground, then the button is pressed again
  bool falling = false;
  for (int frame = 0;
       frame < 120 && !(falling && scene.bear->GetPosition().y - ground < 0.6f);
       ++frame) {
    scene.Run(kFrame);
    falling = scene.bear->GetVelocity().y < 0.0f;
  }
  REQUIRE(scene.bear->IsAirborne());
  float highest = 0.0f;
  bool landed = false;
  for (int frame = 0; frame < 60; ++frame) {
    scene.Run(kFrame, JumpInput(true));
    landed = landed || !scene.bear->IsAirborne();
    if (landed) {
      highest = std::max(highest, scene.bear->GetPosition().y - ground);
    }
  }
  CHECK(highest > 1.0f);
}

TEST_CASE("Jumping: shortly after running over an edge the bear can still jump",
          "[assets][bear][jump]") {
  PhysicsWorld world;
  world.CreateStaticBox(
      vec3(0.0f, -1.0f, 0.0f),
      vec3(5.0f, 1.0f, 5.0f));  // platform with its top at y = 0, ends at z = 5
  world.CreateStaticBox(vec3(0.0f, -11.0f, 0.0f),
                        vec3(200.0f, 1.0f, 200.0f));  // 10 below
  world.OptimizeBroadPhase();
  BearCharacter bear(world, BearModel(), vec3(0.0f, 1.4f, 0.0f),
                     vec3(0.0f, 0.0f, 1.0f), nullptr);
  auto run = [&](BearInput input) {
    world.Update(kFrame);
    bear.Update(kFrame, input);
  };
  BearInput forward;
  forward.forward = 1.0f;
  for (int frame = 0; frame < 60; ++frame) {
    run(BearInput());
  }
  for (int frame = 0; frame < 120 && !bear.IsAirborne(); ++frame) {
    run(forward);
  }
  REQUIRE(bear.IsAirborne());
  CHECK(bear.GetPosition().z > 4.0f);

  SECTION("Right after leaving the edge") {
    run(forward);
    run(JumpInput(true, forward));
    run(JumpInput(true, forward));
    CHECK(bear.GetVelocity().y > 5.0f);
  }
  SECTION("Too late") {
    for (int frame = 0; frame < 15; ++frame) {
      run(forward);
    }
    run(JumpInput(true, forward));
    run(JumpInput(true, forward));
    CHECK(bear.GetVelocity().y < 0.0f);
  }
  SECTION("Falling off an edge starts slowly (no sudden drop)") {
    float speed = bear.GetVelocity().y;
    // gravity of a frame or two (earlier versions fell at 59 m/s at once)
    CHECK(speed > -3.0f);
    run(forward);
    CHECK(bear.GetVelocity().y < speed);
  }
}

TEST_CASE(
    "Jumping: the legs tuck in, reach for the ground before landing and the "
    "paws land where they reached",
    "[assets][bear][jump][animation]") {
  BearOnGround scene;
  scene.Run(1.0f);
  scene.Run(1.0f, Forward());  // at full speed
  const BearRig& rig = scene.bear->GetRig();
  auto front_paw_height = [&]() {
    float root = scene.bear->GetPosition().y - kStandingHeight;
    return 0.5f * (rig.GetFootTarget(BearRig::kFrontLeft).y +
                   rig.GetFootTarget(BearRig::kFrontRight).y) -
           root;
  };

  float at_top = -1.0f, before_landing = -1.0f;
  std::array<vec3, BearRig::kLegCount> before{};
  float largest_step = 0.0f;
  bool landed = false;
  int frames_after_landing = 0;
  for (int frame = 0; frame < 120 && frames_after_landing < 15; ++frame) {
    for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
      before[leg] = rig.GetFootTarget(static_cast<BearRig::Leg>(leg));
    }
    float vertical = scene.bear->GetVelocity().y;
    scene.Run(kFrame, JumpInput(frame < 30, Forward()));
    if (vertical > 0.0f && scene.bear->GetVelocity().y <= 0.0f) {
      at_top = front_paw_height();
    }
    landed = landed || (frame > 5 && !scene.bear->IsAirborne());
    if (!landed) {
      before_landing = front_paw_height();
      continue;
    }
    // Landing: the paws are put on the ground where they are and go on into the
    // steps, they do not jump
    ++frames_after_landing;
    for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
      vec3 now = rig.GetFootTarget(static_cast<BearRig::Leg>(leg));
      // Sideways and up or down (forward the bear moves 1/6 in a frame)
      largest_step = std::max(
          largest_step,
          glm::length(glm::vec2(now.x - before[leg].x, now.y - before[leg].y)));
    }
  }
  REQUIRE(landed);
  CHECK(at_top > 0.2f);          // tucked in at the highest point
  CHECK(before_landing < 0.1f);  // stretched towards the ground
  CHECK(largest_step < 0.12f);

  // Runs on with all paws on the ground
  scene.Run(0.5f, Forward());
  for (int leg = 0; leg < BearRig::kLegCount; ++leg) {
    BearRig::Leg l = static_cast<BearRig::Leg>(leg);
    if (rig.IsFootPlanted(l)) {
      CHECK(scene.PawError(l) < 0.12f);
    }
  }
}

TEST_CASE("Steering: the bear turns towards where it should go and runs there",
          "[assets][bear]") {
  BearOnGround scene;  // looks along +z
  scene.Run(0.5f);
  BearInput right;
  // the model looks along +z, its right is -x
  right.move = vec3(-1.0f, 0.0f, 0.0f);

  scene.Run(0.2f, right);
  vec3 turning = scene.bear->GetDirection();
  CHECK(turning.x < -0.2f);  // turns right away
  CHECK(turning.z > 0.0f);   // but not at once

  scene.Run(1.0f, right);
  CHECK(scene.bear->GetDirection().x < -0.999f);
  CHECK(scene.bear->GetVelocity().x < -8.0f);

  SECTION("Turning around slows the bear down, it does not run sideways") {
    BearInput back;
    back.move = vec3(1.0f, 0.0f, 0.0f);
    scene.Run(0.15f, back);
    CHECK(glm::length(scene.bear->GetVelocity()) < 8.0f);
    scene.Run(1.5f, back);
    CHECK(scene.bear->GetDirection().x > 0.999f);
    CHECK(scene.bear->GetVelocity().x > 8.0f);
  }
  SECTION("Half way on the stick: slower") {
    BearInput slow;
    slow.move = vec3(-0.5f, 0.0f, 0.0f);
    scene.Run(1.0f, slow);
    CHECK_THAT(scene.bear->GetVelocity().x,
               WithinAbs(-0.5f * BearCharacter::kForwardSpeed, 0.5f));
  }
}

TEST_CASE(
    "A caught fish stays in the mouth in the air, on the ground the bear sits "
    "down and eats it",
    "[assets][bear][animation][animals]") {
  BearOnGround scene;
  scene.Run(1.0f);
  BearInput jump = Forward();
  jump.jump = true;
  scene.Run(0.1f, jump);
  REQUIRE(scene.bear->IsAirborne());
  REQUIRE(scene.bear->CanCatchFish());
  REQUIRE(scene.bear->CatchFish());
  CHECK_FALSE(scene.bear->CanCatchFish());  // only one at a time
  CHECK_FALSE(scene.bear->CatchFish());

  // Carried in the mouth while it flies on
  scene.Run(0.1f, jump);
  REQUIRE(scene.bear->GetFish() == BearCharacter::Fish::kCarried);
  BearCharacter::HeldFish held;
  REQUIRE(scene.bear->GetHeldFish(held));
  CHECK(glm::distance(held.center, scene.bear->GetMouthPosition()) < 0.2f);
  CHECK(held.bites == 0);

  // Lands: eats it, the player cannot run off before the last bite
  for (int i = 0;
       i < 120 && scene.bear->GetFish() == BearCharacter::Fish::kCarried; ++i) {
    scene.Run(kFrame, Forward());
  }
  REQUIRE(scene.bear->GetFish() == BearCharacter::Fish::kEating);
  CHECK(scene.bear->GetRig().GetIdle() == BearRig::kIdleEat);
  vec3 start = scene.bear->GetPosition();
  scene.Run(BearRig::kEatBites[0] + 0.05f, Forward());
  // only slows down
  CHECK(glm::distance(scene.bear->GetPosition(), start) < 1.0f);
  REQUIRE(scene.bear->GetHeldFish(held));
  CHECK(held.bites == 1);
  CHECK(glm::distance(held.center, scene.bear->GetMouthPosition()) < 0.6f);
  {
    // It sits: the pelvis is low, and holds the fish with its front paws
    const BearRig& rig = scene.bear->GetRig();
    CHECK(rig.GetBonePosition(rig.GetBone("pelvis")).y < 0.6f);
    vec3 paws = 0.5f * (rig.GetBonePosition(rig.GetBone("hand.L")) +
                        rig.GetBonePosition(rig.GetBone("hand.R")));
    CHECK(glm::distance(paws, held.center) < 0.8f);
  }
  CHECK(scene.bear->TakeEatenFish() == 0);  // not eaten up yet
  scene.Run(BearRig::kEatBites[2] - BearRig::kEatBites[0], Forward());
  CHECK_FALSE(scene.bear->GetHeldFish(held));  // all three bites taken
  CHECK(scene.bear->TakeEatenFish() == 1);
  CHECK(scene.bear->TakeEatenFish() == 0);  // counted once

  SECTION("After the last bite the player can run on") {
    vec3 before = scene.bear->GetPosition();
    scene.Run(1.0f, Forward());
    CHECK(scene.bear->GetFish() == BearCharacter::Fish::kNone);
    CHECK(glm::distance(scene.bear->GetPosition(), before) > 4.0f);
    CHECK(scene.bear->CanCatchFish());
  }
  SECTION("Without input it licks its lips and gets up") {
    scene.Run(scene.bear->GetRig().GetIdleDuration(BearRig::kIdleEat) -
              BearRig::kEatBites[2] + 0.1f);
    CHECK_FALSE(scene.bear->GetRig().IsIdling());
    CHECK(scene.bear->GetFish() == BearCharacter::Fish::kNone);
  }
}

TEST_CASE(
    "Mishka lies on its belly, watches what it is shown and talks with its "
    "mouth",
    "[assets][bear][animals]") {
  const vec3 up(0.0f, 1.0f, 0.0f);
  // Flat ground at y = 0
  bibo::scene::GroundProbe probe = [&](vec3 from, vec3 to, vec3& point,
                                       vec3& normal) {
    if (from.y < 0.0f || to.y > 0.0f) {
      return false;
    }
    point = vec3(to.x, 0.0f, to.z);
    normal = up;
    return true;
  };
  bibo::scene::RestingBear mishka(BearModel(), vec3(0.0f),
                                  vec3(0.0f, 0.0f, 1.0f), probe);
  BearRig standing(BearModel().skeleton);
  standing.Reset(bibo::scene::LocomotionInput(), probe);

  const BearRig& rig = mishka.GetRig();
  CHECK(rig.GetIdle() == BearRig::kIdleLie);
  auto check_lying = [&]() {
    CHECK(rig.GetCenter().y < standing.GetCenter().y - 0.25f);
    for (const char* paw : {"hand.L", "hand.R", "foot.L", "foot.R"}) {
      CAPTURE(paw);
      CHECK(rig.GetBonePosition(rig.GetBone(paw)).y < 0.3f);  // on the ground
    }
    float lowest = std::numeric_limits<float>::max();
    for (const vec3& position : mishka.GetPositions()) {
      lowest = std::min(lowest, position.y);
    }
    CHECK(lowest > -0.12f);  // not into the ground
    CHECK(lowest < 0.15f);
  };
  check_lying();
  // It does not get up
  for (int i = 0; i < 40 * 60; ++i) {
    mishka.Update(kFrame, nullptr);
  }
  REQUIRE(rig.GetIdle() == BearRig::kIdleLie);
  check_lying();

  // The head follows what it watches (to its left: +x)
  auto watch = [&](vec3 target) {
    for (int i = 0; i < 3 * 60; ++i) {
      mishka.Update(kFrame, &target);
    }
    return mishka.GetFacePosition();
  };
  vec3 left = watch(vec3(4.0f, 1.0f, 4.0f));
  vec3 right = watch(vec3(-4.0f, 1.0f, 4.0f));
  CHECK(left.x > right.x + 0.06f);
  check_lying();

  // Bigger than the cub: everything of the rig is scaled around the point on
  // the ground under it
  CHECK(mishka.GetFacePosition() == mishka.Scaled(rig.GetHeadPosition()));
  CHECK(mishka.GetFacePosition().y > 1.2f * rig.GetHeadPosition().y);
  CHECK(mishka.GetBoundsCenter() == mishka.Scaled(rig.GetCenter()));

  mishka.Speak();
  mishka.Update(0.1f, nullptr);
  CHECK(mishka.GetMouthOpen() > 0.2f);
  mishka.Update(0.15f, nullptr);
  CHECK(mishka.GetMouthOpen() == 0.0f);
}

TEST_CASE(
    "Mishka has the coat of a moon bear: black, a cream crescent on the chest, "
    "a tan muzzle",
    "[assets][bear][animals]") {
  const bibo::resources::SkinnedModel& cub = BearModel();
  bibo::resources::SkinnedModel model = cub;
  bibo::scene::RestingBear::PaintCoat(model);
  const BearRig rig(cub.skeleton);
  vec3 eye = vec3(
      glm::inverse(cub.skeleton.bones[rig.GetBone("eye.L")].inverse_bind)[3]);
  vec3 neck = vec3(
      glm::inverse(cub.skeleton.bones[rig.GetBone("neck")].inverse_bind)[3]);
  auto brightness = [](glm::vec4 color) {
    return (color.r + color.g + color.b) / 3.0f;
  };
  int fur = 0, darker = 0, crescent = 0, muzzle = 0;
  for (std::size_t i = 0; i < model.colors.size(); ++i) {
    CHECK(model.colors[i].a == cub.colors[i].a);  // the length of the fur
    if (static_cast<int>(model.surfaces[i]) !=
        bibo::resources::SkinnedModel::kFur) {
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
  scene.Run(0.5f);
  BearInput jump = Forward();
  jump.jump = true;
  scene.Run(0.1f, jump);
  REQUIRE(scene.bear->CatchFish());
  CharacterController::Hit hit;
  hit.position = scene.bear->GetPosition() + vec3(0.0f, 0.0f, 1.0f);
  hit.normal = vec3(0.0f, 0.0f, -1.0f);
  scene.bear->KnockDown(hit);
  scene.Run(kFrame, jump);

  REQUIRE(scene.bear->GetState() == State::kRagdoll);
  CHECK(scene.bear->GetFish() == BearCharacter::Fish::kNone);
  BearCharacter::HeldFish held;
  CHECK_FALSE(scene.bear->GetHeldFish(held));
  CHECK_FALSE(scene.bear->CanCatchFish());  // not while it tumbles
  scene.Run(5.0f);
  CHECK(scene.bear->TakeEatenFish() == 0);  // it did not eat it
}

namespace {
/**
 * The bear on a shore (top at y = 0) in front of deep water from z = 8 to z =
 * 22 (its floor at y = -3), another shore behind it. The water surface is a bit
 * below the shores.
 */
struct BearAtWater {
  static constexpr float kWaterLevel = -0.4f;
  PhysicsWorld world;
  std::unique_ptr<BearCharacter> bear;

  BearAtWater() {
    world.CreateStaticBox(vec3(0.0f, -1.0f, -12.0f),
                          vec3(50.0f, 1.0f, 20.0f));  // z up to 8
    world.CreateStaticBox(vec3(0.0f, -4.0f, 15.0f),
                          vec3(50.0f, 1.0f, 7.0f));  // the floor of the water
    world.CreateStaticBox(vec3(0.0f, -1.0f, 42.0f),
                          vec3(50.0f, 1.0f, 20.0f));  // from z = 22
    world.OptimizeBroadPhase();
    bear = std::make_unique<BearCharacter>(world, BearModel(),
                                           vec3(0.0f, 1.4f, 0.0f),
                                           vec3(0.0f, 0.0f, 1.0f), nullptr);
    bear->SetWaterLevel(kWaterLevel);
  }

  void Run(float seconds, BearInput input = {}) {
    int frames = static_cast<int>(std::lround(seconds / kFrame));
    for (int i = 0; i < frames; ++i) {
      world.Update(kFrame);
      bear->Update(kFrame, input);
    }
  }

  /**
   * Runs forward until the bear swims
   */
  void RunIntoWater() {
    for (int i = 0; i < 4 * 60 && !bear->IsSwimming(); ++i) {
      Run(kFrame, Forward());
    }
    REQUIRE(bear->IsSwimming());
  }
};
}  // namespace

TEST_CASE(
    "In deep water the bear swims: it floats with its head out of the water "
    "and paddles",
    "[assets][bear][swimming]") {
  BearAtWater scene;
  scene.Run(0.5f);
  CHECK_FALSE(scene.bear->IsSwimming());  // on the shore
  CHECK(scene.bear->GetWaterDepth() < 0.0f);
  scene.RunIntoWater();
  CHECK(scene.bear->GetPosition().z > 8.0f);
  CHECK(scene.bear->TakeSplashes() == 1);
  CHECK(scene.bear->TakeSplashes() == 0);
  CHECK_FALSE(scene.bear->IsAirborne());

  // Slower than on land, it floats at its depth (before it reaches the other
  // shore)
  scene.Run(1.5f, Forward());
  REQUIRE(scene.bear->IsSwimming());
  glm::vec3 velocity = scene.bear->GetVelocity();
  CAPTURE(velocity, scene.bear->GetWaterDepth());
  CHECK(glm::length(glm::vec2(velocity.x, velocity.z)) <=
        BearCharacter::kSwimSpeed + 0.1f);
  CHECK(glm::length(glm::vec2(velocity.x, velocity.z)) >
        0.5f * BearCharacter::kSwimSpeed);
  CHECK_THAT(scene.bear->GetWaterDepth(),
             WithinAbs(BearCharacter::kFloatDepth, 0.15f));
  const BearRig& rig = scene.bear->GetRig();
  CHECK(rig.GetBonePosition(rig.GetBone("head")).y >
        BearAtWater::kWaterLevel + 0.2f);
  CHECK(rig.GetBonePosition(rig.GetBone("pelvis")).y <
        BearAtWater::kWaterLevel + 0.4f);

  SECTION("The paws paddle in circles, they do not stand on anything") {
    vec3 before =
        rig.GetFootTarget(BearRig::kFrontLeft) - scene.bear->GetPosition();
    scene.Run(0.3f);
    vec3 after =
        rig.GetFootTarget(BearRig::kFrontLeft) - scene.bear->GetPosition();
    CHECK(glm::distance(before, after) > 0.05f);
    CHECK_FALSE(rig.IsIdling());
  }
  SECTION("Without input it stays floating where it is (no idle animations)") {
    scene.Run(10.0f);
    CHECK(scene.bear->IsSwimming());
    CHECK_FALSE(scene.bear->GetRig().IsIdling());
    CHECK(glm::length(scene.bear->GetVelocity()) < 0.5f);
  }
  SECTION("A jump at the surface takes it out of the water onto the shore") {
    BearInput swim_and_jump = Forward();
    for (int i = 0; i < 12 * 60 && scene.bear->GetPosition().z < 24.0f; ++i) {
      swim_and_jump.jump = (i / 15) % 2 == 0;  // presses again and again
      scene.Run(kFrame, swim_and_jump);
    }
    scene.Run(1.0f, Forward());
    CAPTURE(scene.bear->GetPosition());
    CHECK(scene.bear->GetPosition().z > 24.0f);
    CHECK_FALSE(scene.bear->IsSwimming());
    CHECK(scene.bear->GetWaterDepth() < 0.0f);
  }
}

TEST_CASE("Without a water level the bear never swims",
          "[assets][bear][swimming]") {
  BearAtWater scene;
  scene.bear->SetWaterLevel(std::numeric_limits<float>::lowest());
  scene.Run(3.0f, Forward());
  CHECK_FALSE(scene.bear->IsSwimming());
  CHECK(scene.bear->GetPosition().y < -1.0f);  // at the bottom of the water
}

TEST_CASE("The bear can be placed somewhere else, standing", "[assets][bear]") {
  BearOnGround scene;
  scene.Run(1.0f, Forward());
  scene.bear->PlaceAt(vec3(20.0f, 1.4f, -5.0f), vec3(-1.0f, 0.0f, 0.0f));
  CHECK(scene.bear->GetState() == State::kControl);
  CHECK(glm::distance(scene.bear->GetPosition(), vec3(20.0f, 1.4f, -5.0f)) <
        1e-3f);
  CHECK(glm::distance(scene.bear->GetDirection(), vec3(-1.0f, 0.0f, 0.0f)) <
        1e-4f);
  CHECK(glm::length(scene.bear->GetVelocity()) < 1e-4f);
  scene.Run(1.0f);
  CHECK(glm::distance(scene.bear->GetPosition(), vec3(20.0f, 1.4f, -5.0f)) <
        0.3f);

  SECTION("reset goes back to the start, which can be changed") {
    scene.bear->SetStart(vec3(-3.0f, 1.4f, 2.0f), vec3(0.0f, 0.0f, -1.0f));
    scene.bear->Reset();
    CHECK(glm::distance(scene.bear->GetPosition(), vec3(-3.0f, 1.4f, 2.0f)) <
          1e-3f);
    CHECK(glm::distance(scene.bear->GetDirection(), vec3(0.0f, 0.0f, -1.0f)) <
          1e-4f);
  }
}

TEST_CASE(
    "Looking at a point: the head and the eyes turn to it, the bear does not "
    "start an idle animation meanwhile",
    "[assets][bear][animation]") {
  BearOnGround scene;
  scene.Run(1.0f);
  const vec3 head = scene.bear->GetHeadPosition();
  // The bear looks along +z, its left is +x. The eyes are there first, the head
  // follows.
  FaceControls face;
  auto look_at = [&](vec3 offset) {
    scene.bear->LookAt(head + offset);
    scene.Run(0.15f);
    face = scene.bear->GetRig().GetFace();
    scene.Run(1.35f);
    return scene.bear->GetMouthPosition() - scene.bear->GetHeadPosition();
  };
  vec3 left = look_at(vec3(3.0f, 0.0f, 4.0f));
  CHECK(face.gaze_yaw > 0.1f);
  vec3 right = look_at(vec3(-3.0f, 0.0f, 4.0f));
  CHECK(face.gaze_yaw < -0.1f);
  vec3 up = look_at(vec3(0.0f, 3.0f, 3.0f));
  CHECK(face.gaze_pitch > 0.1f);
  vec3 down = look_at(vec3(0.0f, -1.0f, 4.0f));
  CAPTURE(left, right, up, down);
  CHECK(left.x > right.x + 0.15f);
  CHECK(up.y > down.y + 0.1f);

  // Paying attention: no stretching or yawning
  scene.Run(10.0f);
  CHECK_FALSE(scene.bear->GetRig().IsIdling());
  SECTION("Afterwards it looks around and plays its idle animations again") {
    scene.bear->StopLooking();
    scene.Run(8.0f);
    CHECK(scene.bear->GetRig().IsIdling());
  }
}

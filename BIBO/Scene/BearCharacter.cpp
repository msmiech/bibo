#include "BearCharacter.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "../Animation/Skeleton.hpp"
#include "CreatureMeshes.hpp"

namespace bibo::scene {

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
// Movement (per second): the speeds of the earlier versions, reached smoothly
// (forward: BearCharacter::kForwardSpeed)
const float kBackwardSpeed = 7.5f;
const float kAcceleration = 40.0f, kDeceleration = 50.0f;
const float kTurnRate = glm::radians(90.0f);  // forward/turn input
// Steering towards a direction (move input): quick on the spot, wider curves at
// full speed. The remaining angle is turned within about kSteerResponse
// seconds, so the bear eases out of a turn
const float kSteerRateStanding = glm::radians(420.0f),
            kSteerRateRunning = glm::radians(220.0f),
            kSteerRateAir = glm::radians(150.0f);
const float kSteerResponse = 0.12f;
// Jump: a ballistic arc. Rising with kJumpGravity reaches kJumpHeight after
// kJumpRiseTime, releasing the button early cuts the rise short (stronger
// gravity), falling is faster than rising (the fall of the earlier versions)
const float kJumpGravity =
    2.0f * BearCharacter::kJumpHeight /
    (BearCharacter::kJumpRiseTime * BearCharacter::kJumpRiseTime);
const float kJumpSpeed = kJumpGravity * BearCharacter::kJumpRiseTime;
const float kJumpCutGravity = 2.5f * kJumpGravity;
const float kFallGravity = 0.981f * 60.0f, kMaxFallSpeed = 35.0f;
// The bear can still jump shortly after it ran over an edge, and a jump pressed
// shortly before landing happens on landing
const float kCoyoteTime = 0.12f, kJumpBufferTime = 0.15f;
// In the air the bear keeps its momentum, the player can only change it a
// little
const float kAirAcceleration = 14.0f;
// Swimming: slower to speed up, it glides on in the water; it turns slower than
// on the ground
const float kSwimAcceleration = 9.0f, kSwimDeceleration = 5.0f;
const float kSteerRateSwim = glm::radians(150.0f);
// The water pulls the bear to its floating depth (1/s^2) and slows it down
// (1/s), falling in it loses most of its speed
const float kBuoyancy = 24.0f, kWaterDrag = 7.0f, kEntryDamping = 0.3f;
// A jump at the surface (out onto the shore), it may start this far from the
// floating depth
const float kSwimJumpSpeed = 8.0f, kSwimJumpTolerance = 0.3f;
// Ragdoll: it lies still when all parts are slower than this for a moment
const float kRestSpeed = 0.35f, kRestAngularSpeed = 1.2f, kRestTime = 0.3f;
const float kMinRagdollTime = 0.8f, kMaxRagdollTime = 4.0f;
// After getting up the bear cannot be knocked down again right away
const float kProtectedTime = 1.0f;
// Standing this long without input starts an idle animation
const float kIdleDelay = 6.0f;
// Wind in the fur (fur lengths / s^2)
const glm::vec3 kWind(9.0f, 0.0f, 6.0f);
}  // namespace

physics::CharacterControllerDesc BearCharacter::ControllerDesc(
    glm::vec3 position) {
  // A controller is a kinematic capsule, moved by the game logic
  physics::CharacterControllerDesc desc;
  desc.position = position;  // initial position
  // controller skin within which contacts generated
  desc.contact_offset = 0.05f;
  desc.step_offset = 1.0f;  // max obstacle height the character can climb
  desc.slope_limit = glm::radians(45.0f);  // max slope the character can walk
  desc.radius = 1.0f;                      // radius of the capsule
  desc.height = 0.6f;                      // height of the controller
  return desc;
}

namespace {
// From the center of the capsule down to the ground it stands on
float RootOffset() {
  physics::CharacterControllerDesc desc =
      BearCharacter::ControllerDesc(glm::vec3(0.0f));
  return 0.5f * desc.height + desc.radius + desc.contact_offset;
}
}  // namespace

BearCharacter::BearCharacter(physics::PhysicsWorld& world,
                             const resources::SkinnedModel& model,
                             glm::vec3 start_position,
                             glm::vec3 start_direction,
                             physics::CharacterController::HitCallback on_hit)
    : world_(world),
      model_(model),
      start_position_(start_position),
      start_direction_(glm::normalize(start_direction)),
      on_hit_(std::move(on_hit)),
      rig_(model.skeleton),
      direction_(this->start_direction_) {
  controller_ = std::make_unique<physics::CharacterController>(
      world, ControllerDesc(start_position),
      [this](const physics::CharacterController::Hit& hit) {
        if (this->on_hit_) {
          this->on_hit_(hit);
        }
      });
  ragdoll_ = std::make_unique<physics::Ragdoll>(world, rig_.RagdollParts());
  tongue_root_ = rig_.GetBone("tongue.1");
  tongue_tip_ = rig_.GetBone("tongue.3");
  left_paw_ = rig_.GetBone("hand.L");
  right_paw_ = rig_.GetBone("hand.R");
  // The feet and the fur only stand on static bodies (terrain, rocks), not on
  // the ragdoll itself
  probe_ = [this](glm::vec3 from, glm::vec3 to, glm::vec3& point,
                  glm::vec3& normal) {
    physics::RayHit hit;
    if (!this->world_.CastRay(from, to - from, hit, true)) {
      return false;
    }
    point = hit.position;
    normal = hit.normal;
    return true;
  };

  bind_comb_directions_ =
      fur::CombDirections(model.normals, glm::vec3(0.0f, 0.0f, 1.0f), kUp);
  fur_lengths_.resize(model.positions.size());
  for (std::size_t i = 0; i < fur_lengths_.size(); ++i) {
    bool has_fur =
        static_cast<int>(model.surfaces[i]) == resources::SkinnedModel::kFur;
    fur_lengths_[i] = has_fur ? model.colors[i].a * fur::kMaxLength : 0.0f;
  }
  Reset();
}

BearCharacter::~BearCharacter() = default;

LocomotionInput BearCharacter::MakeLocomotionInput(bool jumped) const {
  LocomotionInput input;
  input.root_position = controller_->GetPosition() - kUp * RootOffset();
  input.direction = direction_;
  input.velocity = velocity_;
  input.yaw_rate = yaw_rate_;
  input.grounded = standing_on_ground_;
  input.jumped = jumped;
  input.swimming = swimming_;
  return input;
}

void BearCharacter::Reset() {
  fish_ = Fish::kNone;
  eaten_fish_ = 0;
  splashes_ = 0;
  jump_was_held_ = false;
  PlaceAt(start_position_, start_direction_);
}

void BearCharacter::SetStart(glm::vec3 position, glm::vec3 direction) {
  start_position_ = position;
  glm::vec3 horizontal(direction.x, 0.0f, direction.z);
  start_direction_ = glm::length(horizontal) > 1e-6f
                         ? glm::normalize(horizontal)
                         : start_direction_;
}

float BearCharacter::GroundOffset() { return RootOffset(); }

void BearCharacter::PlaceAt(glm::vec3 position, glm::vec3 new_direction) {
  ragdoll_->Deactivate();
  state_ = State::kControl;
  glm::vec3 horizontal(new_direction.x, 0.0f, new_direction.z);
  direction_ = glm::length(horizontal) > 1e-6f ? glm::normalize(horizontal)
                                               : start_direction_;
  speed_ = yaw_rate_ = 0.0f;
  velocity_ = air_velocity_ = glm::vec3(0.0f);
  vertical_speed_ = air_time_ = jump_buffer_ = falling_time_ = 0.0f;
  jumping_ = false;
  standing_on_ground_ = false;
  swimming_ = false;
  knock_down_pending_ = false;
  state_time_ = rest_time_ = protected_time_ = idle_wait_time_ = 0.0f;
  position_offset_ = glm::vec3(0.0f);
  rig_.SetCarrying(fish_ == Fish::kCarried);
  controller_->SetPosition(position);
  rig_.Reset(MakeLocomotionInput(), probe_);
  UpdateSkin(0.0f);
  guide_hairs_.Reset(positions_);
}

float BearCharacter::CapsuleWaterDepth() const {
  return water_level_ - (controller_->GetPosition().y - RootOffset());
}

float BearCharacter::GetWaterDepth() const {
  return water_level_ - (GetPosition().y - RootOffset());
}

int BearCharacter::TakeEatenFish() {
  int result = eaten_fish_;
  eaten_fish_ = 0;
  return result;
}

int BearCharacter::TakeSplashes() {
  int result = splashes_;
  splashes_ = 0;
  return result;
}

glm::vec3 BearCharacter::RawPosition() const {
  if (state_ == State::kRagdoll) {
    // The center of the body is this much lower than the center of the capsule
    // when the bear stands
    return rig_.GetCenter() + kUp * 0.28f;
  }
  return controller_->GetPosition();
}

glm::vec3 BearCharacter::GetPosition() const {
  return RawPosition() + position_offset_;
}

void BearCharacter::SetState(State new_state) {
  glm::vec3 before = GetPosition();
  state_ = new_state;
  state_time_ = 0.0f;
  rest_time_ = 0.0f;
  position_offset_ = before - RawPosition();
}

void BearCharacter::KnockDown(const physics::CharacterController::Hit& hit) {
  if (state_ == State::kControl && !IsProtected()) {
    knock_down_pending_ = true;
    pending_hit_ = hit;
  }
}

void BearCharacter::Update(float time_delta, const BearInput& input) {
  if (time_delta <= 0.0f) {
    return;
  }
  time_ += time_delta;
  state_time_ += time_delta;
  protected_time_ = std::max(0.0f, protected_time_ - time_delta);
  position_offset_ *= 1.0f - animation::SmoothingFactor(0.15f, time_delta);

  switch (state_) {
    case State::kControl:
      UpdateControl(time_delta, input);
      break;
    case State::kRagdoll:
      UpdateRagdoll(time_delta);
      break;
    case State::kGettingUp:
      UpdateGettingUp(time_delta);
      break;
  }
  UpdateSkin(time_delta);
}

float BearCharacter::Steer(float time_delta, const BearInput& input,
                           bool& has_input) {
  glm::vec3 move(input.move.x, 0.0f, input.move.z);
  float amount = std::min(glm::length(move), 1.0f);
  if (amount > 1e-3f) {
    has_input = true;
    glm::vec3 wanted = glm::normalize(move);
    // Positive: the wanted direction is to the left
    float angle = std::atan2(glm::cross(direction_, wanted).y,
                             glm::dot(direction_, wanted));
    float max_rate =
        swimming_ ? kSteerRateSwim
        : standing_on_ground_
            ? glm::mix(kSteerRateStanding, kSteerRateRunning,
                       std::clamp(std::abs(speed_) / kForwardSpeed, 0.0f, 1.0f))
            : kSteerRateAir;
    float target_rate = std::clamp(angle / kSteerResponse, -max_rate, max_rate);
    yaw_rate_ += (target_rate - yaw_rate_) *
                 animation::SmoothingFactor(0.03f, time_delta);
    if (std::abs(yaw_rate_ * time_delta) > std::abs(angle)) {
      yaw_rate_ =
          angle / time_delta;  // does not turn past the wanted direction
    }
    direction_ =
        glm::normalize(glm::rotateY(direction_, yaw_rate_ * time_delta));
    // Slows down while turning around (instead of running sideways)
    float alignment = glm::dot(direction_, wanted);
    return amount * (swimming_ ? kSwimSpeed : kForwardSpeed) *
           std::clamp(0.35f + 0.65f * alignment, 0.0f, 1.0f);
  }

  yaw_rate_ = std::clamp(input.turn, -1.0f, 1.0f) * kTurnRate;
  direction_ = glm::normalize(glm::rotateY(direction_, yaw_rate_ * time_delta));
  float forward = std::clamp(input.forward, -1.0f, 1.0f);
  has_input = forward != 0.0f || input.turn != 0.0f;
  if (swimming_) {
    return forward * kSwimSpeed * (forward >= 0.0f ? 1.0f : 0.5f);
  }
  return forward >= 0.0f ? forward * kForwardSpeed : forward * kBackwardSpeed;
}

void BearCharacter::UpdateControl(float time_delta,
                                  const BearInput& player_input) {
  // A caught fish is eaten as soon as the bear stands on the ground, the player
  // waits until the last bite
  BearInput input = player_input;
  if (fish_ == Fish::kCarried && standing_on_ground_ && !jumping_ &&
      !swimming_) {
    rig_.StartIdle(BearRig::kIdleEat);
    fish_ = Fish::kEating;
    idle_wait_time_ = 0.0f;
  }
  if (fish_ == Fish::kEating) {
    if (rig_.GetIdle() != BearRig::kIdleEat) {
      // finished (or the player moved on after the last bite)
      fish_ = Fish::kNone;
    } else if (rig_.GetIdleTime() < BearRig::kEatBites[2]) {
      input = BearInput();
      jump_was_held_ =
          player_input.jump;  // a jump needs a new press afterwards
    } else if (!last_bite_taken_) {
      last_bite_taken_ = true;
      ++eaten_fish_;
    }
  }

  // In deep water the bear swims (until it is shallow enough to walk), not
  // while it rises from a jump out of the water
  float depth = CapsuleWaterDepth();
  bool was_swimming = swimming_;
  swimming_ = depth > (swimming_ ? kWadeDepth : kSwimDepth) &&
              !(jumping_ && vertical_speed_ > 0.0f);
  if (swimming_ && !was_swimming) {
    ++splashes_;
    vertical_speed_ *= kEntryDamping;
    jumping_ = false;
    rig_.StopIdle();
  }

  bool has_input = false;
  float target_speed = Steer(time_delta, input, has_input);
  glm::vec3 horizontal;
  if (swimming_) {
    bool speeding_up = std::abs(target_speed) > std::abs(speed_) &&
                       target_speed * speed_ >= 0.0f;
    float change =
        (speeding_up ? kSwimAcceleration : kSwimDeceleration) * time_delta;
    speed_ += std::clamp(target_speed - speed_, -change, change);
    horizontal = air_velocity_ = direction_ * speed_;
  } else if (standing_on_ground_) {
    bool speeding_up = std::abs(target_speed) > std::abs(speed_) &&
                       target_speed * speed_ >= 0.0f;
    float change = (speeding_up ? kAcceleration : kDeceleration) * time_delta;
    speed_ += std::clamp(target_speed - speed_, -change, change);
    horizontal = air_velocity_ = direction_ * speed_;
  } else {
    // Without input the bear flies on, with input it changes its course a
    // little
    if (has_input) {
      glm::vec3 change = direction_ * target_speed - air_velocity_;
      float max_change = kAirAcceleration * time_delta;
      if (glm::length(change) > max_change) {
        change *= max_change / glm::length(change);
      }
      air_velocity_ += change;
    }
    horizontal = air_velocity_;
    speed_ = glm::dot(air_velocity_,
                      direction_);  // runs on with this speed after landing
  }

  // The jump starts when the button is pressed (or was pressed shortly before
  // landing), also shortly after running over an edge
  bool jump_pressed = input.jump && !jump_was_held_;
  jump_was_held_ = input.jump;
  jump_buffer_ = jump_pressed ? kJumpBufferTime
                              : std::max(0.0f, jump_buffer_ - time_delta);
  air_time_ = standing_on_ground_ || swimming_ ? 0.0f : air_time_ + time_delta;
  bool jumped = false;
  if (swimming_) {
    if (jump_buffer_ > 0.0f &&
        std::abs(depth - kFloatDepth) < kSwimJumpTolerance) {
      // Out of the water, e.g. onto the shore
      vertical_speed_ = kSwimJumpSpeed;
      jumping_ = jumped = true;
      jump_buffer_ = 0.0f;
      swimming_ = false;
    } else {
      // Floats at its depth and bobs a little, the water slows it down
      float bob = 0.04f * std::sin(time_ * 2.6f);
      vertical_speed_ += ((depth - kFloatDepth - bob) * kBuoyancy -
                          vertical_speed_ * kWaterDrag) *
                         time_delta;
    }
  } else if (jump_buffer_ > 0.0f && !jumping_ && air_time_ <= kCoyoteTime) {
    vertical_speed_ = kJumpSpeed;
    jumping_ = jumped = true;
    jump_buffer_ = 0.0f;
  } else if (controller_->IsOnWalkableGround() && !jumping_) {
    // the controller keeps the bear on the ground when it walks down a slope
    vertical_speed_ = 0.0f;
  } else {
    // Holding the button until the highest point jumps highest, falling is
    // faster than rising
    float gravity = vertical_speed_ <= 0.0f
                        ? kFallGravity
                        : (input.jump ? kJumpGravity : kJumpCutGravity);
    vertical_speed_ =
        std::max(vertical_speed_ - gravity * time_delta, -kMaxFallSpeed);
  }

  glm::vec3 displacement = (horizontal + kUp * vertical_speed_) * time_delta;
  glm::vec3 before = controller_->GetPosition();
  // may knock the bear down (onHit)
  standing_on_ground_ = controller_->Move(displacement, time_delta);
  velocity_ = (controller_->GetPosition() - before) / time_delta;
  if (vertical_speed_ > 0.0f && velocity_.y < 0.5f * vertical_speed_) {
    vertical_speed_ = std::max(velocity_.y, 0.0f);  // hit something above
  }
  if (standing_on_ground_ && vertical_speed_ <= 0.0f) {
    jumping_ = false;  // landed
  }

  // Idle animations: after standing still for a while, until the player moves
  // the bear again
  has_input = has_input || input.jump;
  if (has_input) {
    idle_wait_time_ = 0.0f;
    rig_.StopIdle();
  } else if (rig_.IsLooking() && rig_.IsIdling() &&
             rig_.GetIdle() < BearRig::kRestingIdleCount) {
    rig_.StopIdle();  // paying attention
    idle_wait_time_ = 0.0f;
  } else if (!rig_.IsIdling() && !rig_.IsLooking() && standing_on_ground_ &&
             !swimming_ && std::abs(speed_) < 0.05f) {
    idle_wait_time_ += time_delta;
    if (idle_wait_time_ > kIdleDelay) {
      StartIdle(static_cast<BearRig::Idle>(next_idle_));
    }
  }

  rig_.SetCarrying(fish_ == Fish::kCarried);
  rig_.UpdateLocomotion(time_delta, MakeLocomotionInput(jumped), probe_);
  if (knock_down_pending_) {
    StartRagdoll();
  }
}

void BearCharacter::StartIdle(BearRig::Idle idle) {
  if (state_ != State::kControl) {
    return;
  }
  rig_.StartIdle(idle);
  idle_wait_time_ = 0.0f;
  next_idle_ = (idle + 1) % BearRig::kRestingIdleCount;
}

bool BearCharacter::CatchFish() {
  if (!CanCatchFish()) {
    return false;
  }
  fish_ = Fish::kCarried;
  last_bite_taken_ = false;
  rig_.StopIdle();  // e.g. it was stretching at the edge of the water
  return true;
}

glm::vec3 BearCharacter::GetMouthPosition() const {
  return rig_.GetBonePosition(tongue_tip_);
}

BearCharacter::HeldFish BearCharacter::CarriedFish() const {
  // Across the mouth, its back up
  glm::vec3 forward =
      rig_.GetBonePosition(tongue_tip_) - rig_.GetBonePosition(tongue_root_);
  forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : direction_;
  glm::vec3 left = glm::cross(kUp, forward);
  left = glm::length(left) > 1e-4f ? glm::normalize(left)
                                   : glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 up = glm::cross(forward, left);
  HeldFish held;
  held.center = GetMouthPosition() + forward * 0.04f;
  held.rotation = glm::quat_cast(glm::mat3(-forward, up, left));
  held.wiggle = 0.45f * std::sin(time_ * 14.0f);  // it flaps
  return held;
}

bool BearCharacter::GetHeldFish(HeldFish& held) const {
  if (fish_ == Fish::kNone) {
    return false;
  }
  held = CarriedFish();
  if (fish_ == Fish::kCarried) {
    return true;
  }
  // In the front paws, upright with its head towards the mouth: the end that is
  // left is at the mouth when the bear bites
  float idle_time = rig_.GetIdleTime();
  int bites = 0;
  for (float bite : BearRig::kEatBites) {
    bites += idle_time >= bite ? 1 : 0;
  }
  if (bites >= 3) {
    return false;  // eaten
  }
  glm::vec3 mouth = GetMouthPosition();
  glm::vec3 paws = 0.5f * (rig_.GetBonePosition(left_paw_) +
                           rig_.GetBonePosition(right_paw_));
  glm::vec3 axis = mouth - paws;
  axis = glm::length(axis) > 1e-4f ? glm::normalize(axis) : kUp;
  glm::vec3 back = direction_ - axis * glm::dot(direction_, axis);
  back = glm::length(back) > 1e-4f ? glm::normalize(back)
                                   : glm::vec3(0.0f, 0.0f, 1.0f);
  const float tops[3] = {
      0.5f, kSalmonHeadJoint,
      kSalmonTailJoint};  // of what is left after 0, 1 and 2 bites
  float next_bite = BearRig::kEatBites[bites];
  float gap =
      0.04f +
      0.14f * glm::smoothstep(
                  0.0f, 0.4f,
                  next_bite - idle_time);  // closer to the mouth when it bites
  HeldFish eating;
  eating.center = mouth - axis * (tops[bites] + gap);
  eating.rotation =
      glm::quat_cast(glm::mat3(glm::cross(back, axis), back, axis));
  eating.bites = bites;
  eating.wiggle = 0.12f * std::sin(time_ * 6.0f);
  float take =
      glm::smoothstep(BearRig::kEatTakeStart, BearRig::kEatTakeEnd, idle_time);
  eating.center = glm::mix(held.center, eating.center, take);
  eating.rotation = glm::slerp(held.rotation, eating.rotation, take);
  held = eating;
  return true;
}

void BearCharacter::StartRagdoll() {
  knock_down_pending_ = false;
  swimming_ = false;
  fish_ = Fish::kNone;  // drops it
  rig_.SetCarrying(false);
  std::vector<glm::mat4> transforms;
  std::vector<glm::vec3> velocities;
  rig_.RagdollTransforms(transforms);
  rig_.RagdollVelocities(velocities);

  // Bounces off the obstacle: the velocity towards it is reflected, the bear is
  // thrown up a bit and tumbles head over heels
  glm::vec3 away(pending_hit_.normal.x, 0.0f, pending_hit_.normal.z);
  away = glm::length(away) > 1e-3f ? glm::normalize(away) : -direction_;
  float towards = std::min(0.0f, glm::dot(velocity_, away));
  glm::vec3 bounce =
      velocity_ - 1.25f * towards * away + away * 1.0f + kUp * 3.0f;
  glm::vec3 spin =
      glm::cross(kUp, away) * 3.0f + kUp * (0.8f * std::sin(time_ * 7.0f));
  glm::vec3 center = rig_.GetCenter();
  for (std::size_t i = 0; i < transforms.size(); ++i) {
    glm::vec3 limb_motion = velocities[i] - velocity_;  // e.g. a swinging leg
    velocities[i] = bounce + 0.5f * limb_motion +
                    glm::cross(spin, glm::vec3(transforms[i][3]) - center);
  }
  ragdoll_->Activate(transforms, velocities, spin);
  guide_hairs_.AddImpulse(pending_hit_.position, away * 6.0f + kUp * 2.0f,
                          1.0f);
  speed_ = vertical_speed_ = 0.0f;
  jumping_ = false;
  SetState(State::kRagdoll);
}

void BearCharacter::UpdateRagdoll(float time_delta) {
  std::vector<glm::mat4> transforms;
  ragdoll_->GetTransforms(transforms);
  rig_.UpdateFromRagdoll(time_delta, transforms);

  bool resting = ragdoll_->GetMaxLinearSpeed() < kRestSpeed &&
                 ragdoll_->GetMaxAngularSpeed() < kRestAngularSpeed;
  rest_time_ = resting ? rest_time_ + time_delta : 0.0f;
  if ((state_time_ > kMinRagdollTime && rest_time_ > kRestTime) ||
      state_time_ > kMaxRagdollTime) {
    StartGettingUp();
  }
}

void BearCharacter::StartGettingUp() {
  // the character controller must not collide with the ragdoll
  ragdoll_->Deactivate();
  BearRig::GetUpStart start = rig_.BeginGetUp(probe_, direction_);
  glm::vec3 before = GetPosition();
  direction_ = start.direction;
  controller_->SetPosition(start.root_position + kUp * (RootOffset() + 0.02f));
  speed_ = yaw_rate_ = 0.0f;
  velocity_ = glm::vec3(0.0f);
  state_ = State::kGettingUp;
  state_time_ = 0.0f;
  position_offset_ = before - RawPosition();
}

void BearCharacter::UpdateGettingUp(float time_delta) {
  // The capsule stays on the ground (it may also be pushed out of an obstacle),
  // the animation follows it
  falling_time_ += time_delta;
  standing_on_ground_ = controller_->Move(
      kUp * (-kFallGravity * std::min(falling_time_, 1.0f) * time_delta),
      time_delta);
  if (standing_on_ground_) {
    falling_time_ = 0.0f;
  }
  if (rig_.UpdateGetUp(time_delta, probe_,
                       controller_->GetPosition() - kUp * RootOffset())) {
    state_ = State::kControl;
    state_time_ = 0.0f;
    protected_time_ = kProtectedTime;
    air_velocity_ = glm::vec3(0.0f);
    vertical_speed_ = air_time_ = jump_buffer_ = 0.0f;
    jumping_ = false;
  }
}

void BearCharacter::UpdateSkin(float time_delta) {
  rig_.SkinMatrices(skin_matrices_);
  model_.Skin(skin_matrices_, positions_, normals_, &bind_comb_directions_,
              &comb_directions_);
  ++version_;
  if (time_delta <= 0.0f) {
    return;
  }

  fur::Environment environment;
  environment.time = time_;
  environment.wind = kWind;
  glm::vec3 center = rig_.GetCenter();
  glm::vec3 ground, ground_normal;
  if (probe_(center + kUp * 0.3f, center - kUp * 3.0f, ground, ground_normal)) {
    environment.has_ground = true;
    environment.ground_point = ground;
    environment.ground_normal = ground_normal;
  }
  for (const fur::SphereCollider& obstacle : obstacles_) {
    if (glm::distance(obstacle.center, center) <
        obstacle.radius + GetBoundsRadius()) {
      environment.spheres.push_back(obstacle);
    }
  }
  guide_hairs_.Update(time_delta, positions_, normals_, fur_lengths_,
                      environment);
}

}  // namespace bibo::scene

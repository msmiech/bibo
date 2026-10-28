#include "Hedgehog.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

#include "../World/WorldGenerator.hpp"
#include "HedgehogModel.hpp"

namespace bibo::scene {

namespace {
const float kPi = glm::pi<float>();
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const float kTurnRate = 1.6f;  // radians per second
const float kStrideLength =
    0.22f * Hedgehog::kScale;  // distance per step cycle
const float kHopDuration = 0.45f;
const float kHopHeight = 0.5f;

float WrapAngle(float angle) { return std::remainder(angle, 2.0f * kPi); }
}  // namespace

Hedgehog::Ground Hedgehog::TrackFloor(const world::WorldGenerator& generator) {
  const world::WorldGenerator* world = &generator;
  return [world](glm::vec2 point, float& height) {
    height = world->Height(point);
    world::TrackPath::Nearest nearest = world->GetPath().FindNearest(point);
    return nearest.found &&
           std::abs(nearest.lateral) < nearest.floor_half_width - 1.0f &&
           height > world->GetSeaLevel() + 0.3f &&
           std::abs(height - nearest.floor_height) < 1.5f;
  };
}

Hedgehog::Hedgehog(glm::vec2 home, glm::vec2 start_direction,
                   std::uint32_t seed, Ground ground)
    : home_(home),
      start_heading_(glm::length(start_direction) > 1e-6f
                         ? glm::normalize(start_direction)
                         : glm::vec2(0.0f, 1.0f)),
      ground_(std::move(ground)),
      seed_(seed) {
  Reset();
}

float Hedgehog::Uniform(float from, float to) {
  return from + (to - from) * static_cast<float>(random_() >> 8) *
                    (1.0f / 16777216.0f);
}

void Hedgehog::Reset() {
  random_.seed(seed_);
  float height = 0.0f;
  ground_(home_, height);
  position_ = glm::vec3(home_.x, height, home_.y);
  yaw_ = std::atan2(start_heading_.x, start_heading_.y);
  target_ = home_;
  walking_ = scurrying_ = false;
  bear_ = glm::vec2(1e9f);
  wait_time_ = Uniform(0.5f, 3.0f);
  gait_phase_ = stride_ = time_ = 0.0f;
  hop_time_ = -1.0f;
}

void Hedgehog::Startle() {
  if (hop_time_ < 0.0f) {
    hop_time_ = 0.0f;
  }
}

void Hedgehog::Watch(glm::vec3 bear) {
  this->bear_ = glm::vec2(bear.x, bear.z);
}

void Hedgehog::ChooseTarget() {
  // A spot near its home where it can walk, not right where it is
  for (int attempt = 0; attempt < 12; ++attempt) {
    float angle = Uniform(0.0f, 2.0f * kPi);
    float distance = kHomeRadius * std::sqrt(Uniform(0.0f, 1.0f));
    glm::vec2 candidate =
        home_ + distance * glm::vec2(std::cos(angle), std::sin(angle));
    glm::vec2 here(position_.x, position_.z);
    float height;
    if (glm::distance(candidate, here) > 1.5f && ground_(candidate, height) &&
        ground_(0.5f * (candidate + here), height)) {
      target_ = candidate;
      walking_ = true;
      return;
    }
  }
  wait_time_ = 1.0f;
}

void Hedgehog::Update(float time_delta) {
  if (time_delta <= 0.0f) {
    return;
  }
  time_ += time_delta;
  if (hop_time_ >= 0.0f) {
    hop_time_ += time_delta;
    if (hop_time_ > kHopDuration) {
      hop_time_ = -1.0f;
    }
  }

  // The bear comes close: away from it (not too far from home), a bit to the
  // side if straight away is not possible
  glm::vec2 from_bear = glm::vec2(position_.x, position_.z) - bear_;
  float bear_distance = glm::length(from_bear);
  if (!scurrying_ && hop_time_ < 0.0f && bear_distance < kShyDistance &&
      bear_distance > 1e-3f) {
    glm::vec2 here(position_.x, position_.z);
    for (float turn : {0.0f, 0.6f, -0.6f, 1.2f, -1.2f}) {
      glm::vec2 away = from_bear / bear_distance;
      away = glm::vec2(away.x * std::cos(turn) - away.y * std::sin(turn),
                       away.x * std::sin(turn) + away.y * std::cos(turn));
      glm::vec2 candidate = here + away * 2.5f;
      float height;
      if (glm::distance(candidate, home_) < 1.5f * kHomeRadius &&
          ground_(candidate, height)) {
        target_ = candidate;
        walking_ = scurrying_ = true;
        break;
      }
    }
  }

  if (walking_) {
    glm::vec2 here(position_.x, position_.z);
    glm::vec2 to_target = target_ - here;
    if (glm::length(to_target) < 0.3f) {
      walking_ = scurrying_ = false;
      wait_time_ = Uniform(1.5f, 4.5f);  // sniffs around
    } else {
      // Turns towards the target, walks slower while it is not looking there
      float difference = WrapAngle(std::atan2(to_target.x, to_target.y) - yaw_);
      yaw_ = WrapAngle(yaw_ + std::clamp(difference, -kTurnRate * time_delta,
                                         kTurnRate * time_delta));
      float speed = kWalkSpeed * (scurrying_ ? kScurryFactor : 1.0f) *
                    std::clamp(std::cos(difference), 0.0f, 1.0f);
      if (scurrying_) {
        // Turns twice as fast
        float rest = WrapAngle(std::atan2(to_target.x, to_target.y) - yaw_);
        yaw_ = WrapAngle(yaw_ + std::clamp(rest, -kTurnRate * time_delta,
                                           kTurnRate * time_delta));
      }
      glm::vec2 next = here + glm::vec2(std::sin(yaw_), std::cos(yaw_)) *
                                  (speed * time_delta);
      float height;
      if (ground_(next, height)) {
        position_ =
            glm::vec3(next.x,
                      position_.y + (height - position_.y) *
                                        std::min(1.0f, time_delta * 10.0f),
                      next.y);
        gait_phase_ += speed * time_delta / kStrideLength;
      } else {
        // e.g. the edge of the water: it looks for another way
        walking_ = scurrying_ = false;
        wait_time_ = Uniform(0.5f, 1.5f);
      }
    }
  } else {
    wait_time_ -= time_delta;
    if (wait_time_ <= 0.0f) {
      ChooseTarget();
    }
  }
  stride_ +=
      ((walking_ ? 1.0f : 0.0f) - stride_) * std::min(1.0f, time_delta * 6.0f);
  gait_phase_ -= std::floor(gait_phase_);
}

float Hedgehog::GetColliderRadius() const {
  return kHedgehogBodyRadius * kScale * 0.95f;
}

glm::vec3 Hedgehog::GetColliderCenter() const {
  // Resting on the ground (the body is flatter than the sphere)
  return GetPose().body_position +
         kUp * std::max(kHedgehogBodyHeight * kScale,
                        GetColliderRadius() + 0.02f * kScale);
}

Hedgehog::Pose Hedgehog::GetPose() const {
  Pose pose;
  float hop = 0.0f, hop_pitch = 0.0f;
  if (hop_time_ >= 0.0f) {
    float t = hop_time_ / kHopDuration;
    hop = 4.0f * kHopHeight * t * (1.0f - t);
    hop_pitch = 0.18f * std::sin(kPi * t);
  }
  float cycle = 2.0f * kPi * gait_phase_;
  // Bobs with every step and waddles from side to side, sniffs (nose up and
  // down) in bursts when it stands
  float bob = stride_ * 0.012f * std::abs(std::sin(2.0f * cycle)) * kScale;
  float roll = stride_ * 0.05f * std::sin(cycle);
  float sniff = (1.0f - stride_) * 0.035f * std::sin(time_ * 11.0f) *
                std::max(0.0f, std::sin(time_ * 1.3f));
  glm::quat turn = glm::angleAxis(yaw_, kUp);
  pose.body_rotation =
      turn * glm::angleAxis(-(sniff + hop_pitch), glm::vec3(1.0f, 0.0f, 0.0f)) *
      glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
  pose.body_position = position_ + kUp * (bob + hop);
  pose.foot_rotation = turn;
  pose.bristle =
      hop_time_ >= 0.0f
          ? std::sin(kPi * std::min(1.0f, hop_time_ / (0.6f * kHopDuration)))
          : 0.0f;
  for (int leg = 0; leg < 4; ++leg) {
    bool front = leg < 2;
    float side = leg % 2 == 0 ? 1.0f : -1.0f;  //+x is the left side
    // Trot: diagonal pairs of feet step together
    float phase = cycle + (leg == 0 || leg == 3 ? 0.0f : kPi);
    float swing = stride_ * 0.06f * std::sin(phase);
    float lift = stride_ * 0.035f * std::max(0.0f, std::cos(phase));
    glm::vec3 local(side * kHedgehogFootSide, lift,
                    (front ? kHedgehogFootFront : kHedgehogFootBack) + swing);
    pose.feet[leg] = position_ + kUp * hop + turn * (local * kScale);
  }
  return pose;
}

}  // namespace bibo::scene

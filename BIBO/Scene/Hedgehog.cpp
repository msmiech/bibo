#include "Hedgehog.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

#include "../World/WorldGenerator.hpp"
#include "HedgehogModel.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace {
const float PI = glm::pi<float>();
const glm::vec3 UP(0.0f, 1.0f, 0.0f);
const float TURN_RATE = 1.6f;                         // radians per second
const float STRIDE_LENGTH = 0.22f * Hedgehog::SCALE;  // distance per step cycle
const float HOP_DURATION = 0.45f;
const float HOP_HEIGHT = 0.5f;

float wrapAngle(float angle) { return std::remainder(angle, 2.0f * PI); }
}  // namespace

Hedgehog::Ground Hedgehog::trackFloor(const world::WorldGenerator& generator) {
  const world::WorldGenerator* world = &generator;
  return [world](glm::vec2 point, float& height) {
    height = world->height(point);
    world::TrackPath::Nearest nearest = world->getPath().nearest(point);
    return nearest.found &&
           std::abs(nearest.lateral) < nearest.floorHalfWidth - 1.0f &&
           height > world->getSeaLevel() + 0.3f &&
           std::abs(height - nearest.floorHeight) < 1.5f;
  };
}

Hedgehog::Hedgehog(glm::vec2 home, glm::vec2 startDirection, std::uint32_t seed,
                   Ground ground)
    : home(home),
      startHeading(glm::length(startDirection) > 1e-6f
                       ? glm::normalize(startDirection)
                       : glm::vec2(0.0f, 1.0f)),
      ground(std::move(ground)),
      seed(seed) {
  reset();
}

float Hedgehog::uniform(float from, float to) {
  return from +
         (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}

void Hedgehog::reset() {
  random.seed(seed);
  float height = 0.0f;
  ground(home, height);
  position = glm::vec3(home.x, height, home.y);
  yaw = std::atan2(startHeading.x, startHeading.y);
  target = home;
  walking = scurrying = false;
  bear = glm::vec2(1e9f);
  waitTime = uniform(0.5f, 3.0f);
  gaitPhase = stride = time = 0.0f;
  hopTime = -1.0f;
}

void Hedgehog::startle() {
  if (hopTime < 0.0f) {
    hopTime = 0.0f;
  }
}

void Hedgehog::watch(glm::vec3 bear) { this->bear = glm::vec2(bear.x, bear.z); }

void Hedgehog::chooseTarget() {
  // A spot near its home where it can walk, not right where it is
  for (int attempt = 0; attempt < 12; ++attempt) {
    float angle = uniform(0.0f, 2.0f * PI);
    float distance = HOME_RADIUS * std::sqrt(uniform(0.0f, 1.0f));
    glm::vec2 candidate =
        home + distance * glm::vec2(std::cos(angle), std::sin(angle));
    glm::vec2 here(position.x, position.z);
    float height;
    if (glm::distance(candidate, here) > 1.5f && ground(candidate, height) &&
        ground(0.5f * (candidate + here), height)) {
      target = candidate;
      walking = true;
      return;
    }
  }
  waitTime = 1.0f;
}

void Hedgehog::update(float timeDelta) {
  if (timeDelta <= 0.0f) {
    return;
  }
  time += timeDelta;
  if (hopTime >= 0.0f) {
    hopTime += timeDelta;
    if (hopTime > HOP_DURATION) {
      hopTime = -1.0f;
    }
  }

  // The bear comes close: away from it (not too far from home), a bit to the
  // side if straight away is not possible
  glm::vec2 fromBear = glm::vec2(position.x, position.z) - bear;
  float bearDistance = glm::length(fromBear);
  if (!scurrying && hopTime < 0.0f && bearDistance < SHY_DISTANCE &&
      bearDistance > 1e-3f) {
    glm::vec2 here(position.x, position.z);
    for (float turn : {0.0f, 0.6f, -0.6f, 1.2f, -1.2f}) {
      glm::vec2 away = fromBear / bearDistance;
      away = glm::vec2(away.x * std::cos(turn) - away.y * std::sin(turn),
                       away.x * std::sin(turn) + away.y * std::cos(turn));
      glm::vec2 candidate = here + away * 2.5f;
      float height;
      if (glm::distance(candidate, home) < 1.5f * HOME_RADIUS &&
          ground(candidate, height)) {
        target = candidate;
        walking = scurrying = true;
        break;
      }
    }
  }

  if (walking) {
    glm::vec2 here(position.x, position.z);
    glm::vec2 toTarget = target - here;
    if (glm::length(toTarget) < 0.3f) {
      walking = scurrying = false;
      waitTime = uniform(1.5f, 4.5f);  // sniffs around
    } else {
      // Turns towards the target, walks slower while it is not looking there
      float difference = wrapAngle(std::atan2(toTarget.x, toTarget.y) - yaw);
      yaw = wrapAngle(yaw + std::clamp(difference, -TURN_RATE * timeDelta,
                                       TURN_RATE * timeDelta));
      float speed = WALK_SPEED * (scurrying ? SCURRY_FACTOR : 1.0f) *
                    std::clamp(std::cos(difference), 0.0f, 1.0f);
      if (scurrying) {
        // Turns twice as fast
        float rest = wrapAngle(std::atan2(toTarget.x, toTarget.y) - yaw);
        yaw = wrapAngle(yaw + std::clamp(rest, -TURN_RATE * timeDelta,
                                         TURN_RATE * timeDelta));
      }
      glm::vec2 next =
          here + glm::vec2(std::sin(yaw), std::cos(yaw)) * (speed * timeDelta);
      float height;
      if (ground(next, height)) {
        position = glm::vec3(next.x,
                             position.y + (height - position.y) *
                                              std::min(1.0f, timeDelta * 10.0f),
                             next.y);
        gaitPhase += speed * timeDelta / STRIDE_LENGTH;
      } else {
        walking = scurrying =
            false;  // e.g. the edge of the water: it looks for another way
        waitTime = uniform(0.5f, 1.5f);
      }
    }
  } else {
    waitTime -= timeDelta;
    if (waitTime <= 0.0f) {
      chooseTarget();
    }
  }
  stride +=
      ((walking ? 1.0f : 0.0f) - stride) * std::min(1.0f, timeDelta * 6.0f);
  gaitPhase -= std::floor(gaitPhase);
}

float Hedgehog::getColliderRadius() const {
  return HEDGEHOG_BODY_RADIUS * SCALE * 0.95f;
}

glm::vec3 Hedgehog::getColliderCenter() const {
  // Resting on the ground (the body is flatter than the sphere)
  return getPose().bodyPosition +
         UP * std::max(HEDGEHOG_BODY_HEIGHT * SCALE,
                       getColliderRadius() + 0.02f * SCALE);
}

Hedgehog::Pose Hedgehog::getPose() const {
  Pose pose;
  float hop = 0.0f, hopPitch = 0.0f;
  if (hopTime >= 0.0f) {
    float t = hopTime / HOP_DURATION;
    hop = 4.0f * HOP_HEIGHT * t * (1.0f - t);
    hopPitch = 0.18f * std::sin(PI * t);
  }
  float cycle = 2.0f * PI * gaitPhase;
  // Bobs with every step and waddles from side to side, sniffs (nose up and
  // down) in bursts when it stands
  float bob = stride * 0.012f * std::abs(std::sin(2.0f * cycle)) * SCALE;
  float roll = stride * 0.05f * std::sin(cycle);
  float sniff = (1.0f - stride) * 0.035f * std::sin(time * 11.0f) *
                std::max(0.0f, std::sin(time * 1.3f));
  glm::quat turn = glm::angleAxis(yaw, UP);
  pose.bodyRotation =
      turn * glm::angleAxis(-(sniff + hopPitch), glm::vec3(1.0f, 0.0f, 0.0f)) *
      glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
  pose.bodyPosition = position + UP * (bob + hop);
  pose.footRotation = turn;
  pose.bristle =
      hopTime >= 0.0f
          ? std::sin(PI * std::min(1.0f, hopTime / (0.6f * HOP_DURATION)))
          : 0.0f;
  for (int leg = 0; leg < 4; ++leg) {
    bool front = leg < 2;
    float side = leg % 2 == 0 ? 1.0f : -1.0f;  //+x is the left side
    // Trot: diagonal pairs of feet step together
    float phase = cycle + (leg == 0 || leg == 3 ? 0.0f : PI);
    float swing = stride * 0.06f * std::sin(phase);
    float lift = stride * 0.035f * std::max(0.0f, std::cos(phase));
    glm::vec3 local(side * HEDGEHOG_FOOT_SIDE, lift,
                    (front ? HEDGEHOG_FOOT_FRONT : HEDGEHOG_FOOT_BACK) + swing);
    pose.feet[leg] = position + UP * hop + turn * (local * SCALE);
  }
  return pose;
}

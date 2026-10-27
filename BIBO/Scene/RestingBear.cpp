#include "RestingBear.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

using namespace bibo;
using namespace scene;

namespace {
const glm::vec3 UP(0.0f, 1.0f, 0.0f);
const float PI = glm::pi<float>();
// At the start it lies down for this long (in steps), so that the game starts
// with it lying
const float SETTLE_TIME = 1.0f, SETTLE_STEP = 1.0f / 30.0f;
// One sound: the mouth opens and closes within this time
const float SOUND_DURATION = 0.2f, MOUTH_OPEN = 0.32f;
// The exclamation mark above its head
const float MARKER_HEIGHT = 1.4f;
// Wind in the fur (fur lengths / s^2), like the player's bear
const glm::vec3 WIND(9.0f, 0.0f, 6.0f);
// The coat of a moon bear (linear colors): black with a brown shimmer, the
// cream crescent on the chest, the tan muzzle
const glm::vec3 COAT_TINT(0.32f, 0.4f, 0.55f);
const glm::vec3 CRESCENT(0.62f, 0.55f, 0.42f), MUZZLE(0.3f, 0.19f, 0.1f);
// The crescent is a V on the front of the chest: its tip this far below the
// neck, its arms rising this steeply to the shoulders, this wide (vertically)
// and this far out to the sides
const float CRESCENT_TIP = 0.42f, CRESCENT_SLOPE = 0.9f,
            CRESCENT_WIDTH = 0.075f, CRESCENT_REACH = 0.42f;

glm::vec3 bindPosition(const animation::Skeleton& skeleton, const char* bone) {
  for (const animation::Bone& candidate : skeleton.bones) {
    if (candidate.name == bone) {
      return glm::vec3(glm::inverse(candidate.inverseBind)[3]);
    }
  }
  return glm::vec3(0.0f);
}
}  // namespace

RestingBear::RestingBear(const resources::SkinnedModel& model,
                         glm::vec3 position, glm::vec3 direction,
                         GroundProbe probe)
    : model(model),
      position(position),
      probe(std::move(probe)),
      rig(model.skeleton) {
  glm::vec3 horizontal(direction.x, 0.0f, direction.z);
  this->direction = glm::length(horizontal) > 1e-6f
                        ? glm::normalize(horizontal)
                        : glm::vec3(0.0f, 0.0f, 1.0f);

  bindCombDirections =
      fur::combDirections(model.normals, glm::vec3(0.0f, 0.0f, 1.0f), UP);
  furLengths.resize(model.positions.size());
  for (std::size_t i = 0; i < furLengths.size(); ++i) {
    bool hasFur =
        static_cast<int>(model.surfaces[i]) == resources::SkinnedModel::FUR;
    furLengths[i] =
        hasFur ? model.colors[i].a * fur::MAX_LENGTH * FUR_LENGTH : 0.0f;
  }

  // The rig finds the ground in its own frame (before it is scaled), so that
  // the paws of the bigger bear are on the ground
  rigProbe = [this](glm::vec3 from, glm::vec3 to, glm::vec3& point,
                    glm::vec3& normal) {
    if (!this->probe || !this->probe(scaled(from), scaled(to), point, normal)) {
      return false;
    }
    point = this->position + (point - this->position) / SCALE;
    return true;
  };
  rig.reset(locomotionInput(), rigProbe);
  rig.startIdle(BearRig::IDLE_LIE);
  for (float settled = 0.0f; settled < SETTLE_TIME; settled += SETTLE_STEP) {
    rig.updateLocomotion(SETTLE_STEP, locomotionInput(), rigProbe);
  }
  colliderCenter = scaled(rig.getCenter());
  updateSkin(0.0f);
  guideHairs.reset(positions);
}

void RestingBear::paintCoat(resources::SkinnedModel& model) {
  const animation::Skeleton& skeleton = model.skeleton;
  const glm::vec3 neck = bindPosition(skeleton, "neck");
  const glm::vec3 eye = bindPosition(skeleton, "eye.L");
  const glm::vec3 shoulder = bindPosition(skeleton, "upperarm.L");
  for (std::size_t i = 0; i < model.colors.size() && i < model.surfaces.size();
       ++i) {
    if (static_cast<int>(model.surfaces[i]) != resources::SkinnedModel::FUR) {
      continue;
    }
    const glm::vec3 point = model.positions[i];
    glm::vec3 color = glm::vec3(model.colors[i]) * COAT_TINT;

    // The muzzle: the front of the head below the eyes (the model looks along
    // +z)
    float muzzle =
        glm::smoothstep(eye.z - 0.12f, eye.z + 0.02f, point.z) *
        (1.0f - glm::smoothstep(eye.y - 0.1f, eye.y + 0.02f, point.y)) *
        (1.0f -
         glm::smoothstep(eye.x - 0.02f, eye.x + 0.06f, std::abs(point.x)));
    color = glm::mix(color, MUZZLE, muzzle);

    // The crescent: a V on the front of the chest, between the neck and the
    // shoulders
    float across = std::abs(point.x);
    float middle = neck.y - CRESCENT_TIP + CRESCENT_SLOPE * across;
    float band = 1.0f - glm::smoothstep(0.6f * CRESCENT_WIDTH, CRESCENT_WIDTH,
                                        std::abs(point.y - middle));
    float front = glm::smoothstep(shoulder.z, shoulder.z + 0.15f, point.z) *
                  glm::smoothstep(0.0f, 0.35f, model.normals[i].z);
    float inside =
        1.0f - glm::smoothstep(CRESCENT_REACH - 0.06f, CRESCENT_REACH, across);
    color = glm::mix(color, CRESCENT, band * front * inside);

    model.colors[i] = glm::vec4(color, model.colors[i].a);
  }
}

LocomotionInput RestingBear::locomotionInput() const {
  LocomotionInput input;
  input.rootPosition = position;
  input.direction = direction;
  return input;
}

void RestingBear::speak() { mouthTime = 0.0f; }

void RestingBear::update(float timeDelta, const glm::vec3* watch) {
  if (timeDelta <= 0.0f) {
    return;
  }
  time += timeDelta;
  if (watch) {
    rig.lookAt(position +
               // into the frame of the rig before it is scaled
               (*watch - position) / SCALE);
  } else {
    rig.stopLooking();
  }
  mouth = 0.0f;
  if (mouthTime >= 0.0f) {
    mouthTime += timeDelta;
    mouth = mouthTime < SOUND_DURATION
                ? MOUTH_OPEN * std::sin(PI * mouthTime / SOUND_DURATION)
                : 0.0f;
    mouthTime = mouthTime < SOUND_DURATION ? mouthTime : -1.0f;
  }
  rig.setMouthOpen(mouth);
  rig.updateLocomotion(timeDelta, locomotionInput(), rigProbe);
  updateSkin(timeDelta);
}

glm::vec3 RestingBear::getMarkerPosition() const {
  return getFacePosition() +
         UP * (MARKER_HEIGHT + 0.07f * std::sin(time * 2.2f));
}

glm::quat RestingBear::getMarkerRotation() const {
  return glm::angleAxis(time * 1.3f, UP);
}

void RestingBear::updateSkin(float timeDelta) {
  rig.skinMatrices(skinMatrices);
  model.skin(skinMatrices, positions, normals, &bindCombDirections,
             &combDirections);
  for (glm::vec3& point : positions) {
    point = scaled(point);
  }
  ++version;
  if (timeDelta <= 0.0f) {
    return;
  }

  fur::Environment environment;
  environment.time = time;
  environment.wind = WIND;
  glm::vec3 center = getBoundsCenter();
  glm::vec3 ground, groundNormal;
  if (probe &&
      probe(center + UP * 0.3f, center - UP * 3.0f, ground, groundNormal)) {
    environment.hasGround = true;
    environment.groundPoint = ground;
    environment.groundNormal = groundNormal;
  }
  guideHairs.update(timeDelta, positions, normals, furLengths, environment);
}

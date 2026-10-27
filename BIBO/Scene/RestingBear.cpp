#include "RestingBear.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace bibo::scene {

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const float kPi = glm::pi<float>();
// At the start it lies down for this long (in steps), so that the game starts
// with it lying
const float kSettleTime = 1.0f, kSettleStep = 1.0f / 30.0f;
// One sound: the mouth opens and closes within this time
const float kSoundDuration = 0.2f, kMouthOpen = 0.32f;
// The exclamation mark above its head
const float kMarkerHeight = 1.4f;
// Wind in the fur (fur lengths / s^2), like the player's bear
const glm::vec3 kWind(9.0f, 0.0f, 6.0f);
// The coat of a moon bear (linear colors): black with a brown shimmer, the
// cream crescent on the chest, the tan muzzle
const glm::vec3 kCoatTint(0.32f, 0.4f, 0.55f);
const glm::vec3 kCrescent(0.62f, 0.55f, 0.42f), kMuzzle(0.3f, 0.19f, 0.1f);
// The crescent is a V on the front of the chest: its tip this far below the
// neck, its arms rising this steeply to the shoulders, this wide (vertically)
// and this far out to the sides
const float kCrescentTip = 0.42f, kCrescentSlope = 0.9f,
            kCrescentWidth = 0.075f, kCrescentReach = 0.42f;

glm::vec3 BindPosition(const animation::Skeleton& skeleton, const char* bone) {
  for (const animation::Bone& candidate : skeleton.bones) {
    if (candidate.name == bone) {
      return glm::vec3(glm::inverse(candidate.inverse_bind)[3]);
    }
  }
  return glm::vec3(0.0f);
}
}  // namespace

RestingBear::RestingBear(const resources::SkinnedModel& model,
                         glm::vec3 position, glm::vec3 direction,
                         GroundProbe probe)
    : model_(model),
      position_(position),
      probe_(std::move(probe)),
      rig_(model.skeleton) {
  glm::vec3 horizontal(direction.x, 0.0f, direction.z);
  this->direction_ = glm::length(horizontal) > 1e-6f
                         ? glm::normalize(horizontal)
                         : glm::vec3(0.0f, 0.0f, 1.0f);

  bind_comb_directions_ =
      fur::CombDirections(model.normals, glm::vec3(0.0f, 0.0f, 1.0f), kUp);
  fur_lengths_.resize(model.positions.size());
  for (std::size_t i = 0; i < fur_lengths_.size(); ++i) {
    bool has_fur =
        static_cast<int>(model.surfaces[i]) == resources::SkinnedModel::kFur;
    fur_lengths_[i] =
        has_fur ? model.colors[i].a * fur::kMaxLength * kFurLength : 0.0f;
  }

  // The rig finds the ground in its own frame (before it is scaled), so that
  // the paws of the bigger bear are on the ground
  rig_probe_ = [this](glm::vec3 from, glm::vec3 to, glm::vec3& point,
                      glm::vec3& normal) {
    if (!this->probe_ ||
        !this->probe_(Scaled(from), Scaled(to), point, normal)) {
      return false;
    }
    point = this->position_ + (point - this->position_) / kScale;
    return true;
  };
  rig_.Reset(MakeLocomotionInput(), rig_probe_);
  rig_.StartIdle(BearRig::kIdleLie);
  for (float settled = 0.0f; settled < kSettleTime; settled += kSettleStep) {
    rig_.UpdateLocomotion(kSettleStep, MakeLocomotionInput(), rig_probe_);
  }
  collider_center_ = Scaled(rig_.GetCenter());
  UpdateSkin(0.0f);
  guide_hairs_.Reset(positions_);
}

void RestingBear::PaintCoat(resources::SkinnedModel& model) {
  const animation::Skeleton& skeleton = model.skeleton;
  const glm::vec3 neck = BindPosition(skeleton, "neck");
  const glm::vec3 eye = BindPosition(skeleton, "eye.L");
  const glm::vec3 shoulder = BindPosition(skeleton, "upperarm.L");
  for (std::size_t i = 0; i < model.colors.size() && i < model.surfaces.size();
       ++i) {
    if (static_cast<int>(model.surfaces[i]) != resources::SkinnedModel::kFur) {
      continue;
    }
    const glm::vec3 point = model.positions[i];
    glm::vec3 color = glm::vec3(model.colors[i]) * kCoatTint;

    // The muzzle: the front of the head below the eyes (the model looks along
    // +z)
    float muzzle =
        glm::smoothstep(eye.z - 0.12f, eye.z + 0.02f, point.z) *
        (1.0f - glm::smoothstep(eye.y - 0.1f, eye.y + 0.02f, point.y)) *
        (1.0f -
         glm::smoothstep(eye.x - 0.02f, eye.x + 0.06f, std::abs(point.x)));
    color = glm::mix(color, kMuzzle, muzzle);

    // The crescent: a V on the front of the chest, between the neck and the
    // shoulders
    float across = std::abs(point.x);
    float middle = neck.y - kCrescentTip + kCrescentSlope * across;
    float band = 1.0f - glm::smoothstep(0.6f * kCrescentWidth, kCrescentWidth,
                                        std::abs(point.y - middle));
    float front = glm::smoothstep(shoulder.z, shoulder.z + 0.15f, point.z) *
                  glm::smoothstep(0.0f, 0.35f, model.normals[i].z);
    float inside =
        1.0f - glm::smoothstep(kCrescentReach - 0.06f, kCrescentReach, across);
    color = glm::mix(color, kCrescent, band * front * inside);

    model.colors[i] = glm::vec4(color, model.colors[i].a);
  }
}

LocomotionInput RestingBear::MakeLocomotionInput() const {
  LocomotionInput input;
  input.root_position = position_;
  input.direction = direction_;
  return input;
}

void RestingBear::Speak() { mouth_time_ = 0.0f; }

void RestingBear::Update(float time_delta, const glm::vec3* watch) {
  if (time_delta <= 0.0f) {
    return;
  }
  time_ += time_delta;
  if (watch) {
    rig_.LookAt(position_ +
                // into the frame of the rig before it is scaled
                (*watch - position_) / kScale);
  } else {
    rig_.StopLooking();
  }
  mouth_ = 0.0f;
  if (mouth_time_ >= 0.0f) {
    mouth_time_ += time_delta;
    mouth_ = mouth_time_ < kSoundDuration
                 ? kMouthOpen * std::sin(kPi * mouth_time_ / kSoundDuration)
                 : 0.0f;
    mouth_time_ = mouth_time_ < kSoundDuration ? mouth_time_ : -1.0f;
  }
  rig_.SetMouthOpen(mouth_);
  rig_.UpdateLocomotion(time_delta, MakeLocomotionInput(), rig_probe_);
  UpdateSkin(time_delta);
}

glm::vec3 RestingBear::GetMarkerPosition() const {
  return GetFacePosition() +
         kUp * (kMarkerHeight + 0.07f * std::sin(time_ * 2.2f));
}

glm::quat RestingBear::GetMarkerRotation() const {
  return glm::angleAxis(time_ * 1.3f, kUp);
}

void RestingBear::UpdateSkin(float time_delta) {
  rig_.SkinMatrices(skin_matrices_);
  model_.Skin(skin_matrices_, positions_, normals_, &bind_comb_directions_,
              &comb_directions_);
  for (glm::vec3& point : positions_) {
    point = Scaled(point);
  }
  ++version_;
  if (time_delta <= 0.0f) {
    return;
  }

  fur::Environment environment;
  environment.time = time_;
  environment.wind = kWind;
  glm::vec3 center = GetBoundsCenter();
  glm::vec3 ground, ground_normal;
  if (probe_ &&
      probe_(center + kUp * 0.3f, center - kUp * 3.0f, ground, ground_normal)) {
    environment.has_ground = true;
    environment.ground_point = ground;
    environment.ground_normal = ground_normal;
  }
  guide_hairs_.Update(time_delta, positions_, normals_, fur_lengths_,
                      environment);
}

}  // namespace bibo::scene

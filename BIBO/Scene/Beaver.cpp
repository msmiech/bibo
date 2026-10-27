#include "Beaver.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "BeaverModel.hpp"

namespace bibo::scene {

namespace {
const float kPi = glm::pi<float>();
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const glm::vec3 kLeft(1.0f, 0.0f, 0.0f);
const glm::vec3 kForward(0.0f, 0.0f, 1.0f);
const float kTurnRate = 2.5f;  // radians per second
// the mouth opens and closes once per sound
const float kSoundDuration = 0.17f;
const float kNibbleDuration = 2.4f;
const float kCheerDuration = 1.0f;
const float kSlapDuration = 0.7f;
const float kWaveDuration = 1.6f;
// The arms hang a bit forward, raised this far the hands are at the mouth
// (radians around the shoulder)
const float kArmRest = 0.45f, kArmAtMouth = 1.95f;
// Height of the marker above the head (world units)
const float kMarkerHeight = 0.6f;

float WrapAngle(float angle) { return std::remainder(angle, 2.0f * kPi); }

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/**
 * 0 at the start and the end of an animation, 1 in between (blends in and out
 * over fade seconds)
 */
float Envelope(float time, float duration, float fade) {
  if (time < 0.0f || time > duration) {
    return 0.0f;
  }
  return Smooth(time / fade) * Smooth((duration - time) / fade);
}

float Approach(float value, float target, float half_life, float time_delta) {
  return value + (target - value) * (1.0f - std::exp2(-time_delta / half_life));
}

glm::mat4 RotationX(float angle) {
  return glm::rotate(glm::mat4(1.0f), angle, kLeft);
}
}  // namespace

Beaver::Beaver(glm::vec3 position, glm::vec3 direction, std::uint32_t seed)
    : home_(position),
      home_direction_(
          glm::length(glm::vec3(direction.x, 0.0f, direction.z)) > 1e-6f
              ? glm::normalize(glm::vec3(direction.x, 0.0f, direction.z))
              : kForward),
      seed_(seed) {
  Reset();
}

float Beaver::Uniform(float from, float to) {
  return from + (to - from) * static_cast<float>(random_() >> 8) *
                    (1.0f / 16777216.0f);
}

void Beaver::Reset() {
  random_.seed(seed_);
  position_ = home_;
  yaw_ = std::atan2(home_direction_.x, home_direction_.z);
  head_yaw_ = head_pitch_ = eye_yaw_ = eye_pitch_ = 0.0f;
  time_ = 0.0f;
  talking_ = false;
  talk_blend_ = mouth_ = nod_ = 0.0f;
  mouth_time_ = nibble_time_ = cheer_time_ = slap_time_ = wave_time_ = -1.0f;
  attentive_ = false;
  next_nibble_ = Uniform(2.0f, 5.0f);
  next_slap_ = Uniform(7.0f, 12.0f);
}

void Beaver::SetTalking(bool talking) {
  this->talking_ = talking;
  if (talking) {
    nibble_time_ = -1.0f;
  }
}

void Beaver::Speak() { mouth_time_ = 0.0f; }

void Beaver::Cheer() {
  cheer_time_ = 0.0f;
  slap_time_ = 0.25f;  // the tail slaps when it lands
  nibble_time_ = -1.0f;
}

void Beaver::Update(float time_delta, glm::vec3 bear) {
  if (time_delta <= 0.0f) {
    return;
  }
  time_ += time_delta;

  // Turns to the bear when it is close and looks at it, otherwise it looks
  // along the track and around a bit
  glm::vec3 to_bear = bear - position_;
  float horizontal = glm::length(glm::vec2(to_bear.x, to_bear.z));
  bool noticed = horizontal < kAttentionDistance && horizontal > 0.1f;
  // Waves when the bear comes (not when it was just here)
  if (noticed && !attentive_ && !talking_ && cheer_time_ < 0.0f) {
    wave_time_ = 0.0f;
    nibble_time_ = -1.0f;
  }
  attentive_ = noticed;
  float wanted_yaw = attentive_
                         ? std::atan2(to_bear.x, to_bear.z)
                         : std::atan2(home_direction_.x, home_direction_.z);
  float turn = WrapAngle(wanted_yaw - yaw_);
  yaw_ = WrapAngle(
      yaw_ + std::clamp(turn, -kTurnRate * time_delta, kTurnRate * time_delta));
  float look_yaw, look_pitch;
  if (attentive_) {
    glm::vec3 from_head = bear - GetFacePosition();
    look_yaw = WrapAngle(std::atan2(from_head.x, from_head.z) - yaw_);
    look_pitch = std::atan2(from_head.y,
                            glm::length(glm::vec2(from_head.x, from_head.z)));
  } else {
    look_yaw = 0.5f * std::sin(time_ * 0.37f) * std::sin(time_ * 0.11f + 1.0f);
    look_pitch = 0.1f * std::sin(time_ * 0.23f);
  }
  head_yaw_ =
      Approach(head_yaw_, std::clamp(look_yaw, -0.9f, 0.9f), 0.15f, time_delta);
  head_pitch_ = Approach(head_pitch_, std::clamp(look_pitch, -0.5f, 0.4f),
                         0.15f, time_delta);
  // The eyes are quicker than the head: they look there first and take the rest
  // of the turn the head cannot make
  eye_yaw_ = Approach(eye_yaw_, std::clamp(look_yaw - head_yaw_, -0.4f, 0.4f),
                      0.03f, time_delta);
  eye_pitch_ =
      Approach(eye_pitch_, std::clamp(look_pitch - head_pitch_, -0.3f, 0.3f),
               0.03f, time_delta);
  talk_blend_ = Approach(talk_blend_, talking_ ? 1.0f : 0.0f, 0.2f, time_delta);

  // The mouth opens and closes once per sound, the head nods with it
  if (mouth_time_ >= 0.0f) {
    mouth_time_ += time_delta;
    if (mouth_time_ > kSoundDuration) {
      mouth_time_ = -1.0f;
    }
  }
  float sound =
      mouth_time_ >= 0.0f ? std::sin(kPi * mouth_time_ / kSoundDuration) : 0.0f;
  nod_ = 0.07f * sound;

  // Between the conversations: nibbling (quick small bites, the hands at the
  // mouth) and slapping the tail
  if (nibble_time_ >= 0.0f) {
    nibble_time_ += time_delta;
    if (nibble_time_ > kNibbleDuration) {
      nibble_time_ = -1.0f;
    }
  } else if (!talking_ && cheer_time_ < 0.0f && wave_time_ < 0.0f) {
    next_nibble_ -= time_delta;
    if (next_nibble_ <= 0.0f) {
      nibble_time_ = 0.0f;
      next_nibble_ = Uniform(5.0f, 10.0f);
    }
  }
  float nibble = Envelope(nibble_time_, kNibbleDuration, 0.35f);
  mouth_ = std::max(sound, nibble * 0.35f * std::abs(std::sin(time_ * 19.0f)));

  if (wave_time_ >= 0.0f) {
    wave_time_ += time_delta;
    if (wave_time_ > kWaveDuration || talking_) {
      wave_time_ = -1.0f;
    }
  }
  if (cheer_time_ >= 0.0f) {
    cheer_time_ += time_delta;
    if (cheer_time_ > kCheerDuration) {
      cheer_time_ = -1.0f;
    }
  }
  if (slap_time_ >= 0.0f) {
    slap_time_ += time_delta;
    if (slap_time_ > kSlapDuration) {
      slap_time_ = -1.0f;
    }
  } else if (!talking_) {
    next_slap_ -= time_delta;
    if (next_slap_ <= 0.0f) {
      slap_time_ = 0.0f;
      next_slap_ = Uniform(9.0f, 16.0f);
    }
  }
}

glm::vec3 Beaver::GetMarkerPosition() const {
  return position_ + kUp * (kBeaverHeight * kScale + kMarkerHeight +
                            0.07f * std::sin(time_ * 2.2f));
}

glm::quat Beaver::GetMarkerRotation() const {
  return glm::angleAxis(time_ * 1.3f, kUp);
}

glm::vec3 Beaver::GetColliderCenter() const {
  return position_ + kUp * (0.42f * kScale);
}

float Beaver::GetColliderRadius() const { return 0.42f * kScale; }

glm::vec3 Beaver::GetFacePosition() const {
  std::vector<glm::mat4> pose = GetPose();
  return 0.5f * (glm::vec3(pose[BeaverModel::kEyeLeft][3]) +
                 glm::vec3(pose[BeaverModel::kEyeRight][3]));
}

glm::vec3 Beaver::GetHeadPosition() const {
  glm::quat turn = glm::angleAxis(yaw_, kUp);
  return position_ +
         turn * ((kBeaverNeck + glm::vec3(0.0f, 0.12f, 0.1f)) * kScale);
}

std::vector<glm::mat4> Beaver::GetPose() const {
  std::vector<glm::mat4> parts(BeaverModel::kPartCount);
  const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(kScale));

  // Cheering: a hop with the arms up
  float hop = 0.0f, hop_pitch = 0.0f;
  float cheer = Envelope(cheer_time_, kCheerDuration, 0.15f);
  if (cheer_time_ >= 0.0f && cheer_time_ < 0.45f) {
    float t = cheer_time_ / 0.45f;
    hop = 4.0f * 0.12f * t * (1.0f - t) * kScale;
    hop_pitch = -0.12f * std::sin(kPi * t);
  }
  float breathing = 1.0f + 0.012f * std::sin(time_ * 2.1f);
  float nibble = Envelope(nibble_time_, kNibbleDuration, 0.35f);
  // Leans towards the bear while talking and over its hands while nibbling
  float pitch = 0.05f * talk_blend_ + 0.08f * nibble + hop_pitch +
                0.015f * std::sin(time_ * 1.3f);

  glm::mat4 frame = glm::translate(glm::mat4(1.0f), position_ + kUp * hop) *
                    glm::rotate(glm::mat4(1.0f), yaw_, kUp) * RotationX(pitch);
  parts[BeaverModel::kBody] =
      frame * scale *
      glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, breathing, 1.0f));

  glm::mat4 head =
      frame *
      glm::translate(glm::mat4(1.0f),
                     kBeaverNeck * kScale * glm::vec3(1.0f, breathing, 1.0f)) *
      glm::rotate(glm::mat4(1.0f), head_yaw_, kUp) *
      RotationX(-head_pitch_ - pitch + nod_ +
                0.1f * nibble)  // the body leans, the head stays level
      * glm::rotate(glm::mat4(1.0f),
                    0.08f * talk_blend_ * std::sin(time_ * 1.7f), kForward);
  parts[BeaverModel::kHead] = head * scale;
  parts[BeaverModel::kJaw] =
      head * glm::translate(glm::mat4(1.0f), kBeaverJaw * kScale) *
      RotationX(0.5f * mouth_) * scale;
  // The eyes turn in their sockets (yaw left, pitch up), blinking is not needed
  // with such small eyes
  for (bool left : {true, false}) {
    parts[left ? BeaverModel::kEyeLeft : BeaverModel::kEyeRight] =
        head *
        glm::translate(glm::mat4(1.0f), BeaverModel::EyeCenter(left) * kScale) *
        glm::rotate(glm::mat4(1.0f), eye_yaw_, kUp) * RotationX(-eye_pitch_) *
        scale;
  }

  // Arms: gestures while talking (one after the other), at the mouth while
  // nibbling, up in the air when cheering
  for (int side = 0; side < 2; ++side) {
    float sign = side == 0 ? 1.0f : -1.0f;
    float phase = side == 0 ? 0.0f : 2.2f;
    float gesture =
        talk_blend_ * (0.45f + 0.4f * std::sin(time_ * 2.9f + phase));
    // It waves with its right paw
    float wave = side == 1 ? Envelope(wave_time_, kWaveDuration, 0.25f) : 0.0f;
    float raise = kArmRest + gesture + (kArmAtMouth - kArmRest) * nibble +
                  2.3f * cheer + 2.0f * wave;
    float out =
        talk_blend_ * 0.25f * (0.5f + 0.5f * std::sin(time_ * 2.1f + phase)) -
        0.3f * nibble + 0.45f * cheer +
        wave * (0.35f + 0.3f * std::sin(wave_time_ * 11.0f));
    glm::vec3 shoulder = kBeaverShoulder * glm::vec3(sign, 1.0f, 1.0f) * kScale;
    parts[side == 0 ? BeaverModel::kArmLeft : BeaverModel::kArmRight] =
        frame * glm::translate(glm::mat4(1.0f), shoulder) *
        glm::rotate(glm::mat4(1.0f), sign * out, kForward) * RotationX(-raise) *
        scale;
  }

  // The feet stay on the ground (only turned with the body), they point a bit
  // outwards
  glm::mat4 turn = glm::rotate(glm::mat4(1.0f), yaw_, kUp);
  for (int side = 0; side < 2; ++side) {
    float sign = side == 0 ? 1.0f : -1.0f;
    glm::vec3 foot = glm::vec3(
        turn *
        glm::vec4(kBeaverFoot * glm::vec3(sign, 1.0f, 1.0f) * kScale, 0.0f));
    parts[side == 0 ? BeaverModel::kFootLeft : BeaverModel::kFootRight] =
        glm::translate(glm::mat4(1.0f), position_ + foot + kUp * hop) * turn *
        glm::rotate(glm::mat4(1.0f), 0.3f * sign, kUp) * scale;
  }

  // The tail lies on the ground and sways a little, it is lifted and slapped
  // down (the body leaning forward lifts it)
  float lift = 0.0f;
  if (slap_time_ >= 0.0f) {
    float t = slap_time_;
    lift = t < 0.35f ? 0.7f * Smooth(t / 0.35f)
                     : 0.7f * (1.0f - Smooth((t - 0.35f) / 0.08f)) +
                           0.08f * std::sin(kPi * std::clamp((t - 0.43f) / 0.2f,
                                                             0.0f, 1.0f));
  }
  parts[BeaverModel::kTail] =
      frame * glm::translate(glm::mat4(1.0f), kBeaverTail * kScale) *
      glm::rotate(glm::mat4(1.0f), 0.12f * std::sin(time_ * 0.9f), kUp) *
      RotationX(lift - pitch) * scale;
  return parts;
}

}  // namespace bibo::scene

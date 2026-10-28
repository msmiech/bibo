#include "Raccoon.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "RaccoonModel.hpp"

namespace bibo::scene {

namespace {
const float kPi = glm::pi<float>();
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const glm::vec3 kLeft(1.0f, 0.0f, 0.0f);
const glm::vec3 kForward(0.0f, 0.0f, 1.0f);
const float kTurnRate = 4.0f;  // radians per second
// Upright it leans back this far (radians), sitting a bit less; its hips are
// this much lower then (model units)
const float kRearAngle = 1.15f, kSitUpright = 0.85f, kHipDrop = 0.085f;
// The hind legs are folded forward under it when it is upright
const float kLegFold = 0.8f;
const float kStrideLength = 0.32f * Raccoon::kScale;  // distance per step cycle
const float kSoundDuration = 0.15f;
const float kHopDuration = 0.35f, kHopHeight = 0.18f;
const float kJumpDuration = 0.6f, kJumpHeight = 0.55f;
const float kWashDuration = 2.6f;
// Height of the marker above the face (world units)
const float kMarkerHeight = 1.0f;

float WrapAngle(float angle) { return std::remainder(angle, 2.0f * kPi); }

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

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

glm::mat4 RotationY(float angle) {
  return glm::rotate(glm::mat4(1.0f), angle, kUp);
}

glm::mat4 Translation(glm::vec3 offset) {
  return glm::translate(glm::mat4(1.0f), offset);
}

// Advances a timer of an animation, -1 when it is over
void Advance(float& timer, float duration, float time_delta) {
  if (timer >= 0.0f) {
    timer += time_delta;
    if (timer > duration) {
      timer = -1.0f;
    }
  }
}

float Arc(float timer, float duration, float height) {
  if (timer < 0.0f) {
    return 0.0f;
  }
  float t = timer / duration;
  return 4.0f * height * t * (1.0f - t);
}
}  // namespace

Raccoon::Raccoon(Ground ground, std::uint32_t seed, float girth)
    : ground_(std::move(ground)), seed_(seed), girth_(girth) {
  Reset();
}

float Raccoon::Uniform(float from, float to) {
  return from + (to - from) * static_cast<float>(random_() >> 8) *
                    (1.0f / 16777216.0f);
}

void Raccoon::Reset() {
  random_.seed(seed_);
  time_ = activity_time_ = 0.0f;
  talking_ = false;
  talk_blend_ = mouth_ = nod_ = stride_ = gait_phase_ = head_yaw_ =
      head_pitch_ = 0.0f;
  mouth_time_ = hop_time_ = jump_time_ = wash_time_ = -1.0f;
  next_hop_ = Uniform(0.3f, 1.0f);
  next_wash_ = Uniform(3.0f, 6.0f);
  chatters_ = 0;
  if (sits_) {
    position_ = home_;
    yaw_ = std::atan2(home_direction_.x, home_direction_.z);
    rear_ = kSitUpright;
    activity_ = Activity::kSitting;
  } else {
    rear_ = 0.0f;
    activity_ = Activity::kHidden;
  }
}

void Raccoon::SetActivity(Activity next) {
  activity_ = next;
  activity_time_ = 0.0f;
}

void Raccoon::ComeAndCheer(glm::vec2 from, glm::vec2 spot) {
  float height = 0.0f;
  ground_(from, height);
  origin_ = from;
  target_ = spot;
  position_ = glm::vec3(from.x, height, from.y);
  yaw_ = std::atan2(spot.x - from.x, spot.y - from.y);
  rear_ = 0.0f;
  hop_time_ = jump_time_ = wash_time_ = -1.0f;
  SetActivity(Activity::kComing);
}

void Raccoon::Leave() {
  if (activity_ == Activity::kComing || activity_ == Activity::kCheering) {
    target_ = origin_;
    SetActivity(Activity::kLeaving);
  }
}

void Raccoon::SitAt(glm::vec3 position, glm::vec3 direction) {
  home_ = position;
  glm::vec3 flat(direction.x, 0.0f, direction.z);
  home_direction_ = glm::length(flat) > 1e-6f ? glm::normalize(flat) : kForward;
  sits_ = true;
  Reset();
}

void Raccoon::Cheer() {
  if (IsVisible() && jump_time_ < 0.0f) {
    jump_time_ = 0.0f;
    wash_time_ = -1.0f;
    mouth_time_ = 0.0f;
    ++chatters_;
  }
}

void Raccoon::SetTalking(bool talking) {
  this->talking_ = talking;
  if (talking) {
    wash_time_ = -1.0f;
  }
}

void Raccoon::Speak() { mouth_time_ = 0.0f; }

int Raccoon::TakeChatters() {
  int result = chatters_;
  chatters_ = 0;
  return result;
}

void Raccoon::TurnTo(float wanted_yaw, float rate, float time_delta) {
  float turn = WrapAngle(wanted_yaw - yaw_);
  yaw_ =
      WrapAngle(yaw_ + std::clamp(turn, -rate * time_delta, rate * time_delta));
}

bool Raccoon::Walk(float time_delta, float speed) {
  glm::vec2 here(position_.x, position_.z);
  glm::vec2 to_target = target_ - here;
  float distance = glm::length(to_target);
  if (distance < 0.25f) {
    return true;
  }
  // Turns towards the target, slower while it does not look there; it slows
  // down before it gets there
  float difference = WrapAngle(std::atan2(to_target.x, to_target.y) - yaw_);
  TurnTo(yaw_ + difference, kTurnRate, time_delta);
  float pace = speed * std::clamp(std::cos(difference), 0.0f, 1.0f) *
               std::clamp(0.4f + distance / 2.0f, 0.0f, 1.0f);
  glm::vec2 next = here + glm::vec2(std::sin(yaw_), std::cos(yaw_)) *
                              std::min(pace * time_delta, distance);
  float height;
  if (!ground_(next, height)) {
    return true;  // e.g. the water: it stays where it is
  }
  position_ = glm::vec3(
      next.x,
      position_.y + (height - position_.y) * std::min(1.0f, time_delta * 12.0f),
      next.y);
  gait_phase_ += pace * time_delta / kStrideLength;
  return false;
}

void Raccoon::Update(float time_delta, glm::vec3 bear) {
  if (time_delta <= 0.0f || activity_ == Activity::kHidden) {
    return;
  }
  time_ += time_delta;
  activity_time_ += time_delta;
  glm::vec3 to_bear = bear - position_;
  float bear_distance = glm::length(glm::vec2(to_bear.x, to_bear.z));
  float bear_yaw = std::atan2(to_bear.x, to_bear.z);

  bool walking = false;
  float upright = 0.0f;
  bool attentive = false;
  switch (activity_) {
    case Activity::kComing:
      walking = !Walk(time_delta, kTrotSpeed);
      if (!walking) {
        SetActivity(Activity::kCheering);
      }
      break;
    case Activity::kLeaving:
      walking = !Walk(time_delta, 1.3f * kTrotSpeed);
      if (!walking) {
        SetActivity(Activity::kHidden);
        return;
      }
      break;
    case Activity::kCheering:
      // Upright, turned to the bear, it hops and chatters every now and then
      upright = 1.0f;
      attentive = true;
      TurnTo(bear_yaw, 0.6f * kTurnRate, time_delta);
      next_hop_ -= time_delta;
      if (next_hop_ <= 0.0f && hop_time_ < 0.0f && jump_time_ < 0.0f) {
        hop_time_ = 0.0f;
        mouth_time_ = 0.0f;
        ++chatters_;
        next_hop_ = Uniform(0.6f, 1.6f);
      }
      break;
    case Activity::kSitting:
      upright = kSitUpright;
      attentive = talking_ || bear_distance < kAttentionDistance;
      TurnTo(attentive && bear_distance > 0.1f
                 ? bear_yaw
                 : std::atan2(home_direction_.x, home_direction_.z),
             0.5f * kTurnRate, time_delta);
      // Washes its paws between the conversations
      if (wash_time_ < 0.0f && !talking_) {
        next_wash_ -= time_delta;
        if (next_wash_ <= 0.0f) {
          wash_time_ = 0.0f;
          next_wash_ = Uniform(5.0f, 10.0f);
        }
      }
      break;
    case Activity::kHidden:
      break;
  }
  Advance(wash_time_, kWashDuration, time_delta);
  Advance(hop_time_, kHopDuration, time_delta);
  Advance(jump_time_, kJumpDuration, time_delta);
  rear_ = Approach(rear_, upright, 0.12f, time_delta);
  stride_ = Approach(stride_, walking ? 1.0f : 0.0f, 0.08f, time_delta);
  gait_phase_ -= std::floor(gait_phase_);
  talk_blend_ = Approach(talk_blend_, talking_ ? 1.0f : 0.0f, 0.2f, time_delta);

  // The head looks at the bear, otherwise ahead and around a bit
  float look_yaw = 0.0f, look_pitch = 0.0f;
  if (attentive) {
    glm::vec3 from_face = bear - GetFacePosition();
    look_yaw = WrapAngle(std::atan2(from_face.x, from_face.z) - yaw_);
    look_pitch = std::atan2(from_face.y,
                            glm::length(glm::vec2(from_face.x, from_face.z)));
  } else if (!walking) {
    look_yaw = 0.6f * std::sin(time_ * 0.43f) * std::sin(time_ * 0.17f + 2.0f);
    look_pitch = 0.1f * std::sin(time_ * 0.29f);
  }
  head_yaw_ =
      Approach(head_yaw_, std::clamp(look_yaw, -1.0f, 1.0f), 0.12f, time_delta);
  head_pitch_ = Approach(head_pitch_, std::clamp(look_pitch, -0.5f, 0.6f),
                         0.12f, time_delta);

  Advance(mouth_time_, kSoundDuration, time_delta);
  float sound =
      mouth_time_ >= 0.0f ? std::sin(kPi * mouth_time_ / kSoundDuration) : 0.0f;
  nod_ = 0.08f * sound;
  float washing = Envelope(wash_time_, kWashDuration, 0.3f);
  mouth_ = std::max(sound, 0.15f * washing * std::abs(std::sin(time_ * 9.0f)));
}

glm::vec3 Raccoon::GetFacePosition() const {
  std::vector<glm::mat4> pose = GetPose();
  return glm::vec3(pose[RaccoonModel::kHead] *
                   glm::vec4(0.0f, 0.075f, 0.15f, 1.0f));
}

glm::vec3 Raccoon::GetMarkerPosition() const {
  return GetFacePosition() +
         kUp * (kMarkerHeight + 0.07f * std::sin(time_ * 2.2f));
}

glm::quat Raccoon::GetMarkerRotation() const {
  return glm::angleAxis(time_ * 1.3f, kUp);
}

glm::vec3 Raccoon::GetColliderCenter() const {
  return position_ + kUp * (0.3f * kScale);
}

float Raccoon::GetColliderRadius() const {
  return 0.24f * kScale * std::sqrt(girth_);
}

std::vector<glm::mat4> Raccoon::GetPose() const {
  std::vector<glm::mat4> parts(RaccoonModel::kPartCount);
  const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(kScale));
  const float upright = Smooth(rear_);
  const float cycle = 2.0f * kPi * gait_phase_;
  const float jump = Envelope(jump_time_, kJumpDuration, 0.12f);
  const float washing = Envelope(wash_time_, kWashDuration, 0.3f);

  // Hops while cheering, the big jump; bobs with every step
  float lift = Arc(hop_time_, kHopDuration, kHopHeight) +
               Arc(jump_time_, kJumpDuration, kJumpHeight) +
               stride_ * 0.025f * kScale * std::abs(std::sin(cycle));
  float breathing = 0.012f * std::sin(time_ * 2.3f);
  float lean = kRearAngle * upright + 0.12f * jump + breathing -
               stride_ * 0.04f * std::sin(2.0f * cycle);

  glm::mat4 frame = Translation(position_ + kUp * lift) * RotationY(yaw_);
  // The body turns up around the hips, which come down when it sits on its
  // haunches
  glm::mat4 body =
      frame *
      Translation((kRaccoonHips - kUp * (kHipDrop * upright)) * kScale) *
      RotationX(-lean) * Translation(-kRaccoonHips * kScale);
  parts[RaccoonModel::kBody] = body * scale;

  // The head stays about level and looks around (nods with its sounds, down to
  // its paws while it washes them)
  glm::mat4 head = body * Translation(kRaccoonNeck * kScale) *
                   RotationX(0.9f * lean) * RotationY(head_yaw_) *
                   RotationX(-head_pitch_ + nod_ + 0.35f * washing +
                             0.08f * talk_blend_ * std::sin(time_ * 1.9f));
  parts[RaccoonModel::kHead] = head * scale;
  parts[RaccoonModel::kJaw] = head * Translation(kRaccoonJaw * kScale) *
                              RotationX(0.55f * mouth_) * scale;

  // The tail hangs a bit when it trots, swings from side to side and lies on
  // the ground behind it when it is upright
  float wag = std::sin(time_ * (2.0f + 4.0f * upright)) *
              (0.15f + 0.25f * upright + 0.1f * stride_);
  parts[RaccoonModel::kTail] =
      body * Translation(kRaccoonTail * kScale) * RotationY(wag) *
      RotationX(0.95f * lean - 0.25f + 0.1f * stride_) * scale;

  for (int side = 0; side < 2; ++side) {
    float sign = side == 0 ? 1.0f : -1.0f;
    // Trot: diagonal pairs of legs step together
    float front_swing =
        stride_ * 0.5f * std::sin(cycle + (side == 0 ? 0.0f : kPi));
    float hind_swing =
        stride_ * 0.5f * std::sin(cycle + (side == 0 ? kPi : 0.0f));

    // Front paws: hanging in front of the chest when it is upright, waving when
    // it cheers, rubbed together when it washes them, gestures when it talks
    float phase = side == 0 ? 0.0f : kPi;
    float wave = rear_ > 0.9f && activity_ == Activity::kCheering
                     ? 0.8f + 0.5f * std::sin(time_ * 9.0f + phase)
                     : 0.0f;
    float raise = upright * (0.25f + wave) + 1.7f * jump + 0.7f * washing +
                  talk_blend_ * (0.5f + 0.35f * std::sin(time_ * 3.1f + phase));
    float inwards =
        0.35f * washing * (1.0f + 0.4f * std::sin(time_ * 12.0f + phase)) -
        0.3f * jump;
    glm::vec3 shoulder =
        kRaccoonShoulder * glm::vec3(sign, 1.0f, 1.0f) * kScale;
    parts[side == 0 ? RaccoonModel::kFrontLeft : RaccoonModel::kFrontRight] =
        body * Translation(shoulder) *
        glm::rotate(glm::mat4(1.0f), -sign * inwards, kForward) *
        RotationX(front_swing + lean * 0.65f * upright - raise) * scale;

    // Hind legs: under the hips, turned with the body (not leaned with it),
    // folded forward when it is upright
    glm::vec3 hip = (kRaccoonHip * glm::vec3(sign, 1.0f, 1.0f) -
                     kUp * (kHipDrop * upright)) *
                    kScale;
    parts[side == 0 ? RaccoonModel::kHindLeft : RaccoonModel::kHindRight] =
        frame * Translation(hip) * RotationX(hind_swing - kLegFold * upright) *
        scale;
  }
  return parts;
}

RaccoonFans::RaccoonFans(Raccoon::Ground ground, Finder finder,
                         std::uint32_t seed)
    : finder_(std::move(finder)), seed_(seed) {
  for (std::size_t i = 0; i < kCount; ++i) {
    raccoons_.emplace_back(ground, seed + 31u * static_cast<std::uint32_t>(i));
  }
  Reset();
}

void RaccoonFans::Reset() {
  random_.seed(seed_);
  for (Raccoon& raccoon : raccoons_) {
    raccoon.Reset();
  }
  next_arrival_ = 1.0f;
}

int RaccoonFans::GetWatchingCount() const {
  int count = 0;
  for (const Raccoon& raccoon : raccoons_) {
    count += raccoon.GetActivity() == Raccoon::Activity::kComing ||
                     raccoon.GetActivity() == Raccoon::Activity::kCheering
                 ? 1
                 : 0;
  }
  return count;
}

void RaccoonFans::Update(float time_delta, bool cheering, glm::vec3 bear,
                         glm::vec3 bear_head, glm::vec3 heading) {
  if (time_delta <= 0.0f) {
    return;
  }
  for (Raccoon& raccoon : raccoons_) {
    raccoon.Update(time_delta, bear_head);
    glm::vec3 offset = raccoon.GetPosition() - bear;
    bool past = glm::length(glm::vec2(offset.x, offset.z)) > kGoneDistance;
    bool tired = (raccoon.GetActivity() == Raccoon::Activity::kCheering &&
                  raccoon.GetActivityTime() > kWatchTime) ||
                 (raccoon.GetActivity() == Raccoon::Activity::kComing &&
                  raccoon.GetActivityTime() > 2.0f * kWatchTime);
    if (!cheering || past || tired) {
      raccoon.Leave();
    }
  }
  if (!cheering) {
    next_arrival_ = std::min(next_arrival_, 1.0f);
    return;
  }

  // Another one comes out of the woods every few seconds, not too close to the
  // others
  next_arrival_ -= time_delta;
  if (next_arrival_ > 0.0f || GetWatchingCount() >= kMaxWatching) {
    return;
  }
  next_arrival_ = 0.4f;  // tries again soon if there is no spot
  auto hidden = std::ranges::find_if(
      raccoons_, [](const Raccoon& raccoon) { return !raccoon.IsVisible(); });
  glm::vec2 from, spot;
  if (hidden == raccoons_.end() ||
      !finder_(bear, heading, random_, from, spot)) {
    return;
  }
  for (const Raccoon& other : raccoons_) {
    glm::vec3 there = other.GetPosition();
    if (other.IsVisible() &&
        glm::distance(glm::vec2(there.x, there.z), spot) < kSpacing) {
      return;
    }
  }
  hidden->ComeAndCheer(from, spot);
  std::uniform_real_distribution<float> wait(2.5f, 5.0f);
  next_arrival_ = wait(random_);
}

void RaccoonFans::Celebrate() {
  for (Raccoon& raccoon : raccoons_) {
    if (raccoon.GetActivity() == Raccoon::Activity::kCheering) {
      raccoon.Cheer();
    }
  }
}

int RaccoonFans::TakeChatters() {
  int result = 0;
  for (Raccoon& raccoon : raccoons_) {
    result += raccoon.TakeChatters();
  }
  return result;
}

}  // namespace bibo::scene

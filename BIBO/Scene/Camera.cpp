#include "Camera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

#include "../Animation/Skeleton.hpp"
#include "../Util/Input.hpp"

namespace bibo::scene {

using glm::mat4;
using glm::perspective;
using glm::radians;
using glm::vec3;

using animation::SmoothingFactor;
using glm::vec4;

namespace {
// The locked camera orbits around this point above the center of the target,
// looking down a little by default
const float kPivotHeight = 0.75f;
const float kDefaultPitch = glm::radians(17.0f);
const float kMinPitch = glm::radians(-20.0f),
            // from slightly below to high above
    kMaxPitch = glm::radians(70.0f);
const float kFreeMaxPitch = glm::radians(89.0f);
// Following: closely in the horizontal, the height lags behind (a jump does not
// shake the view), but not too far
const float kFollowHalfLife = 0.03f, kHeightHalfLife = 0.08f,
            kAirHeightHalfLife = 0.25f, kMaxHeightLag = 1.5f;
// When the player did not turn the camera for a while, it goes back behind the
// running target (not when it runs towards the camera)
const float kAutoRecenterDelay = 1.5f, kAutoRecenterHalfLife = 0.9f,
            kAutoRecenterMaxAngle = glm::radians(110.0f);
const float kRecenterHalfLife = 0.06f;
// Obstacles pull the camera in at once, it moves back out smoothly
const float kReturnHalfLife = 0.2f;
// The field of view widens a bit at full speed
const float kBaseAperture = 60.0f, kSpeedAperture = 6.0f;
// Shots: how fast the camera moves to the eye and turns to the target, how long
// it blends back to following
const float kShotMoveHalfLife = 0.3f, kShotTurnHalfLife = 0.12f,
            kShotBlendTime = 0.8f;

float WrapAngle(float angle) {
  const float pi = glm::pi<float>();
  angle = std::fmod(angle + pi, 2.0f * pi);
  return angle < 0.0f ? angle + pi : angle - pi;
}

float YawOf(vec3 direction) { return std::atan2(direction.x, direction.z); }
}  // namespace

Camera::Camera(vec3 position, vec3 direction, vec3 up, GLFWwindow& window)
    : SceneObject(window), position_(position), up_(up) {
  glfwGetWindowSize(&window, &window_width_, &window_height_);
  SetDirection(direction);
}

Camera::~Camera() {}

void Camera::ApplyAngles() {
  direction_ = normalize(
      vec3(cos(pitch_) * sin(yaw_), -sin(pitch_), cos(pitch_) * cos(yaw_)));
}

void Camera::SetDirection(vec3 new_direction) {
  direction_ = normalize(new_direction);
  yaw_ = std::atan2(direction_.x, direction_.z);
  pitch_ = -std::asin(std::clamp(direction_.y, -1.0f, 1.0f));
}

void Camera::Update(float time_delta) {
  // Toggled once per key press, not in every frame while the key is held
  bool lock_key_pressed = input::IsKeyDown(window_, GLFW_KEY_L);
  if (lock_key_pressed && !lock_key_was_pressed_) {
    locked_ = !locked_;
    if (!locked_) {
      pitch_ = std::clamp(pitch_, -kFreeMaxPitch, kFreeMaxPitch);
    }
  }
  lock_key_was_pressed_ = lock_key_pressed;
  if (!locked_) {
    auto velocity = speed_ * time_delta;
    vec3 right = normalize(cross(direction_, up_));
    if (input::IsKeyDown(window_, GLFW_KEY_W)) {
      position_ += direction_ * velocity;
    }
    if (input::IsKeyDown(window_, GLFW_KEY_S)) {
      position_ -= direction_ * velocity;
    }
    if (input::IsKeyDown(window_, GLFW_KEY_A)) {
      position_ -= right * velocity;
    }
    if (input::IsKeyDown(window_, GLFW_KEY_D)) {
      position_ += right * velocity;
    }
  }
}

void Camera::Rotate(float right, float down) {
  if (right == 0.0f && down == 0.0f) {
    return;
  }
  yaw_ = WrapAngle(yaw_ - right);
  pitch_ = locked_ ? std::clamp(pitch_ + down, kMinPitch, kMaxPitch)
                   : std::clamp(pitch_ + down, -kFreeMaxPitch, kFreeMaxPitch);
  time_since_rotated_ = 0.0f;
  recentering_ = false;
  ApplyAngles();
  if (locked_ && has_pivot_) {
    PlaceAroundPivot(0.0f);
  }
}

void Camera::Zoom(float steps) {
  target_distance_ =
      std::clamp(target_distance_ - 0.8f * steps, kMinDistance, kMaxDistance);
}

void Camera::Recenter() { recentering_ = true; }

vec3 Camera::PivotOf(const FollowTarget& target) const {
  return target.position + vec3(0.0f, kPivotHeight, 0.0f);
}

void Camera::SnapBehind(const FollowTarget& target) {
  pivot_ = PivotOf(target);
  has_pivot_ = true;
  yaw_ = YawOf(target.direction);
  pitch_ = kDefaultPitch;
  distance_ = target_distance_;
  time_since_rotated_ = 0.0f;
  recentering_ = false;
  aperture_ = kBaseAperture;
  in_shot_ = false;
  shot_blend_ = 0.0f;
  ApplyAngles();
  PlaceAroundPivot(0.0f);
}

void Camera::Shot(vec3 eye, vec3 target, float time_delta, bool cut) {
  if (collision_probe_) {
    eye =
        target + (eye - target) *
                     std::clamp(collision_probe_(target, eye, kCollisionRadius),
                                0.0f, 1.0f);
  }
  if (cut || (!in_shot_ && shot_blend_ <= 0.0f)) {
    position_ = eye;
    SetDirection(target - eye);
  } else {
    position_ +=
        (eye - position_) * SmoothingFactor(kShotMoveHalfLife, time_delta);
    vec3 wanted = target - position_;
    if (length(wanted) > 1e-4f) {
      SetDirection(mix(direction_, normalize(wanted),
                       SmoothingFactor(kShotTurnHalfLife, time_delta)));
    }
  }
  aperture_ += (kBaseAperture - aperture_) * SmoothingFactor(0.3f, time_delta);
  in_shot_ = true;
  shot_blend_ = 0.0f;
}

void Camera::Follow(const FollowTarget& target, float time_delta) {
  if (!has_pivot_) {
    SnapBehind(target);
    return;
  }
  if (time_delta <= 0.0f) {
    return;
  }
  if (in_shot_) {
    // Back from a shot: the camera orbits from where the shot looked and blends
    // over from where it was
    in_shot_ = false;
    shot_blend_ = 1.0f;
    shot_position_ = position_;
    shot_direction_ = direction_;
    SetDirection(direction_);
    pitch_ = std::clamp(pitch_, kMinPitch, kMaxPitch);
    time_since_rotated_ = 0.0f;
  }

  vec3 goal = PivotOf(target);
  float horizontal = SmoothingFactor(kFollowHalfLife, time_delta);
  pivot_.x += (goal.x - pivot_.x) * horizontal;
  pivot_.z += (goal.z - pivot_.z) * horizontal;
  pivot_.y +=
      (goal.y - pivot_.y) *
      SmoothingFactor(target.airborne ? kAirHeightHalfLife : kHeightHalfLife,
                      time_delta);
  pivot_.y =
      std::clamp(pivot_.y, goal.y - kMaxHeightLag, goal.y + kMaxHeightLag);

  float speed = length(vec3(target.velocity.x, 0.0f, target.velocity.z));
  float behind = YawOf(target.direction);
  time_since_rotated_ += time_delta;
  if (recentering_) {
    float turn = WrapAngle(behind - yaw_);
    yaw_ =
        WrapAngle(yaw_ + turn * SmoothingFactor(kRecenterHalfLife, time_delta));
    pitch_ += (kDefaultPitch - pitch_) *
              SmoothingFactor(kRecenterHalfLife, time_delta);
    recentering_ =
        std::abs(turn) > 0.01f || std::abs(kDefaultPitch - pitch_) > 0.01f;
  } else if (time_since_rotated_ > kAutoRecenterDelay && speed > 2.0f) {
    float turn = WrapAngle(behind - yaw_);
    if (std::abs(turn) < kAutoRecenterMaxAngle) {
      float amount = SmoothingFactor(kAutoRecenterHalfLife, time_delta) *
                     std::clamp(speed / 10.0f, 0.0f, 1.0f);
      yaw_ = WrapAngle(yaw_ + turn * amount);
      pitch_ += (kDefaultPitch - pitch_) * amount;
    }
  }
  ApplyAngles();

  distance_ +=
      (target_distance_ - distance_) * SmoothingFactor(0.08f, time_delta);
  float target_aperture =
      kBaseAperture +
      kSpeedAperture * std::clamp((speed - 4.0f) / 6.0f, 0.0f, 1.0f);
  aperture_ +=
      (target_aperture - aperture_) * SmoothingFactor(0.3f, time_delta);

  PlaceAroundPivot(time_delta);
  if (shot_blend_ > 0.0f) {
    float weight = shot_blend_ * shot_blend_ * (3.0f - 2.0f * shot_blend_);
    position_ = mix(position_, shot_position_, weight);
    direction_ = normalize(mix(direction_, shot_direction_, weight));
    shot_blend_ = std::max(0.0f, shot_blend_ - time_delta / kShotBlendTime);
  }
}

void Camera::PlaceAroundPivot(float time_delta) {
  vec3 back = -direction_;
  float allowed = distance_;
  if (collision_probe_) {
    allowed = distance_ *
              std::clamp(collision_probe_(pivot_, pivot_ + back * distance_,
                                          kCollisionRadius),
                         0.0f, 1.0f);
  }
  if (allowed < current_distance_ || time_delta <= 0.0f) {
    current_distance_ = allowed;  // never through a wall
  } else {
    current_distance_ += (allowed - current_distance_) *
                         SmoothingFactor(kReturnHalfLife, time_delta);
  }
  position_ = pivot_ + back * current_distance_;
}

vec3 Camera::GetForward() const {
  return vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
}

vec3 Camera::GetRight() const {
  return vec3(-std::cos(yaw_), 0.0f, std::sin(yaw_));
}

void Camera::SetTarget(vec3 target) { SetDirection(target - position_); }

void Camera::SetView(vec3 eye, vec3 target_position) {
  position_ = eye;
  SetDirection(target_position - eye);
}

void Camera::SetTarget(vec3 target_position, vec3 target_direction) {
  vec3 target_distance =
      normalize(target_direction) * (-3.0f) - vec3(0.0f, -2.5f, 0.0f);
  mat4 translation_matrix = translate(mat4(1.0f), target_distance);
  position_ = vec3(translation_matrix * vec4(target_position, 1.0f));
  SetDirection(target_position - position_);
}

mat4 Camera::GetViewMatrix() {
  return lookAt(position_, position_ + direction_, up_);
}

mat4 Camera::GetProjectionMatrix() {
  return perspective(radians(aperture_), GetAspectRatio(), z_near_, z_far_);
}

ViewFrustum Camera::GetViewFrustum() {
  ViewFrustum frustum;
  frustum.SetCamInternals(aperture_, GetAspectRatio(), z_near_, z_far_);
  frustum.SetCamDef(position_, position_ + direction_, up_);
  return frustum;
}

float Camera::GetAspectRatio() const {
  return static_cast<float>(window_width_) / static_cast<float>(window_height_);
}

void Camera::SetViewportSize(int width, int height) {
  if (width > 0 && height > 0) {
    window_width_ = width;
    window_height_ = height;
  }
}

bool Camera::IsLocked() { return locked_; }

}  // namespace bibo::scene

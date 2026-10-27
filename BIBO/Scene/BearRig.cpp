#include "BearRig.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../Animation/InverseKinematics.hpp"

namespace bibo::scene {

using animation::Pose;
using animation::RotationBetween;
using animation::SmoothingFactor;
using animation::Transform;

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
const glm::vec3 kLeft(1.0f, 0.0f, 0.0f);
const float kPi = glm::pi<float>();
// The front legs are straight in the bind pose, standing a bit lower bends them
// slightly (near full extension a few centimeters already bend the elbows a
// lot)
const float kStandHeight = -0.01f;
// The legs are only tucked in after being in the air for a moment (not when
// stepping over a bump)
const float kTuckDelay = 0.08f;
// How far the body may move while a foot is on the ground
const float kMaxStanceTravel = 0.6f;
// Height of the axis the body rolls around when it lies on its side or back
const float kRollPivot = 0.5f;
// Lengths of the spring bones (from the bone to the tip)
const float kEarLength = 0.26f;
const float kTailLength = 0.28f;
// Face: the lips press together a little when the mouth is closed, a yawn opens
// it this far
const float kJawPressDegrees = 3.0f;
const float kJawOpenDegrees = 44.0f;
// The tongue slides this far out of the mouth; it only bends down where it is
// past the lower lip (distance from the root of the tongue, see TONGUE_CENTER
// and the jaw in tools/build_bear_cub.py), so it hangs over the lip instead of
// going through the jaw
const float kTongueReach = 0.3f;
const float kTongueLipDistance = 0.4f;
// How far the eyelids turn to close the eye (radians): they meet a bit below
// the middle of the eye
const float kUpperLidClose = 0.88f;
const float kLowerLidClose = 0.34f;
const float kBlinkDuration = 0.2f;
// Blending into and out of animations (idle, after an idle was interrupted)
const float kBlendTime = 0.3f;

// Phase in the step cycle when the stance of a leg begins (front left, front
// right, hind left, hind right)
const float kWalkOffsets[BearRig::kLegCount] = {0.25f, 0.75f, 0.0f,
                                                0.5f};  // lateral sequence walk
const float kBoundOffsets[BearRig::kLegCount] = {
    0.5f, 0.6f, 0.0f,
    0.1f};  // half bound: hind legs together, then the front legs
const float kSwimOffsets[BearRig::kLegCount] = {
    0.0f, 0.5f, 0.5f, 0.0f};  // dog paddle: diagonal pairs of legs together

glm::quat RotationOf(const glm::mat4& matrix) {
  glm::mat3 rotation(glm::normalize(glm::vec3(matrix[0])),
                     glm::normalize(glm::vec3(matrix[1])),
                     glm::normalize(glm::vec3(matrix[2])));
  return glm::normalize(glm::quat_cast(rotation));
}

glm::vec3 PositionOf(const glm::mat4& matrix) { return glm::vec3(matrix[3]); }

glm::mat4 Rigid(const glm::mat4& matrix) {
  glm::mat4 result = glm::mat4_cast(RotationOf(matrix));
  result[3] = matrix[3];
  return result;
}

/**
 * Rotation in the frame of the body: pitch lifts the nose, yaw turns left, roll
 * lowers the right side
 */
glm::quat BodyRotation(float pitch, float yaw, float roll) {
  return glm::angleAxis(yaw, kUp) * glm::angleAxis(-pitch, kLeft) *
         glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
}

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float Fract(float x) { return x - std::floor(x); }

/**
 * Limits a rotation to a maximum angle
 */
glm::quat LimitAngle(const glm::quat& rotation, float max_angle) {
  float angle = 2.0f * std::acos(std::clamp(std::abs(rotation.w), 0.0f, 1.0f));
  if (angle <= max_angle) {
    return rotation;
  }
  return glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), rotation,
                    max_angle / angle);
}

float Uniform(std::mt19937& random, float from, float to) {
  return from +
         (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

FaceControls FaceControls::Mix(const FaceControls& a, const FaceControls& b,
                               float t) {
  FaceControls result;
  result.jaw_open = glm::mix(a.jaw_open, b.jaw_open, t);
  result.tongue_out = glm::mix(a.tongue_out, b.tongue_out, t);
  result.tongue_curl = glm::mix(a.tongue_curl, b.tongue_curl, t);
  result.tongue_side = glm::mix(a.tongue_side, b.tongue_side, t);
  result.lids = glm::mix(a.lids, b.lids, t);
  result.gaze_yaw = glm::mix(a.gaze_yaw, b.gaze_yaw, t);
  result.gaze_pitch = glm::mix(a.gaze_pitch, b.gaze_pitch, t);
  result.ears_back = glm::mix(a.ears_back, b.ears_back, t);
  return result;
}

BearRig::IdleKey BearRig::IdleKey::Mix(const IdleKey& a, const IdleKey& b,
                                       float t) {
  IdleKey result;
  result.time = glm::mix(a.time, b.time, t);
  result.pelvis_height = glm::mix(a.pelvis_height, b.pelvis_height, t);
  result.pelvis_pitch = glm::mix(a.pelvis_pitch, b.pelvis_pitch, t);
  result.pelvis_roll = glm::mix(a.pelvis_roll, b.pelvis_roll, t);
  result.spine_pitch = glm::mix(a.spine_pitch, b.spine_pitch, t);
  result.spine_yaw = glm::mix(a.spine_yaw, b.spine_yaw, t);
  result.spine_roll = glm::mix(a.spine_roll, b.spine_roll, t);
  result.neck_pitch = glm::mix(a.neck_pitch, b.neck_pitch, t);
  result.neck_yaw = glm::mix(a.neck_yaw, b.neck_yaw, t);
  result.head_pitch = glm::mix(a.head_pitch, b.head_pitch, t);
  result.head_yaw = glm::mix(a.head_yaw, b.head_yaw, t);
  result.head_roll = glm::mix(a.head_roll, b.head_roll, t);
  for (int leg = 0; leg < kLegCount; ++leg) {
    result.feet[leg] = glm::mix(a.feet[leg], b.feet[leg], t);
    result.grounded[leg] = glm::mix(a.grounded[leg], b.grounded[leg], t);
    result.toe_pitch[leg] = glm::mix(a.toe_pitch[leg], b.toe_pitch[leg], t);
  }
  result.face = FaceControls::Mix(a.face, b.face, t);
  result.sway = glm::mix(a.sway, b.sway, t);
  return result;
}

BearRig::BearRig(const animation::Skeleton& skeleton) : skeleton_(skeleton) {
  auto require = [&](const char* name) {
    int bone = skeleton.Find(name);
    if (bone < 0) {
      missing_bones_ += std::string(missing_bones_.empty() ? "" : ", ") + name;
    }
    return bone;
  };
  root_ = require("root");
  pelvis_ = require("pelvis");
  spine_ = require("spine");
  chest_ = require("chest");
  neck_ = require("neck");
  head_ = require("head");
  jaw_ = require("jaw");
  tongue_ = {require("tongue.1"), require("tongue.2"), require("tongue.3")};
  ear_left_ = require("ear.L");
  ear_right_ = require("ear.R");
  tail_ = require("tail");
  const char* const eye_bones[2][3] = {
      {"eye.L", "eyelid.upper.L", "eyelid.lower.L"},
      {"eye.R", "eyelid.upper.R", "eyelid.lower.R"}};
  for (int side = 0; side < 2; ++side) {
    eyes_[side].eye = require(eye_bones[side][0]);
    eyes_[side].upper_lid = require(eye_bones[side][1]);
    eyes_[side].lower_lid = require(eye_bones[side][2]);
  }
  const char* const leg_bones[kLegCount][3] = {
      {"upperarm.L", "forearm.L", "hand.L"},
      {"upperarm.R", "forearm.R", "hand.R"},
      {"thigh.L", "shin.L", "foot.L"},
      {"thigh.R", "shin.R", "foot.R"}};
  for (int leg = 0; leg < kLegCount; ++leg) {
    legs_[leg].upper = require(leg_bones[leg][0]);
    legs_[leg].lower = require(leg_bones[leg][1]);
    legs_[leg].paw = require(leg_bones[leg][2]);
  }
  if (!IsValid()) {
    return;
  }

  bind_ = skeleton.BindPose();
  skeleton.ModelMatrices(bind_, bind_model_);
  for (const glm::mat4& matrix : bind_model_) {
    bind_rotation_.push_back(RotationOf(matrix));
  }
  for (int leg = 0; leg < kLegCount; ++leg) {
    LegRig& rig = legs_[leg];
    glm::vec3 upper = PositionOf(bind_model_[rig.upper]),
              lower = PositionOf(bind_model_[rig.lower]),
              paw = PositionOf(bind_model_[rig.paw]);
    rig.upper_length = glm::distance(upper, lower);
    rig.lower_length = glm::distance(lower, paw);
    rig.paw_height = paw.y;
    rig.neutral = glm::vec3(paw.x, 0.0f, paw.z);
    rig.paw_rotation = bind_rotation_[rig.paw];
    rig.front = leg == kFrontLeft || leg == kFrontRight;
    rig.side = paw.x >= 0.0f ? 1.0f : -1.0f;
  }
  pelvis_bind_rotation_ = bind_rotation_[pelvis_];
  // The eyes look along their bones, the upper lids close around the axis
  // across the gaze
  for (Eye& eye : eyes_) {
    glm::vec3 gaze = glm::normalize(glm::vec3(bind_model_[eye.eye][1]));
    glm::vec3 up = glm::normalize(kUp - gaze * glm::dot(kUp, gaze));
    eye.lid_axis = glm::normalize(glm::cross(up, gaze));
  }
  tongue_direction_ = glm::normalize(PositionOf(bind_model_[tongue_[2]]) -
                                     PositionOf(bind_model_[tongue_[0]]));
  for (int i = 0; i < 3; ++i) {
    tongue_bone_distance_[i] = glm::dot(PositionOf(bind_model_[tongue_[i]]) -
                                            PositionOf(bind_model_[tongue_[0]]),
                                        tongue_direction_);
  }
  part_bones_ = {pelvis_,
                 chest_,
                 head_,
                 legs_[kFrontLeft].upper,
                 legs_[kFrontLeft].lower,
                 legs_[kFrontRight].upper,
                 legs_[kFrontRight].lower,
                 legs_[kHindLeft].upper,
                 legs_[kHindLeft].lower,
                 legs_[kHindRight].upper,
                 legs_[kHindRight].lower};
  BuildIdleAnimations();

  pose_ = bind_;
  world_ = bind_model_;
  previous_world_ = world_;
}

glm::mat4 BearRig::BodyFrame(const Controls& c) const {
  glm::mat4 frame = glm::translate(glm::mat4(1.0f), c.root_position) *
                    glm::mat4_cast(glm::angleAxis(c.yaw, kUp));
  if (c.body_roll != 0.0f) {
    frame = frame *
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kRollPivot, 0.0f)) *
            glm::mat4_cast(
                glm::angleAxis(c.body_roll, glm::vec3(0.0f, 0.0f, 1.0f))) *
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -kRollPivot, 0.0f));
  }
  return frame;
}

glm::mat4 BearRig::ParentWorld(int bone) const {
  int parent = skeleton_.bones[bone].parent;
  return parent >= 0 ? world_[parent]
                     : BodyFrame(controls_) * skeleton_.root_parent;
}

void BearRig::ForwardKinematics(int bone) {
  world_[bone] = ParentWorld(bone) * pose_[bone].ToMatrix();
}

void BearRig::RotateBone(int bone, const glm::quat& world_delta) {
  glm::quat parent = RotationOf(ParentWorld(bone));
  glm::quat current = RotationOf(world_[bone]);
  pose_[bone].rotation =
      glm::normalize(glm::inverse(parent) * world_delta * current);
  ForwardKinematics(bone);
}

glm::quat BearRig::BoneFrame(int bone) const {
  return RotationOf(world_[bone]) * glm::inverse(bind_rotation_[bone]);
}

void BearRig::UpdateSprings(int bone, animation::SpringPoint& spring,
                            float length, float stiffness, float damping,
                            float gravity, float max_angle, float time_delta) {
  glm::vec3 origin = PositionOf(world_[bone]);
  // bones point along their local y axis
  glm::vec3 direction = RotationOf(world_[bone]) * kUp;
  glm::vec3 anchor = origin + direction * length;
  if (time_delta <= 0.0f ||
      glm::distance(spring.position, anchor) > 3.0f * length) {
    spring.Reset(anchor);
    return;
  }
  spring.Update(anchor, stiffness, damping, glm::vec3(0.0f, -gravity, 0.0f),
                time_delta);
  glm::vec3 to_tip = spring.position - origin;
  if (glm::dot(to_tip, to_tip) < 1e-8f) {
    spring.Reset(anchor);
    return;
  }
  RotateBone(bone, LimitAngle(RotationBetween(direction, to_tip), max_angle));
  spring.position =
      // the tip stays at the end of the bone
      origin + glm::normalize(to_tip) * length;
}

void BearRig::ApplyFace(int bone, const FaceControls& face, float time_delta) {
  // Rotations around the axes of the model, carried along by the head (or jaw)
  auto around_model_axes = [&](int frame_bone, const glm::quat& rotation) {
    glm::quat frame = BoneFrame(frame_bone);
    return frame * rotation * glm::inverse(frame);
  };

  const float tongue_out = std::clamp(face.tongue_out, 0.0f, 1.0f);
  if (bone == jaw_) {
    // The mouth opens a bit for the tongue to come out
    float open =
        std::clamp(std::max(face.jaw_open, 0.2f * tongue_out), 0.0f, 1.0f);
    float angle = glm::radians(-kJawBindOpenDegrees - kJawPressDegrees +
                               open * (kJawOpenDegrees + kJawPressDegrees));
    // Positive around the left axis opens
    RotateBone(bone, around_model_axes(head_, glm::angleAxis(angle, kLeft)));
  } else if (bone == tongue_[0]) {
    // The tongue slides forward along the floor of the mouth, rising a bit to
    // come over the lower lip
    glm::quat frame = BoneFrame(jaw_);
    glm::vec3 position =
        PositionOf(world_[bone]) +
        frame * tongue_direction_ * (kTongueReach * tongue_out);
    pose_[bone].translation =
        glm::vec3(glm::inverse(ParentWorld(bone)) * glm::vec4(position, 1.0f));
    ForwardKinematics(bone);
    RotateBone(
        bone,
        around_model_axes(
            jaw_,
            glm::angleAxis(0.5f * face.tongue_side * tongue_out, kUp) *
                glm::angleAxis(-0.1f * Smooth(2.0f * tongue_out), kLeft)));
  } else if (bone == tongue_[1] || bone == tongue_[2]) {
    // Only the part of the tongue that is out of the mouth hangs down (and
    // curls down), inside it lies on the floor of the mouth
    int index = bone == tongue_[1] ? 1 : 2;
    float outside =
        Smooth((tongue_bone_distance_[index] + kTongueReach * tongue_out -
                kTongueLipDistance + 0.03f) /
               0.08f);
    float hang = (index == 2 ? 0.7f : 0.3f) * tongue_out * outside;
    float curl = (index == 2 ? 0.6f : 0.4f) * face.tongue_curl *
                 // curling up is always possible
                 (face.tongue_curl > 0.0f ? outside : 1.0f);
    RotateBone(bone,
               around_model_axes(bone, glm::angleAxis(hang + curl, kLeft)));
  } else if (bone == ear_left_ || bone == ear_right_) {
    RotateBone(
        bone, around_model_axes(
                  head_, glm::angleAxis(-0.9f * face.ears_back, kLeft) *
                             glm::angleAxis((bone == ear_left_ ? -0.3f : 0.3f) *
                                                face.ears_back,
                                            glm::vec3(0.0f, 0.0f, 1.0f))));
    UpdateSprings(bone, ear_springs_[bone == ear_left_ ? 0 : 1], kEarLength,
                  260.0f, 10.0f, 2.0f, 0.8f, time_delta);
  } else {
    for (const Eye& eye : eyes_) {
      float closed = std::clamp(face.lids, 0.0f, 1.0f);
      if (bone == eye.eye) {
        RotateBone(bone,
                   around_model_axes(head_, BodyRotation(face.gaze_pitch,
                                                         face.gaze_yaw, 0.0f)));
      } else if (bone == eye.upper_lid) {
        // The upper lid follows the eye when it looks up or down
        float angle =
            closed * kUpperLidClose - 0.7f * face.gaze_pitch * (1.0f - closed);
        RotateBone(bone, around_model_axes(
                             head_, glm::angleAxis(angle, eye.lid_axis)));
      } else if (bone == eye.lower_lid) {
        float angle =
            closed * kLowerLidClose + 0.3f * face.gaze_pitch * (1.0f - closed);
        RotateBone(bone, around_model_axes(
                             head_, glm::angleAxis(angle, -eye.lid_axis)));
      }
    }
  }
}

void BearRig::Evaluate(const Pose& base, const Controls& c, float time_delta) {
  controls_ = c;
  pose_ = base;
  world_.resize(pose_.size());
  glm::quat body = RotationOf(BodyFrame(c));
  auto in_body = [&](float pitch, float yaw, float roll) {
    return body * BodyRotation(pitch, yaw, roll) * glm::inverse(body);
  };

  for (int i = 0; i < static_cast<int>(pose_.size()); ++i) {
    ForwardKinematics(i);
    if (i == pelvis_) {
      glm::vec3 position = PositionOf(world_[i]) + body * c.pelvis_offset;
      pose_[i].translation =
          glm::vec3(glm::inverse(ParentWorld(i)) * glm::vec4(position, 1.0f));
      ForwardKinematics(i);
      RotateBone(i, in_body(c.pelvis_pitch, c.pelvis_yaw, c.pelvis_roll));
    } else if (i == spine_ || i == chest_) {
      RotateBone(i, in_body(0.5f * c.spine_pitch, 0.5f * c.spine_yaw,
                            0.5f * c.spine_roll));
    } else if (i == neck_) {
      RotateBone(i, in_body(c.neck_pitch, c.neck_yaw, 0.0f));
    } else if (i == head_) {
      RotateBone(i, in_body(c.head_pitch, c.head_yaw, c.head_roll));
    } else if (i == tail_) {
      UpdateSprings(i, tail_spring_, kTailLength, 140.0f, 7.0f, 4.0f, 1.0f,
                    time_delta);
    } else {
      ApplyFace(i, c.face, time_delta);
    }
  }

  // Legs: the paws reach their targets with two bone IK, the knees bend
  // forward, the elbows backward
  glm::vec3 forward = body * glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 left = body * kLeft;
  glm::vec3 body_up = body * kUp;
  for (int leg = 0; leg < kLegCount; ++leg) {
    const LegRig& rig = legs_[leg];
    glm::vec3 normal = glm::length(c.foot_normals[leg]) > 0.5f
                           ? glm::normalize(c.foot_normals[leg])
                           : body_up;
    glm::vec3 target = c.feet[leg] + normal * rig.paw_height;
    glm::vec3 upper = PositionOf(world_[rig.upper]);
    glm::vec3 lower = PositionOf(world_[rig.lower]);
    glm::vec3 pole = lower + (rig.front ? -forward : forward) * 0.6f +
                     left * (0.1f * rig.side);
    animation::TwoBoneSolution solution = animation::SolveTwoBoneIk(
        upper, rig.upper_length, rig.lower_length, target, pole);

    RotateBone(rig.upper,
               RotationBetween(lower - upper, solution.joint - upper));
    ForwardKinematics(rig.lower);
    ForwardKinematics(rig.paw);
    glm::vec3 joint = PositionOf(world_[rig.lower]);
    RotateBone(rig.lower, RotationBetween(PositionOf(world_[rig.paw]) - joint,
                                          solution.end - joint));
    ForwardKinematics(rig.paw);

    // The paw lies flat on the ground and points where the bear goes, it curls
    // while the leg swings
    glm::quat tilt = LimitAngle(RotationBetween(body_up, normal), 0.6f);
    glm::quat toes = glm::angleAxis(c.toe_pitch[leg], left);
    glm::quat paw = toes * tilt * body * rig.paw_rotation;
    pose_[rig.paw].rotation =
        glm::normalize(glm::inverse(RotationOf(world_[rig.lower])) * paw);
    ForwardKinematics(rig.paw);
  }
}

bool BearRig::ProbeGround(const GroundProbe& probe, glm::vec3 point,
                          glm::vec3& ground, glm::vec3& normal) const {
  if (!probe ||
      !probe(point + kUp * 1.0f, point - kUp * 1.6f, ground, normal)) {
    return false;
  }
  if (normal.y < 0.2f) {
    normal = kUp;  // a wall, not a floor
  }
  return true;
}

void BearRig::PlantFeet(const GroundProbe& probe, const Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, kUp);
  for (int leg = 0; leg < kLegCount; ++leg) {
    Foot& foot = feet_[leg];
    glm::vec3 position = c.root_position + yaw * legs_[leg].neutral;
    glm::vec3 ground, normal;
    if (ProbeGround(probe, position, ground, normal)) {
      position = ground;
    } else {
      normal = kUp;
    }
    foot.position = foot.planted = foot.swing_from = position;
    foot.normal = normal;
    foot.swinging = false;
    foot.swing_progress = 0.0f;
  }
}

void BearRig::LandFeet(const GroundProbe& probe, const Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, kUp);
  for (int leg = 0; leg < kLegCount; ++leg) {
    Foot& foot = feet_[leg];
    glm::vec3 neutral = c.root_position + yaw * legs_[leg].neutral;
    glm::vec3 offset(foot.position.x - neutral.x, 0.0f,
                     foot.position.z - neutral.z);
    if (glm::length(offset) > 0.35f) {
      offset *= 0.35f / glm::length(offset);
    }
    glm::vec3 position = neutral + offset;
    glm::vec3 ground, normal;
    if (ProbeGround(probe, position, ground, normal)) {
      position = ground;
    } else {
      normal = kUp;
    }
    foot.position = foot.planted = foot.swing_from = position;
    foot.normal = normal;
    foot.swinging = false;
    foot.swing_progress = 0.0f;
  }
}

void BearRig::UpdateAirborne(float time_delta, const LocomotionInput& input,
                             const GroundProbe& probe, Controls& c) {
  const float launch_duration = 0.18f;
  glm::quat yaw = glm::angleAxis(c.yaw, kUp);
  glm::vec3 horizontal_velocity(input.velocity.x, 0.0f, input.velocity.z);
  if (airborne_time_ == 0.0f) {
    for (Foot& foot : feet_) {
      // A step that started on the ground ends where it was going, the paws
      // push off from there
      if (foot.swinging) {
        foot.swinging = false;
        foot.position = foot.planted;
      }
    }
    for (int leg = 0; leg < kLegCount; ++leg) {
      feet_[leg].swing_from = feet_[leg].position;
      air_offsets_[leg] = feet_[leg].position - input.root_position;
    }
  }
  airborne_time_ += time_delta;
  last_vertical_speed_ = input.velocity.y;
  if (launch_time_ >= 0.0f) {
    launch_time_ += time_delta;
  }
  float launch = launch_time_ >= 0.0f
                     ? 1.0f - Smooth(launch_time_ / launch_duration)
                     : 0.0f;

  // Falling: the legs reach for the ground shortly before the bear lands there
  // (where it will be in a moment)
  float fall_speed = std::max(-input.velocity.y, 0.0f);
  float target_reach = 0.0f;
  glm::vec3 ahead = input.root_position + horizontal_velocity * 0.1f;
  glm::vec3 ground, normal;
  if (fall_speed > 0.5f && probe &&
      probe(ahead + kUp * 0.5f, ahead - kUp * 12.0f, ground, normal)) {
    float time_to_land = std::max(ahead.y - ground.y, 0.0f) / fall_speed;
    target_reach = 1.0f - Smooth((time_to_land - 0.06f) / 0.3f);
  }
  reach_ += (target_reach - reach_) * SmoothingFactor(0.03f, time_delta);

  // The legs only go into the air pose after a moment in the air (not when the
  // bear runs over a bump), right away when jumping
  float weight = std::max(Smooth((airborne_time_ - kTuckDelay) / 0.12f),
                          launch_time_ >= 0.0f ? 1.0f : 0.0f);
  air_pose_ = std::max(air_pose_, weight);
  // A long fall spreads the legs
  float spread = Smooth((fall_speed - 10.0f) / 12.0f) * (1.0f - reach_);
  for (int leg = 0; leg < kLegCount; ++leg) {
    const LegRig& rig = legs_[leg];
    Foot& foot = feet_[leg];
    // Tucked in: the front paws under the chest, the hind paws under the belly.
    // Reaching: the front legs stretch forward and down to take the landing,
    // the hind legs follow a bit higher
    glm::vec3 tucked = rig.front ? glm::vec3(0.0f, 0.3f, 0.12f)
                                 : glm::vec3(0.0f, 0.28f, 0.08f);
    glm::vec3 reaching = rig.front ? glm::vec3(0.0f, -0.02f, 0.2f)
                                   : glm::vec3(0.0f, 0.06f, -0.04f);
    glm::vec3 offset = glm::mix(tucked, reaching, reach_) +
                       glm::vec3(0.1f * rig.side, 0.1f, 0.0f) * spread;
    glm::vec3 target = input.root_position + yaw * (rig.neutral + offset);
    float toes = glm::mix(0.6f, -0.1f, reach_);
    if (rig.front) {
      // Pushing off: the front legs lift and reach forward
      target += yaw * glm::vec3(0.0f, 0.05f, 0.18f) * launch;
    } else {
      // Pushing off: the hind paws stay where they left the ground until the
      // legs are stretched
      target = glm::mix(target, foot.swing_from, launch);
      toes = glm::mix(toes, 0.9f, launch);
    }
    // Smoothed relative to the body, which moves fast (in the world the paws
    // would trail behind)
    air_offsets_[leg] =
        glm::mix(air_offsets_[leg], target - input.root_position,
                 weight * SmoothingFactor(0.035f, time_delta));
    foot.position = input.root_position + air_offsets_[leg];
    foot.normal = glm::normalize(
        glm::mix(foot.normal, kUp, SmoothingFactor(0.05f, time_delta)));
    c.toe_pitch[leg] = toes * weight;
  }

  // Nose up when rising, down when falling, a bit less when the front legs
  // reach for the ground
  float pitch =
      input.velocity.y > 0.0f
          ? std::min(input.velocity.y * 0.025f, 0.3f)
          : std::max(input.velocity.y * 0.018f, -0.3f) * (1.0f - 0.4f * reach_);
  body_pitch_ += (pitch - body_pitch_) * SmoothingFactor(0.08f, time_delta);
  body_height_ += (0.0f - body_height_) * SmoothingFactor(0.1f, time_delta);
}

void BearRig::UpdateSwimming(float time_delta, const LocomotionInput& input,
                             Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, kUp);
  if (!was_swimming_) {
    // The paws start paddling from where they are
    for (int leg = 0; leg < kLegCount; ++leg) {
      feet_[leg].swinging = false;
      air_offsets_[leg] = feet_[leg].position - input.root_position;
    }
  }
  airborne_time_ = 0.0f;
  launch_time_ = -1.0f;
  reach_ = 0.0f;

  // Faster strokes and wider circles when the bear swims fast, small ones when
  // it treads water
  float speed = glm::length(glm::vec2(input.velocity.x, input.velocity.z));
  float effort =
      std::clamp(speed / 3.5f + std::abs(input.yaw_rate) * 0.25f, 0.0f, 1.0f);
  swim_effort_ += (effort - swim_effort_) * SmoothingFactor(0.2f, time_delta);
  swim_phase_ += glm::mix(1.1f, 1.8f, swim_effort_) * time_delta;
  float size = glm::mix(0.55f, 1.0f, swim_effort_);
  for (int leg = 0; leg < kLegCount; ++leg) {
    const LegRig& rig = legs_[leg];
    Foot& foot = feet_[leg];
    float phase = 2.0f * kPi * (swim_phase_ + kSwimOffsets[leg]);
    // The paw goes round: forward high, down, back under the body and up again
    // (relative to the ground point under the hip)
    glm::vec3 offset =
        rig.front ? glm::vec3(0.0f, 0.34f + 0.13f * size * std::sin(phase),
                              0.14f + 0.2f * size * std::cos(phase))
                  : glm::vec3(0.0f, 0.3f + 0.1f * size * std::sin(phase),
                              -0.08f + 0.2f * size * std::cos(phase));
    offset.x = 0.05f * rig.side;
    glm::vec3 target = input.root_position + yaw * (rig.neutral + offset);
    air_offsets_[leg] =
        glm::mix(air_offsets_[leg], target - input.root_position,
                 SmoothingFactor(was_swimming_ ? 0.03f : 0.08f, time_delta));
    foot.position = foot.planted = foot.swing_from =
        input.root_position + air_offsets_[leg];
    foot.normal = glm::normalize(
        glm::mix(foot.normal, kUp, SmoothingFactor(0.05f, time_delta)));
    // The paw pushes the water with its sole on the way back and turns on the
    // way forward
    c.toe_pitch[leg] = rig.front ? 0.35f + 0.45f * std::sin(phase)
                                 : 0.1f + 0.5f * std::sin(phase);
  }
  // Nose up, the body lies in the water
  body_pitch_ += (0.12f - body_pitch_) * SmoothingFactor(0.15f, time_delta);
  body_height_ += (0.0f - body_height_) * SmoothingFactor(0.1f, time_delta);
  was_swimming_ = true;
}

void BearRig::Reset(const LocomotionInput& input, const GroundProbe& probe) {
  if (!IsValid()) {
    return;
  }
  gait_phase_ = 0.0f;
  gallop_ = 0.0f;
  body_height_ = body_pitch_ = lean_ = bend_ = 0.0f;
  airborne_time_ = 0.0f;
  launch_time_ = -1.0f;
  air_pose_ = reach_ = 0.0f;
  landing_blend_ = 1.0f;
  swim_phase_ = swim_effort_ = 0.0f;
  was_swimming_ = false;
  squash_ = animation::SpringValue();
  pitch_kick_ = animation::SpringValue();
  look_yaw_ = look_pitch_ = look_target_yaw_ = look_target_pitch_ = 0.0f;
  gaze_yaw_ = gaze_pitch_ = gaze_target_yaw_ = gaze_target_pitch_ = 0.0f;
  blink_progress_ = -1.0f;
  idle_ = -1;
  blend_time_ = 0.0f;

  Controls c;
  c.root_position = input.root_position;
  c.yaw = std::atan2(input.direction.x, input.direction.z);
  c.pelvis_offset.y = kStandHeight;
  controls_ = c;
  PlantFeet(probe, c);
  for (int leg = 0; leg < kLegCount; ++leg) {
    c.feet[leg] = feet_[leg].position;
    c.foot_normals[leg] = feet_[leg].normal;
  }
  Evaluate(bind_, c, 0.0f);
  previous_world_ = world_;
  last_time_delta_ = 0.0f;
}

float BearRig::BlinkAmount() const {
  if (blink_progress_ < 0.0f) {
    return 0.0f;
  }
  // Closes quickly, opens a bit slower
  return blink_progress_ < 0.35f
             ? Smooth(blink_progress_ / 0.35f)
             : 1.0f - Smooth((blink_progress_ - 0.35f) / 0.65f);
}

void BearRig::LookAt(glm::vec3 target) {
  looking_ = true;
  look_target_ = target;
}

void BearRig::UpdateSecondary(float time_delta, bool moving) {
  // The head looks around when the bear stands, and ahead when it moves; at a
  // point it looks there
  look_timer_ -= time_delta;
  float target_yaw = 0.0f, target_pitch = 0.0f;
  if (looking_) {
    glm::vec3 to_target = look_target_ - GetBonePosition(head_);
    target_yaw = std::remainder(
        std::atan2(to_target.x, to_target.z) - controls_.yaw, 2.0f * kPi);
    target_pitch = std::atan2(to_target.y,
                              glm::length(glm::vec2(to_target.x, to_target.z)));
    look_target_yaw_ = std::clamp(target_yaw, -0.9f, 0.9f);
    look_target_pitch_ = std::clamp(target_pitch, -0.35f, 0.45f);
    look_timer_ = 1.0f;
  } else if (moving) {
    look_target_yaw_ = 0.0f;
    look_target_pitch_ = 0.05f;
    look_timer_ = std::min(look_timer_, 1.0f);
  } else if (look_timer_ <= 0.0f) {
    look_target_yaw_ = Uniform(random_, -0.6f, 0.6f);
    look_target_pitch_ = Uniform(random_, -0.12f, 0.25f);
    look_timer_ = Uniform(random_, 1.5f, 4.0f);
    saccade_timer_ = 0.0f;  // the eyes lead the head
  }
  look_yaw_ +=
      (look_target_yaw_ - look_yaw_) * SmoothingFactor(0.15f, time_delta);
  look_pitch_ +=
      (look_target_pitch_ - look_pitch_) * SmoothingFactor(0.15f, time_delta);

  // Saccades: the eyes jump quickly from one point to the next. Looking at a
  // point they take the rest of the turn the head has not made yet and stay on
  // it (only tiny jumps)
  saccade_timer_ -= time_delta;
  if (looking_) {
    if (saccade_timer_ <= 0.0f) {
      fixation_yaw_ = Uniform(random_, -0.02f, 0.02f);
      fixation_pitch_ = Uniform(random_, -0.015f, 0.015f);
      saccade_timer_ = Uniform(random_, 0.5f, 1.5f);
    }
    gaze_target_yaw_ = target_yaw - look_yaw_ + fixation_yaw_;
    gaze_target_pitch_ = target_pitch - look_pitch_ + fixation_pitch_;
  } else if (saccade_timer_ <= 0.0f) {
    gaze_target_yaw_ =
        0.6f * (look_target_yaw_ - look_yaw_) + Uniform(random_, -0.12f, 0.12f);
    gaze_target_pitch_ = 0.6f * (look_target_pitch_ - look_pitch_) +
                         Uniform(random_, -0.07f, 0.07f);
    saccade_timer_ = Uniform(random_, 0.4f, 2.2f);
  }
  gaze_yaw_ += (std::clamp(gaze_target_yaw_, -0.35f, 0.35f) - gaze_yaw_) *
               SmoothingFactor(0.02f, time_delta);
  gaze_pitch_ += (std::clamp(gaze_target_pitch_, -0.25f, 0.25f) - gaze_pitch_) *
                 SmoothingFactor(0.02f, time_delta);

  blink_timer_ -= time_delta;
  if (blink_progress_ < 0.0f && blink_timer_ <= 0.0f) {
    blink_progress_ = 0.0f;
  }
  if (blink_progress_ >= 0.0f) {
    blink_progress_ += time_delta / kBlinkDuration;
    if (blink_progress_ >= 1.0f) {
      blink_progress_ = -1.0f;
      // sometimes twice in a row
      blink_timer_ = Uniform(random_, 0.0f, 1.0f) < 0.2f
                         ? 0.12f
                         : Uniform(random_, 2.0f, 5.5f);
    }
  }

  ear_twitch_timer_ -= time_delta;
  if (ear_twitch_timer_ <= 0.0f) {
    if (!moving) {
      glm::vec3 flick(Uniform(random_, -1.0f, 1.0f),
                      Uniform(random_, 0.0f, 0.5f),
                      Uniform(random_, -1.0f, 1.0f));
      ear_springs_[random_() % 2].velocity += flick * 3.0f;
    }
    ear_twitch_timer_ = Uniform(random_, 3.0f, 7.0f);
  }
}

void BearRig::UpdateLocomotion(float time_delta, const LocomotionInput& input,
                               const GroundProbe& probe) {
  if (!IsValid() || time_delta <= 0.0f) {
    return;
  }
  previous_world_ = world_;
  last_time_delta_ = time_delta;
  time_ += time_delta;

  Controls c;
  if (idle_ >= 0) {
    UpdateSecondary(time_delta, false);
    c = IdleControls(input, probe);
    idle_time_ += time_delta;
    if (idle_ == kIdleLie) {
      idle_time_ = std::fmod(idle_time_,
                             GetIdleDuration(kIdleLie));  // lies until StopIdle
    } else if (idle_time_ >= GetIdleDuration(static_cast<Idle>(idle_))) {
      // The animation ends standing: walking starts from where the feet are
      idle_ = -1;
      for (int leg = 0; leg < kLegCount; ++leg) {
        feet_[leg].position = feet_[leg].planted = feet_[leg].swing_from =
            c.feet[leg];
        feet_[leg].normal = c.foot_normals[leg];
        feet_[leg].swinging = false;
      }
      gait_phase_ = 0.0f;
    }
  } else {
    c.root_position = input.root_position;
    glm::vec3 forward =
        glm::normalize(glm::vec3(input.direction.x, 0.0f, input.direction.z));
    c.yaw = std::atan2(forward.x, forward.z);
    glm::quat yaw = glm::angleAxis(c.yaw, kUp);
    glm::vec3 horizontal_velocity(input.velocity.x, 0.0f, input.velocity.z);
    float speed = glm::length(horizontal_velocity);
    float forward_speed = glm::dot(horizontal_velocity, forward);
    // Turning on the spot also needs steps
    float effort = speed + std::abs(input.yaw_rate) * 0.9f;
    float moving = std::clamp(effort / 2.0f, 0.0f, 1.0f);

    gallop_ += (std::clamp((speed - 3.0f) / 6.0f, 0.0f, 1.0f) - gallop_) *
               SmoothingFactor(0.06f, time_delta);
    // steps per second of every leg
    const float frequency = glm::mix(1.8f, 3.6f, gallop_);
    // Part of the cycle a foot is on the ground: never longer than the leg can
    // follow the body (e.g. while speeding up)
    float duty = glm::mix(0.6f, 0.3f, gallop_);
    if (speed > 0.01f) {
      duty = std::clamp(kMaxStanceTravel * frequency / speed, 0.15f, duty);
    }
    const float swing_duration = (1.0f - duty) / frequency;
    const float stance_duration = duty / frequency;

    if (input.jumped) {
      launch_time_ = 0.0f;
      pitch_kick_.velocity += 1.2f;
    }
    if (input.swimming) {
      UpdateSwimming(time_delta, input, c);
    } else if (input.grounded && !input.jumped) {
      if (airborne_time_ > kTuckDelay || launch_time_ >= 0.0f ||
          was_swimming_) {
        // Landing: the paws are on the ground again, the legs give way (the
        // harder the landing, the more), the front legs take the impact first
        float impact = std::max(-last_vertical_speed_, 0.0f);
        squash_.velocity -= std::min(impact * 0.1f, 2.0f);
        pitch_kick_.velocity -= std::min(impact * 0.07f, 1.4f);
        LandFeet(probe, c);
        landing_blend_ = 0.0f;
      }
      airborne_time_ = 0.0f;
      launch_time_ = -1.0f;
      reach_ = 0.0f;

      bool any_swinging = false, any_away = false;
      for (int leg = 0; leg < kLegCount; ++leg) {
        glm::vec3 neutral = input.root_position + yaw * legs_[leg].neutral;
        glm::vec2 away(feet_[leg].planted.x - neutral.x,
                       feet_[leg].planted.z - neutral.z);
        any_swinging = any_swinging || feet_[leg].swinging;
        any_away = any_away || glm::length(away) > 0.3f;
      }
      // Standing still: the feet finish their steps and then stay where they
      // are
      bool stepping = effort > 0.2f || any_swinging || any_away;
      if (stepping) {
        gait_phase_ += frequency * time_delta;
      }

      // Right after landing the paws go smoothly from where they landed into
      // the steps (which may be under way)
      float landing_follow = glm::mix(SmoothingFactor(0.04f, time_delta), 1.0f,
                                      Smooth(landing_blend_));
      landing_blend_ = std::min(1.0f, landing_blend_ + time_delta / 0.2f);
      for (int leg = 0; leg < kLegCount; ++leg) {
        Foot& foot = feet_[leg];
        glm::vec3 previous = foot.position;
        float phase =
            Fract(gait_phase_ -
                  glm::mix(kWalkOffsets[leg], kBoundOffsets[leg], gallop_));
        if (stepping && phase >= duty) {
          float progress = (phase - duty) / (1.0f - duty);
          if (!foot.swinging) {
            foot.swinging = true;
            foot.swing_from = foot.planted;
          }
          foot.swing_progress = progress;
          // Where the hip will be when the foot lands, and half a stance
          // further, so the foot passes under the hip
          float time_to_land = (1.0f - progress) * swing_duration;
          glm::quat landing_yaw =
              glm::angleAxis(c.yaw + input.yaw_rate * time_to_land, kUp);
          glm::vec3 reach = horizontal_velocity * (0.5f * stance_duration);
          if (glm::length(reach) > 0.45f) {
            reach *= 0.45f / glm::length(reach);
          }
          glm::vec3 landing = input.root_position +
                              horizontal_velocity * time_to_land +
                              landing_yaw * legs_[leg].neutral + reach;
          glm::vec3 ground, normal;
          if (ProbeGround(probe, landing, ground, normal)) {
            landing = ground;
          } else {
            landing.y = input.root_position.y;
            normal = kUp;
          }
          float eased = Smooth(progress);
          float lift = glm::mix(0.12f, 0.24f, gallop_) *
                       std::clamp(effort / 2.5f, 0.35f, 1.0f);
          foot.position = glm::mix(foot.swing_from, landing, eased) +
                          kUp * (lift * std::sin(kPi * progress));
          foot.normal = glm::normalize(glm::mix(foot.normal, normal, eased));
          foot.planted = landing;
          c.toe_pitch[leg] = 0.7f * std::sin(kPi * progress);
        } else {
          foot.swinging = false;
          foot.swing_progress = 0.0f;
          foot.position = foot.planted;
        }
        foot.position = glm::mix(previous, foot.position, landing_follow);
      }

      // The body follows the ground under the feet: height over the hind legs,
      // slope from the hind to the front legs
      float hind_ground =
          0.5f * (feet_[kHindLeft].planted.y + feet_[kHindRight].planted.y) -
          input.root_position.y;
      float front_ground =
          0.5f * (feet_[kFrontLeft].planted.y + feet_[kFrontRight].planted.y) -
          input.root_position.y;
      float length = legs_[kFrontLeft].neutral.z - legs_[kHindLeft].neutral.z;
      body_height_ += (std::clamp(hind_ground, -0.5f, 0.5f) - body_height_) *
                      SmoothingFactor(0.06f, time_delta);
      body_pitch_ += (std::clamp(std::atan2(front_ground - hind_ground, length),
                                 -0.5f, 0.5f) -
                      body_pitch_) *
                     SmoothingFactor(0.08f, time_delta);
    } else {
      UpdateAirborne(time_delta, input, probe, c);
    }
    bool swimming = input.swimming;
    was_swimming_ = swimming;
    for (int leg = 0; leg < kLegCount; ++leg) {
      c.feet[leg] = feet_[leg].position;
      c.foot_normals[leg] = feet_[leg].normal;
    }
    squash_.Update(0.0f, 170.0f, 13.0f, time_delta);
    pitch_kick_.Update(0.0f, 110.0f, 12.0f, time_delta);
    if ((input.grounded && !input.jumped) || swimming) {
      air_pose_ -= std::min(air_pose_, time_delta / 0.25f);
    }

    // Bobbing with the steps: twice per cycle when walking, rocking back and
    // forth when bounding
    float cycle = 2.0f * kPi * gait_phase_;
    float walking = moving * (1.0f - gallop_), bounding = moving * gallop_;
    float breathing =
        0.02f * std::sin(2.0f * kPi * 0.33f * time_) * (1.0f - moving);
    c.pelvis_offset.y = kStandHeight + body_height_ + squash_.value -
                        0.022f * std::cos(2.0f * cycle) * walking +
                        0.07f * std::sin(cycle - 0.9f) * bounding;
    c.pelvis_pitch =
        body_pitch_ + pitch_kick_.value + 0.08f * std::sin(cycle) * bounding;
    // Leaning into turns, the spine bends into them
    lean_ +=
        (std::clamp(-input.yaw_rate * forward_speed * 0.03f, -0.28f, 0.28f) -
         lean_) *
        SmoothingFactor(0.12f, time_delta);
    bend_ += (std::clamp(input.yaw_rate * 0.22f, -0.35f, 0.35f) - bend_) *
             SmoothingFactor(0.12f, time_delta);
    c.pelvis_roll = lean_ + 0.03f * std::sin(cycle) * walking;
    c.spine_yaw = bend_;
    c.spine_pitch = 0.16f * std::sin(cycle + 0.4f) * bounding +
                    0.8f * squash_.value + breathing;
    c.neck_yaw = 0.5f * bend_;
    if (swimming) {
      // The body bobs and rocks a little with the strokes
      float stroke = 2.0f * kPi * swim_phase_;
      c.pelvis_offset.y =
          kStandHeight + body_height_ + 0.025f * std::sin(2.0f * stroke);
      c.pelvis_pitch = body_pitch_ + 0.03f * std::sin(2.0f * stroke + 1.0f);
      c.pelvis_roll = lean_ + 0.05f * std::sin(stroke) * swim_effort_;
      c.spine_pitch = 0.03f * std::sin(2.0f * stroke);
    }

    UpdateSecondary(time_delta, moving > 0.1f);
    // The head stays level and looks around, running flattens the ears a bit
    c.neck_pitch = -0.4f * c.pelvis_pitch;
    // In the air the head and the eyes look down to where the bear lands, the
    // ears fold back and the mouth opens a bit
    c.head_pitch = look_pitch_ - 0.3f * c.pelvis_pitch - 0.5f * c.spine_pitch -
                   0.25f * reach_ * air_pose_;
    c.head_yaw = look_yaw_ + 0.4f * bend_;
    c.face.gaze_yaw = gaze_yaw_ + 0.3f * bend_;
    c.face.gaze_pitch = gaze_pitch_ - 0.15f * reach_ * air_pose_;
    c.face.lids = BlinkAmount() * (1.0f - air_pose_);
    c.face.ears_back = std::max(0.35f * bounding, 0.5f * air_pose_);
    c.face.jaw_open = std::max({0.12f * bounding, 0.2f * air_pose_,
                                carrying_ ? 0.34f : 0.0f});  // panting a little
    if (swimming) {
      // The head is held up out of the water, the ears flat against the
      // splashes
      c.neck_pitch += 0.25f;
      c.head_pitch += 0.12f;
      c.face.ears_back = 0.45f;
    }
  }

  c.face.jaw_open = std::max(c.face.jaw_open, mouth_open_);
  Evaluate(bind_, c, time_delta);
  // Blending from the previous animation (e.g. sitting when the player
  // interrupts an idle animation)
  if (blend_time_ > 0.0f && blend_snapshot_.size() == pose_.size()) {
    float weight = Smooth(1.0f - blend_time_ / kBlendTime);
    for (std::size_t i = 0; i < pose_.size(); ++i) {
      pose_[i] = Transform::Mix(blend_snapshot_[i], pose_[i], weight);
      ForwardKinematics(static_cast<int>(i));
    }
    blend_time_ -= time_delta;
  }
}

void BearRig::StartIdle(Idle animation) {
  if (!IsValid()) {
    return;
  }
  idle_ = animation;
  idle_time_ = 0.0f;
  blend_snapshot_ = pose_;
  blend_time_ = kBlendTime;
}

void BearRig::StopIdle() {
  if (idle_ < 0) {
    return;
  }
  idle_ = -1;
  blend_snapshot_ = pose_;
  blend_time_ = kBlendTime;
  // The feet step from where they are in the animation (the paws in the air
  // land on the next step)
  for (int leg = 0; leg < kLegCount; ++leg) {
    feet_[leg].position = feet_[leg].planted = feet_[leg].swing_from =
        controls_.feet[leg];
    feet_[leg].swinging = false;
  }
}

float BearRig::GetIdleDuration(Idle animation) const {
  const std::vector<IdleKey>& keys = idle_animations_[animation];
  return keys.empty() ? 0.0f : keys.back().time;
}

BearRig::IdleKey BearRig::StandingKey() const {
  IdleKey key;
  key.pelvis_height = kStandHeight;
  for (int leg = 0; leg < kLegCount; ++leg) {
    key.feet[leg] = legs_[leg].neutral;
    key.grounded[leg] = 1.0f;
  }
  return key;
}

BearRig::IdleKey BearRig::SittingKey() const {
  // Sitting on its rump like a person, the hind legs in front, the front paws
  // resting on the belly
  IdleKey key = StandingKey();
  key.pelvis_height = -0.56f;
  key.pelvis_pitch = 1.1f;
  key.spine_pitch = 0.25f;
  key.neck_pitch = -0.6f;
  key.head_pitch = -0.5f;
  key.face.jaw_open = 0.07f;  // relaxed, the teeth show a little
  for (int leg = 0; leg < kLegCount; ++leg) {
    float side = legs_[leg].side;
    if (legs_[leg].front) {
      key.feet[leg] = glm::vec3(0.28f * side, 0.62f, 0.08f);
      key.grounded[leg] = 0.0f;
      key.toe_pitch[leg] = 0.5f;
    } else {
      key.feet[leg] = glm::vec3(0.36f * side, 0.0f, 0.24f);
      key.toe_pitch[leg] = -0.5f;
    }
  }
  return key;
}

BearRig::IdleKey BearRig::LyingKey() const {
  // On its belly like a sphinx, the front paws stretched out in front, the hind
  // legs folded under the body, the head up
  IdleKey key = StandingKey();
  key.pelvis_height = -0.38f;
  key.pelvis_pitch = -0.03f;
  key.spine_pitch = 0.06f;
  key.neck_pitch = 0.3f;
  key.head_pitch = -0.08f;
  key.face.lids = 0.2f;  // relaxed
  for (int leg = 0; leg < kLegCount; ++leg) {
    float side = legs_[leg].side;
    if (legs_[leg].front) {
      key.feet[leg] = legs_[leg].neutral + glm::vec3(0.03f * side, 0.0f, 0.38f);
      key.toe_pitch[leg] = 0.15f;
    } else {
      key.feet[leg] =
          legs_[leg].neutral + glm::vec3(0.12f * side, 0.0f, -0.08f);
      key.toe_pitch[leg] = -0.35f;
    }
  }
  return key;
}

void BearRig::BuildIdleAnimations() {
  const IdleKey standing = StandingKey();
  const IdleKey sitting = SittingKey();
  const IdleKey lying = LyingKey();
  auto at = [](IdleKey key, float time) {
    key.time = time;
    return key;
  };

  // Stretching (like a bear in the wild): sits down, pulls its left hind paw to
  // its chest with both front paws, looks up contentedly and rocks a bit, then
  // sticks out its tongue (with its head tilted), lets go and gets up again
  {
    std::vector<IdleKey>& keys = idle_animations_[kIdleStretch];
    keys.push_back(at(standing, 0.0f));
    keys.push_back(at(sitting, 0.9f));
    IdleKey reach = sitting;
    reach.head_pitch = -0.85f;  // looks at its paw
    reach.feet[kHindLeft] = glm::vec3(0.3f, 0.32f, 0.3f);
    reach.grounded[kHindLeft] = 0.0f;
    reach.toe_pitch[kHindLeft] = -0.9f;
    reach.feet[kFrontLeft] = glm::vec3(0.26f, 0.75f, 0.2f);
    reach.feet[kFrontRight] = glm::vec3(-0.1f, 0.75f, 0.22f);
    keys.push_back(at(reach, 1.6f));
    IdleKey hold = reach;
    hold.feet[kHindLeft] = glm::vec3(0.15f, 0.92f, 0.16f);
    hold.toe_pitch[kHindLeft] = -1.4f;  // the sole faces forward
    hold.feet[kFrontLeft] = glm::vec3(0.25f, 0.98f, 0.2f);
    hold.feet[kFrontRight] = glm::vec3(0.04f, 0.95f, 0.22f);
    hold.toe_pitch[kFrontLeft] = hold.toe_pitch[kFrontRight] = 0.7f;
    hold.neck_yaw = 0.25f;
    hold.head_yaw = 0.45f;
    hold.head_pitch = -0.35f;  // looks up to its left
    hold.face.lids = 0.45f;
    hold.face.gaze_yaw = 0.15f;
    keys.push_back(at(hold, 2.5f));
    IdleKey content = hold;
    content.head_yaw = 0.6f;
    content.head_pitch = -0.25f;
    content.face.lids = 0.75f;
    content.sway = 0.07f;
    content.spine_roll = 0.05f;
    keys.push_back(at(content, 4.3f));
    IdleKey goofy = content;
    goofy.head_yaw = 0.1f;
    goofy.head_roll = -0.34f;
    goofy.head_pitch = -0.45f;
    goofy.face.jaw_open = 0.22f;
    goofy.face.tongue_out = 1.0f;
    goofy.face.tongue_side = 0.35f;
    goofy.face.tongue_curl = 0.45f;
    goofy.face.lids = 0.25f;
    goofy.face.gaze_yaw = -0.1f;
    goofy.sway = 0.12f;
    keys.push_back(at(goofy, 5.0f));
    goofy.head_roll = -0.4f;
    goofy.sway = 0.14f;
    keys.push_back(at(goofy, 6.6f));
    IdleKey tongue_in = hold;
    tongue_in.head_yaw = 0.0f;
    tongue_in.head_pitch = -0.5f;
    tongue_in.neck_yaw = 0.0f;
    tongue_in.face = FaceControls();
    tongue_in.face.lids = 0.15f;
    keys.push_back(at(tongue_in, 7.2f));
    IdleKey release = sitting;
    release.feet[kFrontLeft] = glm::vec3(0.3f, 0.62f, 0.14f);
    release.feet[kFrontRight] = glm::vec3(-0.3f, 0.62f, 0.14f);
    keys.push_back(at(release, 8.0f));
    keys.push_back(at(sitting, 8.8f));
    keys.push_back(at(standing, 9.7f));
  }

  // Yawning: sits down, takes a breath, opens its mouth wide with closed eyes
  // and the ears back, smacks its lips and looks around
  {
    std::vector<IdleKey>& keys = idle_animations_[kIdleYawn];
    keys.push_back(at(standing, 0.0f));
    keys.push_back(at(sitting, 0.9f));
    IdleKey inhale = sitting;
    inhale.head_pitch = -0.35f;
    inhale.neck_pitch = -0.5f;
    inhale.spine_pitch = 0.35f;
    inhale.face.jaw_open = 0.3f;
    inhale.face.lids = 0.45f;
    inhale.face.ears_back = 0.35f;
    keys.push_back(at(inhale, 1.7f));
    IdleKey yawn = inhale;
    yawn.head_pitch = -0.25f;  // head back
    yawn.spine_pitch = 0.4f;
    yawn.face.jaw_open = 1.0f;
    yawn.face.lids = 1.0f;
    yawn.face.ears_back = 0.9f;
    yawn.face.tongue_curl = -0.9f;
    yawn.face.tongue_out = 0.2f;
    for (int leg : {kFrontLeft, kFrontRight}) {
      yawn.feet[leg] = glm::vec3(0.34f * legs_[leg].side, 0.72f,
                                 0.28f);  // the front paws stretch
      yawn.toe_pitch[leg] = 0.9f;
    }
    keys.push_back(at(yawn, 2.6f));
    yawn.head_roll = 0.12f;
    keys.push_back(at(yawn, 3.4f));
    IdleKey close = sitting;
    close.head_pitch = -0.45f;
    close.face.jaw_open = 0.12f;
    close.face.lids = 0.7f;
    close.face.ears_back = 0.25f;
    keys.push_back(at(close, 4.0f));
    close.face.jaw_open = 0.0f;
    keys.push_back(at(close, 4.2f));
    close.face.jaw_open = 0.1f;
    close.face.tongue_out = 0.12f;  // licks its lips
    keys.push_back(at(close, 4.4f));
    close.face.jaw_open = 0.0f;
    close.face.tongue_out = 0.0f;
    close.face.lids = 0.3f;
    keys.push_back(at(close, 4.65f));
    IdleKey look_left = sitting;
    look_left.head_yaw = -0.45f;
    look_left.face.lids = 0.2f;
    look_left.face.gaze_yaw = -0.2f;
    keys.push_back(at(look_left, 5.6f));
    IdleKey look_right = sitting;
    look_right.head_yaw = 0.25f;
    look_right.face.gaze_yaw = 0.15f;
    keys.push_back(at(look_right, 6.6f));
    keys.push_back(at(standing, 7.5f));
  }

  // Eating a salmon: sits down and takes the fish from its mouth into its front
  // paws, looks at it, takes three big bites (chewing happily with closed eyes
  // and rocking in between, the last one with the head back), licks its lips
  // and gets up
  {
    std::vector<IdleKey>& keys = idle_animations_[kIdleEat];
    IdleKey carry = standing;
    carry.face.jaw_open = 0.34f;  // the fish is still in its mouth
    keys.push_back(at(carry, 0.0f));
    IdleKey hold = sitting;
    hold.head_pitch = -0.95f;  // looks at the fish
    hold.neck_pitch = -0.6f;
    hold.face.gaze_pitch = -0.15f;
    hold.face.ears_back = 0.0f;
    for (int leg : {kFrontLeft, kFrontRight}) {
      hold.feet[leg] =
          glm::vec3(0.12f * legs_[leg].side, 1.0f,
                    0.42f);  // the paws hold the fish in front of the chest
      hold.toe_pitch[leg] = 0.9f;
    }
    IdleKey take = hold;
    take.head_pitch = -1.05f;
    take.face.jaw_open = 0.34f;
    keys.push_back(at(take, kEatTakeStart));
    take.face.jaw_open = 0.1f;
    keys.push_back(at(take, kEatTakeEnd));
    IdleKey look = hold;
    look.head_roll = 0.18f;  // tilts its head: yummy
    look.face.gaze_yaw = 0.05f;
    keys.push_back(at(look, 1.05f));

    for (int bite = 0; bite < 3; ++bite) {
      float time = kEatBites[bite];
      bool last = bite == 2;
      IdleKey open = hold;
      // the last bite with the head back, like gulping it down
      open.head_pitch = last ? -0.2f : -0.55f;
      open.neck_pitch = last ? -0.65f : -0.25f;
      open.face.jaw_open = last ? 0.85f : 0.65f;
      open.face.ears_back = 0.3f;
      for (int leg : {kFrontLeft, kFrontRight}) {
        open.feet[leg] = glm::vec3(0.1f * legs_[leg].side, last ? 1.25f : 1.12f,
                                   0.4f);  // lifts the fish to its mouth
      }
      keys.push_back(at(open, time - 0.18f));
      IdleKey chomp = open;
      chomp.face.jaw_open = 0.0f;
      chomp.face.lids = 0.7f;
      chomp.head_pitch += 0.08f;
      keys.push_back(at(chomp, time));
      // Chewing with closed eyes, rocking happily
      IdleKey chew = hold;
      chew.face.lids = 0.85f;
      chew.face.ears_back = 0.0f;
      chew.sway = 0.08f;
      chew.head_pitch = -0.8f;
      float chew_end = last ? time + 0.85f : kEatBites[bite + 1] - 0.3f;
      int chews = 0;
      for (float t = time + 0.14f; t < chew_end - 0.05f; t += 0.14f) {
        chew.face.jaw_open = chews++ % 2 == 0 ? 0.17f : 0.02f;
        chew.head_roll = chews % 4 < 2 ? 0.1f : -0.06f;
        keys.push_back(at(chew, t));
      }
      chew.face.jaw_open = 0.0f;
      keys.push_back(at(chew, chew_end));
    }
    // Licks its lips, left and right, with the paws on its belly
    IdleKey lick = sitting;
    lick.head_pitch = -0.35f;
    lick.face.lids = 0.6f;
    lick.face.jaw_open = 0.14f;
    lick.face.tongue_out = 0.4f;
    lick.face.tongue_side = 0.7f;
    lick.face.tongue_curl = -0.4f;
    lick.sway = 0.06f;
    float lick_start = kEatBites[2] + 1.0f;
    keys.push_back(at(lick, lick_start + 0.2f));
    lick.face.tongue_side = -0.7f;
    keys.push_back(at(lick, lick_start + 0.55f));
    IdleKey content = sitting;
    content.face.lids = 0.9f;  // blissful
    content.head_pitch = -0.3f;
    content.head_roll = -0.12f;
    content.sway = 0.1f;
    keys.push_back(at(content, lick_start + 0.85f));
    keys.push_back(at(content, lick_start + 1.25f));
    keys.push_back(at(sitting, lick_start + 1.6f));
    keys.push_back(at(standing, lick_start + 2.3f));
  }

  // Lying (a loop): looks around with its head up, then rests its chin on the
  // ground between its paws for a while with sleepy eyes and the ears relaxed,
  // sighs and lifts its head again
  {
    std::vector<IdleKey>& keys = idle_animations_[kIdleLie];
    keys.push_back(at(lying, 0.0f));
    keys.push_back(at(lying, 5.0f));
    IdleKey chin = lying;
    chin.neck_pitch = -0.1f;
    chin.head_pitch = -0.12f;
    chin.spine_pitch = 0.0f;
    chin.face.lids = 0.6f;
    chin.face.ears_back = 0.2f;
    keys.push_back(at(chin, 6.2f));
    chin.face.lids = 0.75f;
    chin.sway = 0.02f;
    keys.push_back(at(chin, 9.5f));
    IdleKey sigh = chin;
    sigh.spine_pitch = 0.08f;  // a deep breath
    sigh.face.jaw_open = 0.08f;
    sigh.face.lids = 0.5f;
    keys.push_back(at(sigh, 10.3f));
    keys.push_back(at(lying, 11.4f));
    keys.push_back(at(lying, 14.0f));
  }
}

BearRig::Controls BearRig::IdleControls(const LocomotionInput& input,
                                        const GroundProbe& probe) {
  const std::vector<IdleKey>& keys = idle_animations_[idle_];
  std::size_t next = 1;
  while (next + 1 < keys.size() && keys[next].time <= idle_time_) {
    ++next;
  }
  const IdleKey& from = keys[next - 1];
  const IdleKey& to = keys[next];
  IdleKey key = IdleKey::Mix(
      from, to,
      Smooth((idle_time_ - from.time) / std::max(to.time - from.time, 1e-3f)));
  const bool lying = idle_ == kIdleLie;
  if (lying) {
    alert_ += ((looking_ ? 1.0f : 0.0f) - alert_) *
              SmoothingFactor(0.25f, last_time_delta_);
    key = IdleKey::Mix(key, keys.front(), alert_);
  }

  Controls c;
  c.root_position = input.root_position;
  c.yaw = std::atan2(input.direction.x, input.direction.z);
  float rocking = key.sway * std::sin(2.0f * kPi * 0.45f * idle_time_);
  c.pelvis_offset.y = key.pelvis_height;
  c.pelvis_pitch = key.pelvis_pitch;
  c.pelvis_roll = key.pelvis_roll + rocking;
  // breathing
  c.spine_pitch =
      key.spine_pitch + 0.02f * std::sin(2.0f * kPi * 0.33f * time_);
  c.spine_yaw = key.spine_yaw;
  c.spine_roll = key.spine_roll - 0.6f * rocking;
  c.neck_pitch = key.neck_pitch;
  c.neck_yaw = key.neck_yaw;
  c.head_pitch = key.head_pitch;
  c.head_yaw = key.head_yaw;
  c.head_roll = key.head_roll - 0.4f * rocking;
  if (lying) {
    c.neck_yaw += 0.35f * look_yaw_;
    c.head_yaw += 0.65f * look_yaw_;
    c.head_pitch += look_pitch_;
  }
  c.face = key.face;
  c.face.lids = std::max(c.face.lids, BlinkAmount());
  c.face.gaze_yaw += 0.5f * gaze_yaw_;
  c.face.gaze_pitch += 0.5f * gaze_pitch_;

  glm::mat4 frame = BodyFrame(c);
  glm::vec3 body_up = glm::vec3(frame * glm::vec4(kUp, 0.0f));
  for (int leg = 0; leg < kLegCount; ++leg) {
    glm::vec3 foot = glm::vec3(frame * glm::vec4(key.feet[leg], 1.0f));
    glm::vec3 normal = body_up;
    glm::vec3 ground, ground_normal;
    if (key.grounded[leg] > 0.0f &&
        ProbeGround(probe, foot, ground, ground_normal)) {
      foot = glm::mix(foot, ground, key.grounded[leg]);
      normal =
          glm::normalize(glm::mix(body_up, ground_normal, key.grounded[leg]));
    }
    c.feet[leg] = foot;
    c.foot_normals[leg] = normal;
    c.toe_pitch[leg] = key.toe_pitch[leg];
  }
  return c;
}

std::vector<physics::RagdollPart> BearRig::RagdollParts() const {
  std::vector<physics::RagdollPart> parts(kPartCount);
  if (!IsValid()) {
    return {};
  }
  auto position = [&](int bone) { return PositionOf(bind_model_[bone]); };
  auto set_part = [&](int index, int parent, glm::vec3 start, glm::vec3 end,
                      float radius, float mass) {
    physics::RagdollPart& part = parts[index];
    part.parent = parent;
    part.bind_transform = Rigid(bind_model_[part_bones_[index]]);
    part.capsule_start = start;
    part.capsule_end = end;
    part.radius = radius;
    part.mass = mass;
  };
  auto ball_joint = [&](int index, glm::vec3 position, glm::vec3 along,
                        glm::vec3 plane, float swing, float twist) {
    physics::RagdollJoint& joint = parts[index].joint;
    joint.type = physics::RagdollJoint::kSwingTwist;
    joint.position = position;
    joint.twist_axis = along;
    joint.plane_axis = plane;
    joint.swing_angle = swing;
    joint.twist_angle = twist;
    joint.friction = 2.0f;
  };
  // Elbows bend so that the paw goes forward, knees so that the foot goes
  // backward (rotation around the left axis +X)
  auto hinge_joint = [&](int index, glm::vec3 position, glm::vec3 along,
                         float min_angle, float max_angle) {
    physics::RagdollJoint& joint = parts[index].joint;
    joint.type = physics::RagdollJoint::kHinge;
    joint.position = position;
    joint.twist_axis = kLeft;
    joint.plane_axis = along;
    joint.min_angle = min_angle;
    joint.max_angle = max_angle;
    joint.friction = 0.5f;
  };
  const glm::vec3 forward(0.0f, 0.0f, 1.0f);

  glm::vec3 spine_position = position(spine_),
            chest_position = position(chest_), neck_position = position(neck_),
            head_position = position(head_);
  set_part(kPartPelvis, -1, position(pelvis_) + glm::vec3(0.0f, -0.04f, -0.08f),
           spine_position, 0.45f, 9.0f);
  set_part(kPartChest, kPartPelvis, chest_position, neck_position, 0.44f, 9.0f);
  ball_joint(kPartChest, 0.5f * (spine_position + chest_position),
             glm::normalize(chest_position - spine_position), kUp, 0.45f, 0.3f);
  set_part(kPartHead, kPartChest, head_position + glm::vec3(0.0f, 0.18f, 0.1f),
           head_position + glm::vec3(0.0f, 0.06f, 0.42f), 0.4f, 5.0f);
  ball_joint(kPartHead, 0.5f * (neck_position + head_position),
             glm::normalize(head_position - neck_position), kUp, 0.7f, 0.5f);

  const int upper_parts[4] = {kPartUpperarmL, kPartUpperarmR, kPartThighL,
                              kPartThighR};
  for (int leg = 0; leg < kLegCount; ++leg) {
    const LegRig& rig = legs_[leg];
    int upper_part = upper_parts[leg], lower_part = upper_part + 1;
    glm::vec3 upper = position(rig.upper), lower = position(rig.lower),
              paw = position(rig.paw);
    glm::vec3 toes = paw + glm::vec3(0.0f, -0.03f, rig.front ? 0.1f : 0.12f);
    set_part(upper_part, rig.front ? kPartChest : kPartPelvis, upper, lower,
             rig.front ? 0.17f : 0.2f, rig.front ? 1.6f : 2.2f);
    ball_joint(upper_part, upper, glm::normalize(lower - upper), forward, 0.9f,
               0.3f);
    set_part(lower_part, upper_part, lower, toes, rig.front ? 0.15f : 0.14f,
             rig.front ? 1.2f : 1.3f);
    if (rig.front) {
      hinge_joint(lower_part, lower, glm::normalize(paw - lower), -1.6f, 0.2f);
    } else {
      hinge_joint(lower_part, lower, glm::normalize(paw - lower), -0.2f, 1.6f);
    }
  }
  return parts;
}

void BearRig::RagdollTransforms(std::vector<glm::mat4>& transforms) const {
  transforms.resize(kPartCount);
  for (int i = 0; i < kPartCount; ++i) {
    transforms[i] = Rigid(world_[part_bones_[i]]);
  }
}

void BearRig::RagdollVelocities(std::vector<glm::vec3>& velocities) const {
  velocities.assign(kPartCount, glm::vec3(0.0f));
  if (last_time_delta_ <= 0.0f || previous_world_.size() != world_.size()) {
    return;
  }
  for (int i = 0; i < kPartCount; ++i) {
    velocities[i] = (PositionOf(world_[part_bones_[i]]) -
                     PositionOf(previous_world_[part_bones_[i]])) /
                    last_time_delta_;
  }
}

void BearRig::UpdateFromRagdoll(float time_delta,
                                const std::vector<glm::mat4>& part_transforms) {
  if (!IsValid() || part_transforms.size() != kPartCount) {
    return;
  }
  previous_world_ = world_;
  last_time_delta_ = time_delta;
  time_ += time_delta;
  idle_ = -1;
  blend_time_ = 0.0f;
  // Dazed: eyes shut, the mouth a bit open and the tongue flopping out
  FaceControls face;
  face.lids = 1.0f;
  face.jaw_open = 0.18f;
  face.tongue_out = 0.35f;
  face.tongue_curl = 0.3f;
  face.ears_back = 0.4f;
  controls_.face = face;

  // Spine and neck are between two ragdoll parts: each of their two bones turns
  // half of the way
  glm::quat pending_rotation(1.0f, 0.0f, 0.0f, 0.0f);
  auto split_between = [&](int first, int second, const glm::mat4& target) {
    glm::quat first_bind = bind_[first].rotation,
              second_bind = bind_[second].rotation;
    glm::quat at_bind =
        RotationOf(ParentWorld(first)) * first_bind * second_bind;
    glm::quat half =
        glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                   glm::inverse(at_bind) * RotationOf(target), 0.5f);
    pose_[first].rotation = glm::normalize(
        first_bind * (second_bind * half * glm::inverse(second_bind)));
    pending_rotation = glm::normalize(second_bind * half);
  };

  for (int i = 0; i < static_cast<int>(pose_.size()); ++i) {
    // The face starts from its bind pose (ApplyFace turns it from there)
    bool face_bone = i == jaw_ || i == tongue_[0] || i == tongue_[1] ||
                     i == tongue_[2] || i == ear_left_ || i == ear_right_ ||
                     i == eyes_[0].eye || i == eyes_[0].upper_lid ||
                     i == eyes_[0].lower_lid || i == eyes_[1].eye ||
                     i == eyes_[1].upper_lid || i == eyes_[1].lower_lid;
    if (face_bone) {
      pose_[i] = bind_[i];
    }
    if (i == pelvis_) {
      pose_[i] = Transform::FromMatrix(glm::inverse(ParentWorld(i)) *
                                       Rigid(part_transforms[kPartPelvis]));
    } else if (i == spine_) {
      split_between(spine_, chest_, part_transforms[kPartChest]);
    } else if (i == neck_) {
      split_between(neck_, head_, part_transforms[kPartHead]);
    } else if (i == chest_ || i == head_) {
      pose_[i].rotation = pending_rotation;
    } else {
      for (int part = kPartUpperarmL; part < kPartCount; ++part) {
        if (part_bones_[part] == i) {
          pose_[i].rotation =
              glm::normalize(glm::inverse(RotationOf(ParentWorld(i))) *
                             RotationOf(part_transforms[part]));
        }
      }
    }
    ForwardKinematics(i);
    if (i == tail_) {
      UpdateSprings(i, tail_spring_, kTailLength, 140.0f, 7.0f, 4.0f, 1.0f,
                    time_delta);
    } else if (face_bone) {
      ApplyFace(i, face, time_delta);
    }
  }
}

BearRig::GetUpStart BearRig::BeginGetUp(const GroundProbe& probe,
                                        glm::vec3 previous_direction) {
  glm::vec3 pelvis_position = PositionOf(world_[pelvis_]),
            chest_position = PositionOf(world_[chest_]);
  glm::vec3 along = chest_position - pelvis_position;
  glm::vec3 horizontal(along.x, 0.0f, along.z);
  glm::vec3 direction =
      glm::length(horizontal) > 0.3f * glm::length(along)
          ? glm::normalize(horizontal)
          : glm::normalize(
                glm::vec3(previous_direction.x, 0.0f, previous_direction.z));

  // Ground under the body: the ray starts inside of the body, so that it does
  // not hit an obstacle the bear lies against
  glm::vec3 center = 0.5f * (pelvis_position + chest_position);
  glm::vec3 ground, normal;
  glm::vec3 root_position =
      probe && probe(center + kUp * 0.1f, center - kUp * 2.5f, ground, normal)
          ? ground
          : center - kUp * 0.5f;

  // How far the body is rolled around its length axis (0: on the belly, +-pi:
  // on the back)
  glm::vec3 left = glm::cross(kUp, direction);
  glm::vec3 body_up =
      RotationOf(world_[pelvis_]) * (glm::inverse(pelvis_bind_rotation_) * kUp);
  glm::vec3 projected = body_up - direction * glm::dot(body_up, direction);
  get_up_roll_ =
      std::atan2(-glm::dot(projected, left), glm::dot(projected, kUp));
  get_up_rolls_over_ = std::abs(get_up_roll_) > 0.75f;
  get_up_time_ = 0.0f;

  Controls c;
  c.root_position = root_position;
  c.yaw = std::atan2(direction.x, direction.z);
  c.body_roll = get_up_roll_;
  c.face = controls_.face;
  controls_ = c;
  // The root bone was still where the bear ran before it fell: it moves to
  // where it gets up, so that only the bones below it carry the pose of the
  // ragdoll (blending a far away root would sweep the body through the ground)
  pose_[root_] = bind_[root_];
  ForwardKinematics(root_);
  get_up_snapshot_ = skeleton_.LocalPose(world_, BodyFrame(c));
  return {root_position, direction};
}

namespace {
const float kSettleTime = 0.35f;
const float kRollTime = 0.6f;
const float kPushTime = 0.4f;
const float kStandTime = 0.35f;
const float kShakeTime = 0.75f;
}  // namespace

float BearRig::GetGetUpDuration() const {
  return kSettleTime + (get_up_rolls_over_ ? kRollTime : 0.0f) + kPushTime +
         kStandTime + kShakeTime;
}

bool BearRig::UpdateGetUp(float time_delta, const GroundProbe& probe,
                          glm::vec3 root_position) {
  if (!IsValid()) {
    return true;
  }
  previous_world_ = world_;
  last_time_delta_ = time_delta;
  time_ += time_delta;
  get_up_time_ += time_delta;
  UpdateSecondary(time_delta, false);

  // Key poses, the feet relative to the body (model space offsets from where
  // they are in the bind pose)
  struct Key {
    float pelvis_height, pelvis_pitch, neck_pitch, head_pitch;
    glm::vec3 front, hind;
    FaceControls face;
  };
  FaceControls dazed;
  dazed.lids = 0.7f;
  dazed.jaw_open = 0.12f;
  dazed.tongue_out = 0.2f;
  dazed.ears_back = 0.3f;
  FaceControls awake;
  awake.lids = 0.15f;
  const Key lying{-0.56f,
                  -0.05f,
                  -0.3f,
                  0.15f,
                  glm::vec3(0.06f, 0.0f, 0.22f),
                  glm::vec3(0.08f, 0.0f, -0.16f),
                  dazed};
  const Key pushed{-0.38f,
                   0.4f,
                   0.05f,
                   -0.1f,
                   glm::vec3(0.0f, 0.0f, 0.05f),
                   glm::vec3(0.04f, 0.0f, -0.12f),
                   awake};
  const Key standing{
      kStandHeight,    0.0f,          0.0f, 0.0f, glm::vec3(0.0f),
      glm::vec3(0.0f), FaceControls()};
  auto mix = [](const Key& a, const Key& b, float t) {
    return Key{glm::mix(a.pelvis_height, b.pelvis_height, t),
               glm::mix(a.pelvis_pitch, b.pelvis_pitch, t),
               glm::mix(a.neck_pitch, b.neck_pitch, t),
               glm::mix(a.head_pitch, b.head_pitch, t),
               glm::mix(a.front, b.front, t),
               glm::mix(a.hind, b.hind, t),
               FaceControls::Mix(a.face, b.face, t)};
  };

  const float roll_end = kSettleTime + (get_up_rolls_over_ ? kRollTime : 0.0f);
  const float push_end = roll_end + kPushTime;
  const float stand_end = push_end + kStandTime;
  const float shake_end = stand_end + kShakeTime;
  const float t = get_up_time_;

  Controls c;
  c.root_position = root_position;
  c.yaw = controls_.yaw;
  Key key = lying;
  float roll = get_up_roll_;
  float kick = 0.0f, shake = 0.0f, shake_phase = 0.0f;
  if (t < kSettleTime) {
    roll = get_up_roll_;
  } else if (t < roll_end) {
    // On the back or the side: rolls over onto the belly, kicking with the legs
    float u = (t - kSettleTime) / kRollTime;
    roll = get_up_roll_ * (1.0f - Smooth(u));
    kick = std::sin(kPi * u);
  } else if (t < push_end) {
    roll = 0.0f;
    key = mix(lying, pushed, Smooth((t - roll_end) / kPushTime));
  } else if (t < stand_end) {
    roll = 0.0f;
    key = mix(pushed, standing, Smooth((t - push_end) / kStandTime));
  } else {
    // Shakes the snow out of its fur
    roll = 0.0f;
    key = standing;
    float u = std::min((t - stand_end) / kShakeTime, 1.0f);
    shake_phase = 2.0f * kPi * 4.0f * u;
    shake = (1.0f - u) * std::min(1.0f, 4.0f * u);
  }
  c.body_roll = roll;
  c.pelvis_offset.y = key.pelvis_height;
  c.pelvis_pitch = key.pelvis_pitch;
  c.neck_pitch = key.neck_pitch;
  c.head_pitch = key.head_pitch;
  c.pelvis_roll = 0.3f * shake * std::sin(shake_phase);
  c.spine_roll = -0.4f * shake * std::sin(shake_phase);
  c.head_roll = -0.5f * shake * std::sin(shake_phase + 0.6f);
  c.neck_yaw = 0.2f * shake * std::sin(shake_phase + 1.2f);
  c.face = key.face;
  c.face.lids = std::max({c.face.lids, 0.8f * shake, BlinkAmount()});
  c.face.ears_back = std::max(c.face.ears_back, 0.5f * shake);
  c.face.gaze_yaw = gaze_yaw_;
  c.face.gaze_pitch = gaze_pitch_;

  glm::mat4 frame = BodyFrame(c);
  glm::vec3 body_up = RotationOf(frame) * kUp;
  for (int leg = 0; leg < kLegCount; ++leg) {
    const LegRig& rig = legs_[leg];
    glm::vec3 offset = rig.front ? key.front : key.hind;
    offset.x *= rig.side;
    float wave = 2.0f * kPi * 3.0f * t + (rig.front ? 0.0f : kPi) +
                 (rig.side > 0.0f ? 0.0f : kPi * 0.5f);
    offset +=
        kick * glm::vec3(0.0f, 0.14f * std::sin(wave), 0.08f * std::cos(wave));
    glm::vec3 foot = glm::vec3(frame * glm::vec4(rig.neutral + offset, 1.0f));
    glm::vec3 normal = body_up;
    glm::vec3 ground, ground_normal;
    if (roll == 0.0f && ProbeGround(probe, foot, ground, ground_normal)) {
      foot = ground;
      normal = ground_normal;
    }
    c.feet[leg] = foot;
    c.foot_normals[leg] = normal;
  }

  Evaluate(bind_, c, time_delta);
  // At first the pose of the ragdoll blends into the animation
  float blend = Smooth(t / kSettleTime);
  if (blend < 1.0f && get_up_snapshot_.size() == pose_.size()) {
    for (std::size_t i = 0; i < pose_.size(); ++i) {
      pose_[i] = Transform::Mix(get_up_snapshot_[i], pose_[i], blend);
      ForwardKinematics(static_cast<int>(i));
    }
  }

  if (t >= shake_end) {
    // Walking starts from where the feet are now
    for (int leg = 0; leg < kLegCount; ++leg) {
      feet_[leg].position = feet_[leg].planted = feet_[leg].swing_from =
          c.feet[leg];
      feet_[leg].normal = c.foot_normals[leg];
      feet_[leg].swinging = false;
    }
    gait_phase_ = 0.0f;
    body_height_ = body_pitch_ = lean_ = bend_ = 0.0f;
    airborne_time_ = 0.0f;
    launch_time_ = -1.0f;
    air_pose_ = reach_ = 0.0f;
    landing_blend_ = 1.0f;
    return true;
  }
  return false;
}

void BearRig::SkinMatrices(std::vector<glm::mat4>& result) const {
  skeleton_.SkinMatrices(world_, result);
}

glm::vec3 BearRig::GetCenter() const {
  return 0.5f * (PositionOf(world_[pelvis_]) + PositionOf(world_[chest_]));
}

}  // namespace bibo::scene

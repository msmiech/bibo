#include "BearRig.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../Animation/InverseKinematics.hpp"

using namespace bibo;
using namespace scene;
using animation::Pose;
using animation::rotationBetween;
using animation::smoothingFactor;
using animation::Transform;

namespace {
const glm::vec3 UP(0.0f, 1.0f, 0.0f);
const glm::vec3 LEFT(1.0f, 0.0f, 0.0f);
const float PI = glm::pi<float>();
// The front legs are straight in the bind pose, standing a bit lower bends them
// slightly (near full extension a few centimeters already bend the elbows a
// lot)
const float STAND_HEIGHT = -0.01f;
// The legs are only tucked in after being in the air for a moment (not when
// stepping over a bump)
const float TUCK_DELAY = 0.08f;
// How far the body may move while a foot is on the ground
const float MAX_STANCE_TRAVEL = 0.6f;
// Height of the axis the body rolls around when it lies on its side or back
const float ROLL_PIVOT = 0.5f;
// Lengths of the spring bones (from the bone to the tip)
const float EAR_LENGTH = 0.26f;
const float TAIL_LENGTH = 0.28f;
// Face: the lips press together a little when the mouth is closed, a yawn opens
// it this far
const float JAW_PRESS_DEGREES = 3.0f;
const float JAW_OPEN_DEGREES = 44.0f;
// The tongue slides this far out of the mouth; it only bends down where it is
// past the lower lip (distance from the root of the tongue, see TONGUE_CENTER
// and the jaw in tools/build_bear_cub.py), so it hangs over the lip instead of
// going through the jaw
const float TONGUE_REACH = 0.3f;
const float TONGUE_LIP_DISTANCE = 0.4f;
// How far the eyelids turn to close the eye (radians): they meet a bit below
// the middle of the eye
const float UPPER_LID_CLOSE = 0.88f;
const float LOWER_LID_CLOSE = 0.34f;
const float BLINK_DURATION = 0.2f;
// Blending into and out of animations (idle, after an idle was interrupted)
const float BLEND_TIME = 0.3f;

// Phase in the step cycle when the stance of a leg begins (front left, front
// right, hind left, hind right)
const float WALK_OFFSETS[BearRig::LEG_COUNT] = {0.25f, 0.75f, 0.0f,
                                                0.5f};  // lateral sequence walk
const float BOUND_OFFSETS[BearRig::LEG_COUNT] = {
    0.5f, 0.6f, 0.0f,
    0.1f};  // half bound: hind legs together, then the front legs
const float SWIM_OFFSETS[BearRig::LEG_COUNT] = {
    0.0f, 0.5f, 0.5f, 0.0f};  // dog paddle: diagonal pairs of legs together

glm::quat rotationOf(const glm::mat4& matrix) {
  glm::mat3 rotation(glm::normalize(glm::vec3(matrix[0])),
                     glm::normalize(glm::vec3(matrix[1])),
                     glm::normalize(glm::vec3(matrix[2])));
  return glm::normalize(glm::quat_cast(rotation));
}

glm::vec3 positionOf(const glm::mat4& matrix) { return glm::vec3(matrix[3]); }

glm::mat4 rigid(const glm::mat4& matrix) {
  glm::mat4 result = glm::mat4_cast(rotationOf(matrix));
  result[3] = matrix[3];
  return result;
}

/**
 * Rotation in the frame of the body: pitch lifts the nose, yaw turns left, roll
 * lowers the right side
 */
glm::quat bodyRotation(float pitch, float yaw, float roll) {
  return glm::angleAxis(yaw, UP) * glm::angleAxis(-pitch, LEFT) *
         glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
}

float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float fract(float x) { return x - std::floor(x); }

/**
 * Limits a rotation to a maximum angle
 */
glm::quat limitAngle(const glm::quat& rotation, float maxAngle) {
  float angle = 2.0f * std::acos(std::clamp(std::abs(rotation.w), 0.0f, 1.0f));
  if (angle <= maxAngle) {
    return rotation;
  }
  return glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), rotation,
                    maxAngle / angle);
}

float uniform(std::mt19937& random, float from, float to) {
  return from +
         (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}
}  // namespace

FaceControls FaceControls::mix(const FaceControls& a, const FaceControls& b,
                               float t) {
  FaceControls result;
  result.jawOpen = glm::mix(a.jawOpen, b.jawOpen, t);
  result.tongueOut = glm::mix(a.tongueOut, b.tongueOut, t);
  result.tongueCurl = glm::mix(a.tongueCurl, b.tongueCurl, t);
  result.tongueSide = glm::mix(a.tongueSide, b.tongueSide, t);
  result.lids = glm::mix(a.lids, b.lids, t);
  result.gazeYaw = glm::mix(a.gazeYaw, b.gazeYaw, t);
  result.gazePitch = glm::mix(a.gazePitch, b.gazePitch, t);
  result.earsBack = glm::mix(a.earsBack, b.earsBack, t);
  return result;
}

BearRig::IdleKey BearRig::IdleKey::mix(const IdleKey& a, const IdleKey& b,
                                       float t) {
  IdleKey result;
  result.time = glm::mix(a.time, b.time, t);
  result.pelvisHeight = glm::mix(a.pelvisHeight, b.pelvisHeight, t);
  result.pelvisPitch = glm::mix(a.pelvisPitch, b.pelvisPitch, t);
  result.pelvisRoll = glm::mix(a.pelvisRoll, b.pelvisRoll, t);
  result.spinePitch = glm::mix(a.spinePitch, b.spinePitch, t);
  result.spineYaw = glm::mix(a.spineYaw, b.spineYaw, t);
  result.spineRoll = glm::mix(a.spineRoll, b.spineRoll, t);
  result.neckPitch = glm::mix(a.neckPitch, b.neckPitch, t);
  result.neckYaw = glm::mix(a.neckYaw, b.neckYaw, t);
  result.headPitch = glm::mix(a.headPitch, b.headPitch, t);
  result.headYaw = glm::mix(a.headYaw, b.headYaw, t);
  result.headRoll = glm::mix(a.headRoll, b.headRoll, t);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    result.feet[leg] = glm::mix(a.feet[leg], b.feet[leg], t);
    result.grounded[leg] = glm::mix(a.grounded[leg], b.grounded[leg], t);
    result.toePitch[leg] = glm::mix(a.toePitch[leg], b.toePitch[leg], t);
  }
  result.face = FaceControls::mix(a.face, b.face, t);
  result.sway = glm::mix(a.sway, b.sway, t);
  return result;
}

BearRig::BearRig(const animation::Skeleton& skeleton) : skeleton(skeleton) {
  auto require = [&](const char* name) {
    int bone = skeleton.find(name);
    if (bone < 0) {
      missingBones += std::string(missingBones.empty() ? "" : ", ") + name;
    }
    return bone;
  };
  root = require("root");
  pelvis = require("pelvis");
  spine = require("spine");
  chest = require("chest");
  neck = require("neck");
  head = require("head");
  jaw = require("jaw");
  tongue = {require("tongue.1"), require("tongue.2"), require("tongue.3")};
  earLeft = require("ear.L");
  earRight = require("ear.R");
  tail = require("tail");
  const char* const eyeBones[2][3] = {
      {"eye.L", "eyelid.upper.L", "eyelid.lower.L"},
      {"eye.R", "eyelid.upper.R", "eyelid.lower.R"}};
  for (int side = 0; side < 2; ++side) {
    eyes[side].eye = require(eyeBones[side][0]);
    eyes[side].upperLid = require(eyeBones[side][1]);
    eyes[side].lowerLid = require(eyeBones[side][2]);
  }
  const char* const legBones[LEG_COUNT][3] = {
      {"upperarm.L", "forearm.L", "hand.L"},
      {"upperarm.R", "forearm.R", "hand.R"},
      {"thigh.L", "shin.L", "foot.L"},
      {"thigh.R", "shin.R", "foot.R"}};
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    legs[leg].upper = require(legBones[leg][0]);
    legs[leg].lower = require(legBones[leg][1]);
    legs[leg].paw = require(legBones[leg][2]);
  }
  if (!isValid()) {
    return;
  }

  bind = skeleton.bindPose();
  skeleton.modelMatrices(bind, bindModel);
  for (const glm::mat4& matrix : bindModel) {
    bindRotation.push_back(rotationOf(matrix));
  }
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    LegRig& rig = legs[leg];
    glm::vec3 upper = positionOf(bindModel[rig.upper]),
              lower = positionOf(bindModel[rig.lower]),
              paw = positionOf(bindModel[rig.paw]);
    rig.upperLength = glm::distance(upper, lower);
    rig.lowerLength = glm::distance(lower, paw);
    rig.pawHeight = paw.y;
    rig.neutral = glm::vec3(paw.x, 0.0f, paw.z);
    rig.pawRotation = bindRotation[rig.paw];
    rig.front = leg == FRONT_LEFT || leg == FRONT_RIGHT;
    rig.side = paw.x >= 0.0f ? 1.0f : -1.0f;
  }
  pelvisBindRotation = bindRotation[pelvis];
  // The eyes look along their bones, the upper lids close around the axis
  // across the gaze
  for (Eye& eye : eyes) {
    glm::vec3 gaze = glm::normalize(glm::vec3(bindModel[eye.eye][1]));
    glm::vec3 up = glm::normalize(UP - gaze * glm::dot(UP, gaze));
    eye.lidAxis = glm::normalize(glm::cross(up, gaze));
  }
  tongueDirection = glm::normalize(positionOf(bindModel[tongue[2]]) -
                                   positionOf(bindModel[tongue[0]]));
  for (int i = 0; i < 3; ++i) {
    tongueBoneDistance[i] = glm::dot(
        positionOf(bindModel[tongue[i]]) - positionOf(bindModel[tongue[0]]),
        tongueDirection);
  }
  partBones = {pelvis,
               chest,
               head,
               legs[FRONT_LEFT].upper,
               legs[FRONT_LEFT].lower,
               legs[FRONT_RIGHT].upper,
               legs[FRONT_RIGHT].lower,
               legs[HIND_LEFT].upper,
               legs[HIND_LEFT].lower,
               legs[HIND_RIGHT].upper,
               legs[HIND_RIGHT].lower};
  buildIdleAnimations();

  pose = bind;
  world = bindModel;
  previousWorld = world;
}

glm::mat4 BearRig::bodyFrame(const Controls& c) const {
  glm::mat4 frame = glm::translate(glm::mat4(1.0f), c.rootPosition) *
                    glm::mat4_cast(glm::angleAxis(c.yaw, UP));
  if (c.bodyRoll != 0.0f) {
    frame = frame *
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, ROLL_PIVOT, 0.0f)) *
            glm::mat4_cast(
                glm::angleAxis(c.bodyRoll, glm::vec3(0.0f, 0.0f, 1.0f))) *
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -ROLL_PIVOT, 0.0f));
  }
  return frame;
}

glm::mat4 BearRig::parentWorld(int bone) const {
  int parent = skeleton.bones[bone].parent;
  return parent >= 0 ? world[parent]
                     : bodyFrame(controls) * skeleton.rootParent;
}

void BearRig::forwardKinematics(int bone) {
  world[bone] = parentWorld(bone) * pose[bone].toMatrix();
}

void BearRig::rotateBone(int bone, const glm::quat& worldDelta) {
  glm::quat parent = rotationOf(parentWorld(bone));
  glm::quat current = rotationOf(world[bone]);
  pose[bone].rotation =
      glm::normalize(glm::inverse(parent) * worldDelta * current);
  forwardKinematics(bone);
}

glm::quat BearRig::boneFrame(int bone) const {
  return rotationOf(world[bone]) * glm::inverse(bindRotation[bone]);
}

void BearRig::updateSprings(int bone, animation::SpringPoint& spring,
                            float length, float stiffness, float damping,
                            float gravity, float maxAngle, float timeDelta) {
  glm::vec3 origin = positionOf(world[bone]);
  glm::vec3 direction =
      rotationOf(world[bone]) * UP;  // bones point along their local y axis
  glm::vec3 anchor = origin + direction * length;
  if (timeDelta <= 0.0f ||
      glm::distance(spring.position, anchor) > 3.0f * length) {
    spring.reset(anchor);
    return;
  }
  spring.update(anchor, stiffness, damping, glm::vec3(0.0f, -gravity, 0.0f),
                timeDelta);
  glm::vec3 toTip = spring.position - origin;
  if (glm::dot(toTip, toTip) < 1e-8f) {
    spring.reset(anchor);
    return;
  }
  rotateBone(bone, limitAngle(rotationBetween(direction, toTip), maxAngle));
  spring.position =
      origin +
      glm::normalize(toTip) * length;  // the tip stays at the end of the bone
}

void BearRig::applyFace(int bone, const FaceControls& face, float timeDelta) {
  // Rotations around the axes of the model, carried along by the head (or jaw)
  auto aroundModelAxes = [&](int frameBone, const glm::quat& rotation) {
    glm::quat frame = boneFrame(frameBone);
    return frame * rotation * glm::inverse(frame);
  };

  const float tongueOut = std::clamp(face.tongueOut, 0.0f, 1.0f);
  if (bone == jaw) {
    // The mouth opens a bit for the tongue to come out
    float open =
        std::clamp(std::max(face.jawOpen, 0.2f * tongueOut), 0.0f, 1.0f);
    float angle = glm::radians(-JAW_BIND_OPEN_DEGREES - JAW_PRESS_DEGREES +
                               open * (JAW_OPEN_DEGREES + JAW_PRESS_DEGREES));
    rotateBone(
        bone,
        aroundModelAxes(
            head, glm::angleAxis(
                      angle, LEFT)));  // positive around the left axis opens
  } else if (bone == tongue[0]) {
    // The tongue slides forward along the floor of the mouth, rising a bit to
    // come over the lower lip
    glm::quat frame = boneFrame(jaw);
    glm::vec3 position = positionOf(world[bone]) +
                         frame * tongueDirection * (TONGUE_REACH * tongueOut);
    pose[bone].translation =
        glm::vec3(glm::inverse(parentWorld(bone)) * glm::vec4(position, 1.0f));
    forwardKinematics(bone);
    rotateBone(
        bone,
        aroundModelAxes(
            jaw, glm::angleAxis(0.5f * face.tongueSide * tongueOut, UP) *
                     glm::angleAxis(-0.1f * smooth(2.0f * tongueOut), LEFT)));
  } else if (bone == tongue[1] || bone == tongue[2]) {
    // Only the part of the tongue that is out of the mouth hangs down (and
    // curls down), inside it lies on the floor of the mouth
    int index = bone == tongue[1] ? 1 : 2;
    float outside =
        smooth((tongueBoneDistance[index] + TONGUE_REACH * tongueOut -
                TONGUE_LIP_DISTANCE + 0.03f) /
               0.08f);
    float hang = (index == 2 ? 0.7f : 0.3f) * tongueOut * outside;
    float curl =
        (index == 2 ? 0.6f : 0.4f) * face.tongueCurl *
        (face.tongueCurl > 0.0f ? outside
                                : 1.0f);  // curling up is always possible
    rotateBone(bone, aroundModelAxes(bone, glm::angleAxis(hang + curl, LEFT)));
  } else if (bone == earLeft || bone == earRight) {
    rotateBone(bone,
               aroundModelAxes(
                   head, glm::angleAxis(-0.9f * face.earsBack, LEFT) *
                             glm::angleAxis((bone == earLeft ? -0.3f : 0.3f) *
                                                face.earsBack,
                                            glm::vec3(0.0f, 0.0f, 1.0f))));
    updateSprings(bone, earSprings[bone == earLeft ? 0 : 1], EAR_LENGTH, 260.0f,
                  10.0f, 2.0f, 0.8f, timeDelta);
  } else {
    for (const Eye& eye : eyes) {
      float closed = std::clamp(face.lids, 0.0f, 1.0f);
      if (bone == eye.eye) {
        rotateBone(bone,
                   aroundModelAxes(
                       head, bodyRotation(face.gazePitch, face.gazeYaw, 0.0f)));
      } else if (bone == eye.upperLid) {
        // The upper lid follows the eye when it looks up or down
        float angle =
            closed * UPPER_LID_CLOSE - 0.7f * face.gazePitch * (1.0f - closed);
        rotateBone(bone,
                   aroundModelAxes(head, glm::angleAxis(angle, eye.lidAxis)));
      } else if (bone == eye.lowerLid) {
        float angle =
            closed * LOWER_LID_CLOSE + 0.3f * face.gazePitch * (1.0f - closed);
        rotateBone(bone,
                   aroundModelAxes(head, glm::angleAxis(angle, -eye.lidAxis)));
      }
    }
  }
}

void BearRig::evaluate(const Pose& base, const Controls& c, float timeDelta) {
  controls = c;
  pose = base;
  world.resize(pose.size());
  glm::quat body = rotationOf(bodyFrame(c));
  auto inBody = [&](float pitch, float yaw, float roll) {
    return body * bodyRotation(pitch, yaw, roll) * glm::inverse(body);
  };

  for (int i = 0; i < static_cast<int>(pose.size()); ++i) {
    forwardKinematics(i);
    if (i == pelvis) {
      glm::vec3 position = positionOf(world[i]) + body * c.pelvisOffset;
      pose[i].translation =
          glm::vec3(glm::inverse(parentWorld(i)) * glm::vec4(position, 1.0f));
      forwardKinematics(i);
      rotateBone(i, inBody(c.pelvisPitch, c.pelvisYaw, c.pelvisRoll));
    } else if (i == spine || i == chest) {
      rotateBone(i, inBody(0.5f * c.spinePitch, 0.5f * c.spineYaw,
                           0.5f * c.spineRoll));
    } else if (i == neck) {
      rotateBone(i, inBody(c.neckPitch, c.neckYaw, 0.0f));
    } else if (i == head) {
      rotateBone(i, inBody(c.headPitch, c.headYaw, c.headRoll));
    } else if (i == tail) {
      updateSprings(i, tailSpring, TAIL_LENGTH, 140.0f, 7.0f, 4.0f, 1.0f,
                    timeDelta);
    } else {
      applyFace(i, c.face, timeDelta);
    }
  }

  // Legs: the paws reach their targets with two bone IK, the knees bend
  // forward, the elbows backward
  glm::vec3 forward = body * glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 left = body * LEFT;
  glm::vec3 bodyUp = body * UP;
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    const LegRig& rig = legs[leg];
    glm::vec3 normal = glm::length(c.footNormals[leg]) > 0.5f
                           ? glm::normalize(c.footNormals[leg])
                           : bodyUp;
    glm::vec3 target = c.feet[leg] + normal * rig.pawHeight;
    glm::vec3 upper = positionOf(world[rig.upper]);
    glm::vec3 lower = positionOf(world[rig.lower]);
    glm::vec3 pole = lower + (rig.front ? -forward : forward) * 0.6f +
                     left * (0.1f * rig.side);
    animation::TwoBoneSolution solution = animation::solveTwoBoneIK(
        upper, rig.upperLength, rig.lowerLength, target, pole);

    rotateBone(rig.upper,
               rotationBetween(lower - upper, solution.joint - upper));
    forwardKinematics(rig.lower);
    forwardKinematics(rig.paw);
    glm::vec3 joint = positionOf(world[rig.lower]);
    rotateBone(rig.lower, rotationBetween(positionOf(world[rig.paw]) - joint,
                                          solution.end - joint));
    forwardKinematics(rig.paw);

    // The paw lies flat on the ground and points where the bear goes, it curls
    // while the leg swings
    glm::quat tilt = limitAngle(rotationBetween(bodyUp, normal), 0.6f);
    glm::quat toes = glm::angleAxis(c.toePitch[leg], left);
    glm::quat paw = toes * tilt * body * rig.pawRotation;
    pose[rig.paw].rotation =
        glm::normalize(glm::inverse(rotationOf(world[rig.lower])) * paw);
    forwardKinematics(rig.paw);
  }
}

bool BearRig::probeGround(const GroundProbe& probe, glm::vec3 point,
                          glm::vec3& ground, glm::vec3& normal) const {
  if (!probe || !probe(point + UP * 1.0f, point - UP * 1.6f, ground, normal)) {
    return false;
  }
  if (normal.y < 0.2f) {
    normal = UP;  // a wall, not a floor
  }
  return true;
}

void BearRig::plantFeet(const GroundProbe& probe, const Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, UP);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    Foot& foot = feet[leg];
    glm::vec3 position = c.rootPosition + yaw * legs[leg].neutral;
    glm::vec3 ground, normal;
    if (probeGround(probe, position, ground, normal)) {
      position = ground;
    } else {
      normal = UP;
    }
    foot.position = foot.planted = foot.swingFrom = position;
    foot.normal = normal;
    foot.swinging = false;
    foot.swingProgress = 0.0f;
  }
}

void BearRig::landFeet(const GroundProbe& probe, const Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, UP);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    Foot& foot = feet[leg];
    glm::vec3 neutral = c.rootPosition + yaw * legs[leg].neutral;
    glm::vec3 offset(foot.position.x - neutral.x, 0.0f,
                     foot.position.z - neutral.z);
    if (glm::length(offset) > 0.35f) {
      offset *= 0.35f / glm::length(offset);
    }
    glm::vec3 position = neutral + offset;
    glm::vec3 ground, normal;
    if (probeGround(probe, position, ground, normal)) {
      position = ground;
    } else {
      normal = UP;
    }
    foot.position = foot.planted = foot.swingFrom = position;
    foot.normal = normal;
    foot.swinging = false;
    foot.swingProgress = 0.0f;
  }
}

void BearRig::updateAirborne(float timeDelta, const LocomotionInput& input,
                             const GroundProbe& probe, Controls& c) {
  const float LAUNCH_DURATION = 0.18f;
  glm::quat yaw = glm::angleAxis(c.yaw, UP);
  glm::vec3 horizontalVelocity(input.velocity.x, 0.0f, input.velocity.z);
  if (airborneTime == 0.0f) {
    for (Foot& foot : feet) {
      // A step that started on the ground ends where it was going, the paws
      // push off from there
      if (foot.swinging) {
        foot.swinging = false;
        foot.position = foot.planted;
      }
    }
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
      feet[leg].swingFrom = feet[leg].position;
      airOffsets[leg] = feet[leg].position - input.rootPosition;
    }
  }
  airborneTime += timeDelta;
  lastVerticalSpeed = input.velocity.y;
  if (launchTime >= 0.0f) {
    launchTime += timeDelta;
  }
  float launch =
      launchTime >= 0.0f ? 1.0f - smooth(launchTime / LAUNCH_DURATION) : 0.0f;

  // Falling: the legs reach for the ground shortly before the bear lands there
  // (where it will be in a moment)
  float fallSpeed = std::max(-input.velocity.y, 0.0f);
  float targetReach = 0.0f;
  glm::vec3 ahead = input.rootPosition + horizontalVelocity * 0.1f;
  glm::vec3 ground, normal;
  if (fallSpeed > 0.5f && probe &&
      probe(ahead + UP * 0.5f, ahead - UP * 12.0f, ground, normal)) {
    float timeToLand = std::max(ahead.y - ground.y, 0.0f) / fallSpeed;
    targetReach = 1.0f - smooth((timeToLand - 0.06f) / 0.3f);
  }
  reach += (targetReach - reach) * smoothingFactor(0.03f, timeDelta);

  // The legs only go into the air pose after a moment in the air (not when the
  // bear runs over a bump), right away when jumping
  float weight = std::max(smooth((airborneTime - TUCK_DELAY) / 0.12f),
                          launchTime >= 0.0f ? 1.0f : 0.0f);
  airPose = std::max(airPose, weight);
  // A long fall spreads the legs
  float spread = smooth((fallSpeed - 10.0f) / 12.0f) * (1.0f - reach);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    const LegRig& rig = legs[leg];
    Foot& foot = feet[leg];
    // Tucked in: the front paws under the chest, the hind paws under the belly.
    // Reaching: the front legs stretch forward and down to take the landing,
    // the hind legs follow a bit higher
    glm::vec3 tucked = rig.front ? glm::vec3(0.0f, 0.3f, 0.12f)
                                 : glm::vec3(0.0f, 0.28f, 0.08f);
    glm::vec3 reaching = rig.front ? glm::vec3(0.0f, -0.02f, 0.2f)
                                   : glm::vec3(0.0f, 0.06f, -0.04f);
    glm::vec3 offset = glm::mix(tucked, reaching, reach) +
                       glm::vec3(0.1f * rig.side, 0.1f, 0.0f) * spread;
    glm::vec3 target = input.rootPosition + yaw * (rig.neutral + offset);
    float toes = glm::mix(0.6f, -0.1f, reach);
    if (rig.front) {
      // Pushing off: the front legs lift and reach forward
      target += yaw * glm::vec3(0.0f, 0.05f, 0.18f) * launch;
    } else {
      // Pushing off: the hind paws stay where they left the ground until the
      // legs are stretched
      target = glm::mix(target, foot.swingFrom, launch);
      toes = glm::mix(toes, 0.9f, launch);
    }
    // Smoothed relative to the body, which moves fast (in the world the paws
    // would trail behind)
    airOffsets[leg] = glm::mix(airOffsets[leg], target - input.rootPosition,
                               weight * smoothingFactor(0.035f, timeDelta));
    foot.position = input.rootPosition + airOffsets[leg];
    foot.normal = glm::normalize(
        glm::mix(foot.normal, UP, smoothingFactor(0.05f, timeDelta)));
    c.toePitch[leg] = toes * weight;
  }

  // Nose up when rising, down when falling, a bit less when the front legs
  // reach for the ground
  float pitch =
      input.velocity.y > 0.0f
          ? std::min(input.velocity.y * 0.025f, 0.3f)
          : std::max(input.velocity.y * 0.018f, -0.3f) * (1.0f - 0.4f * reach);
  bodyPitch += (pitch - bodyPitch) * smoothingFactor(0.08f, timeDelta);
  bodyHeight += (0.0f - bodyHeight) * smoothingFactor(0.1f, timeDelta);
}

void BearRig::updateSwimming(float timeDelta, const LocomotionInput& input,
                             Controls& c) {
  glm::quat yaw = glm::angleAxis(c.yaw, UP);
  if (!wasSwimming) {
    // The paws start paddling from where they are
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
      feet[leg].swinging = false;
      airOffsets[leg] = feet[leg].position - input.rootPosition;
    }
  }
  airborneTime = 0.0f;
  launchTime = -1.0f;
  reach = 0.0f;

  // Faster strokes and wider circles when the bear swims fast, small ones when
  // it treads water
  float speed = glm::length(glm::vec2(input.velocity.x, input.velocity.z));
  float effort =
      std::clamp(speed / 3.5f + std::abs(input.yawRate) * 0.25f, 0.0f, 1.0f);
  swimEffort += (effort - swimEffort) * smoothingFactor(0.2f, timeDelta);
  swimPhase += glm::mix(1.1f, 1.8f, swimEffort) * timeDelta;
  float size = glm::mix(0.55f, 1.0f, swimEffort);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    const LegRig& rig = legs[leg];
    Foot& foot = feet[leg];
    float phase = 2.0f * PI * (swimPhase + SWIM_OFFSETS[leg]);
    // The paw goes round: forward high, down, back under the body and up again
    // (relative to the ground point under the hip)
    glm::vec3 offset =
        rig.front ? glm::vec3(0.0f, 0.34f + 0.13f * size * std::sin(phase),
                              0.14f + 0.2f * size * std::cos(phase))
                  : glm::vec3(0.0f, 0.3f + 0.1f * size * std::sin(phase),
                              -0.08f + 0.2f * size * std::cos(phase));
    offset.x = 0.05f * rig.side;
    glm::vec3 target = input.rootPosition + yaw * (rig.neutral + offset);
    airOffsets[leg] =
        glm::mix(airOffsets[leg], target - input.rootPosition,
                 smoothingFactor(wasSwimming ? 0.03f : 0.08f, timeDelta));
    foot.position = foot.planted = foot.swingFrom =
        input.rootPosition + airOffsets[leg];
    foot.normal = glm::normalize(
        glm::mix(foot.normal, UP, smoothingFactor(0.05f, timeDelta)));
    // The paw pushes the water with its sole on the way back and turns on the
    // way forward
    c.toePitch[leg] = rig.front ? 0.35f + 0.45f * std::sin(phase)
                                : 0.1f + 0.5f * std::sin(phase);
  }
  // Nose up, the body lies in the water
  bodyPitch += (0.12f - bodyPitch) * smoothingFactor(0.15f, timeDelta);
  bodyHeight += (0.0f - bodyHeight) * smoothingFactor(0.1f, timeDelta);
  wasSwimming = true;
}

void BearRig::reset(const LocomotionInput& input, const GroundProbe& probe) {
  if (!isValid()) {
    return;
  }
  gaitPhase = 0.0f;
  gallop = 0.0f;
  bodyHeight = bodyPitch = lean = bend = 0.0f;
  airborneTime = 0.0f;
  launchTime = -1.0f;
  airPose = reach = 0.0f;
  landingBlend = 1.0f;
  swimPhase = swimEffort = 0.0f;
  wasSwimming = false;
  squash = animation::SpringValue();
  pitchKick = animation::SpringValue();
  lookYaw = lookPitch = lookTargetYaw = lookTargetPitch = 0.0f;
  gazeYaw = gazePitch = gazeTargetYaw = gazeTargetPitch = 0.0f;
  blinkProgress = -1.0f;
  idle = -1;
  blendTime = 0.0f;

  Controls c;
  c.rootPosition = input.rootPosition;
  c.yaw = std::atan2(input.direction.x, input.direction.z);
  c.pelvisOffset.y = STAND_HEIGHT;
  controls = c;
  plantFeet(probe, c);
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    c.feet[leg] = feet[leg].position;
    c.footNormals[leg] = feet[leg].normal;
  }
  evaluate(bind, c, 0.0f);
  previousWorld = world;
  lastTimeDelta = 0.0f;
}

float BearRig::blinkAmount() const {
  if (blinkProgress < 0.0f) {
    return 0.0f;
  }
  // Closes quickly, opens a bit slower
  return blinkProgress < 0.35f ? smooth(blinkProgress / 0.35f)
                               : 1.0f - smooth((blinkProgress - 0.35f) / 0.65f);
}

void BearRig::lookAt(glm::vec3 target) {
  looking = true;
  lookTarget = target;
}

void BearRig::updateSecondary(float timeDelta, bool moving) {
  // The head looks around when the bear stands, and ahead when it moves; at a
  // point it looks there
  lookTimer -= timeDelta;
  float targetYaw = 0.0f, targetPitch = 0.0f;
  if (looking) {
    glm::vec3 toTarget = lookTarget - getBonePosition(head);
    targetYaw = std::remainder(
        std::atan2(toTarget.x, toTarget.z) - controls.yaw, 2.0f * PI);
    targetPitch =
        std::atan2(toTarget.y, glm::length(glm::vec2(toTarget.x, toTarget.z)));
    lookTargetYaw = std::clamp(targetYaw, -0.9f, 0.9f);
    lookTargetPitch = std::clamp(targetPitch, -0.35f, 0.45f);
    lookTimer = 1.0f;
  } else if (moving) {
    lookTargetYaw = 0.0f;
    lookTargetPitch = 0.05f;
    lookTimer = std::min(lookTimer, 1.0f);
  } else if (lookTimer <= 0.0f) {
    lookTargetYaw = uniform(random, -0.6f, 0.6f);
    lookTargetPitch = uniform(random, -0.12f, 0.25f);
    lookTimer = uniform(random, 1.5f, 4.0f);
    saccadeTimer = 0.0f;  // the eyes lead the head
  }
  lookYaw += (lookTargetYaw - lookYaw) * smoothingFactor(0.15f, timeDelta);
  lookPitch +=
      (lookTargetPitch - lookPitch) * smoothingFactor(0.15f, timeDelta);

  // Saccades: the eyes jump quickly from one point to the next. Looking at a
  // point they take the rest of the turn the head has not made yet and stay on
  // it (only tiny jumps)
  saccadeTimer -= timeDelta;
  if (looking) {
    if (saccadeTimer <= 0.0f) {
      fixationYaw = uniform(random, -0.02f, 0.02f);
      fixationPitch = uniform(random, -0.015f, 0.015f);
      saccadeTimer = uniform(random, 0.5f, 1.5f);
    }
    gazeTargetYaw = targetYaw - lookYaw + fixationYaw;
    gazeTargetPitch = targetPitch - lookPitch + fixationPitch;
  } else if (saccadeTimer <= 0.0f) {
    gazeTargetYaw =
        0.6f * (lookTargetYaw - lookYaw) + uniform(random, -0.12f, 0.12f);
    gazeTargetPitch =
        0.6f * (lookTargetPitch - lookPitch) + uniform(random, -0.07f, 0.07f);
    saccadeTimer = uniform(random, 0.4f, 2.2f);
  }
  gazeYaw += (std::clamp(gazeTargetYaw, -0.35f, 0.35f) - gazeYaw) *
             smoothingFactor(0.02f, timeDelta);
  gazePitch += (std::clamp(gazeTargetPitch, -0.25f, 0.25f) - gazePitch) *
               smoothingFactor(0.02f, timeDelta);

  blinkTimer -= timeDelta;
  if (blinkProgress < 0.0f && blinkTimer <= 0.0f) {
    blinkProgress = 0.0f;
  }
  if (blinkProgress >= 0.0f) {
    blinkProgress += timeDelta / BLINK_DURATION;
    if (blinkProgress >= 1.0f) {
      blinkProgress = -1.0f;
      // sometimes twice in a row
      blinkTimer = uniform(random, 0.0f, 1.0f) < 0.2f
                       ? 0.12f
                       : uniform(random, 2.0f, 5.5f);
    }
  }

  earTwitchTimer -= timeDelta;
  if (earTwitchTimer <= 0.0f) {
    if (!moving) {
      glm::vec3 flick(uniform(random, -1.0f, 1.0f), uniform(random, 0.0f, 0.5f),
                      uniform(random, -1.0f, 1.0f));
      earSprings[random() % 2].velocity += flick * 3.0f;
    }
    earTwitchTimer = uniform(random, 3.0f, 7.0f);
  }
}

void BearRig::updateLocomotion(float timeDelta, const LocomotionInput& input,
                               const GroundProbe& probe) {
  if (!isValid() || timeDelta <= 0.0f) {
    return;
  }
  previousWorld = world;
  lastTimeDelta = timeDelta;
  time += timeDelta;

  Controls c;
  if (idle >= 0) {
    updateSecondary(timeDelta, false);
    c = idleControls(input, probe);
    idleTime += timeDelta;
    if (idle == IDLE_LIE) {
      idleTime = std::fmod(idleTime,
                           getIdleDuration(IDLE_LIE));  // lies until stopIdle
    } else if (idleTime >= getIdleDuration(static_cast<Idle>(idle))) {
      // The animation ends standing: walking starts from where the feet are
      idle = -1;
      for (int leg = 0; leg < LEG_COUNT; ++leg) {
        feet[leg].position = feet[leg].planted = feet[leg].swingFrom =
            c.feet[leg];
        feet[leg].normal = c.footNormals[leg];
        feet[leg].swinging = false;
      }
      gaitPhase = 0.0f;
    }
  } else {
    c.rootPosition = input.rootPosition;
    glm::vec3 forward =
        glm::normalize(glm::vec3(input.direction.x, 0.0f, input.direction.z));
    c.yaw = std::atan2(forward.x, forward.z);
    glm::quat yaw = glm::angleAxis(c.yaw, UP);
    glm::vec3 horizontalVelocity(input.velocity.x, 0.0f, input.velocity.z);
    float speed = glm::length(horizontalVelocity);
    float forwardSpeed = glm::dot(horizontalVelocity, forward);
    // Turning on the spot also needs steps
    float effort = speed + std::abs(input.yawRate) * 0.9f;
    float moving = std::clamp(effort / 2.0f, 0.0f, 1.0f);

    gallop += (std::clamp((speed - 3.0f) / 6.0f, 0.0f, 1.0f) - gallop) *
              smoothingFactor(0.06f, timeDelta);
    const float frequency =
        glm::mix(1.8f, 3.6f, gallop);  // steps per second of every leg
    // Part of the cycle a foot is on the ground: never longer than the leg can
    // follow the body (e.g. while speeding up)
    float duty = glm::mix(0.6f, 0.3f, gallop);
    if (speed > 0.01f) {
      duty = std::clamp(MAX_STANCE_TRAVEL * frequency / speed, 0.15f, duty);
    }
    const float swingDuration = (1.0f - duty) / frequency;
    const float stanceDuration = duty / frequency;

    if (input.jumped) {
      launchTime = 0.0f;
      pitchKick.velocity += 1.2f;
    }
    if (input.swimming) {
      updateSwimming(timeDelta, input, c);
    } else if (input.grounded && !input.jumped) {
      if (airborneTime > TUCK_DELAY || launchTime >= 0.0f || wasSwimming) {
        // Landing: the paws are on the ground again, the legs give way (the
        // harder the landing, the more), the front legs take the impact first
        float impact = std::max(-lastVerticalSpeed, 0.0f);
        squash.velocity -= std::min(impact * 0.1f, 2.0f);
        pitchKick.velocity -= std::min(impact * 0.07f, 1.4f);
        landFeet(probe, c);
        landingBlend = 0.0f;
      }
      airborneTime = 0.0f;
      launchTime = -1.0f;
      reach = 0.0f;

      bool anySwinging = false, anyAway = false;
      for (int leg = 0; leg < LEG_COUNT; ++leg) {
        glm::vec3 neutral = input.rootPosition + yaw * legs[leg].neutral;
        glm::vec2 away(feet[leg].planted.x - neutral.x,
                       feet[leg].planted.z - neutral.z);
        anySwinging = anySwinging || feet[leg].swinging;
        anyAway = anyAway || glm::length(away) > 0.3f;
      }
      // Standing still: the feet finish their steps and then stay where they
      // are
      bool stepping = effort > 0.2f || anySwinging || anyAway;
      if (stepping) {
        gaitPhase += frequency * timeDelta;
      }

      // Right after landing the paws go smoothly from where they landed into
      // the steps (which may be under way)
      float landingFollow = glm::mix(smoothingFactor(0.04f, timeDelta), 1.0f,
                                     smooth(landingBlend));
      landingBlend = std::min(1.0f, landingBlend + timeDelta / 0.2f);
      for (int leg = 0; leg < LEG_COUNT; ++leg) {
        Foot& foot = feet[leg];
        glm::vec3 previous = foot.position;
        float phase = fract(gaitPhase - glm::mix(WALK_OFFSETS[leg],
                                                 BOUND_OFFSETS[leg], gallop));
        if (stepping && phase >= duty) {
          float progress = (phase - duty) / (1.0f - duty);
          if (!foot.swinging) {
            foot.swinging = true;
            foot.swingFrom = foot.planted;
          }
          foot.swingProgress = progress;
          // Where the hip will be when the foot lands, and half a stance
          // further, so the foot passes under the hip
          float timeToLand = (1.0f - progress) * swingDuration;
          glm::quat landingYaw =
              glm::angleAxis(c.yaw + input.yawRate * timeToLand, UP);
          glm::vec3 reach = horizontalVelocity * (0.5f * stanceDuration);
          if (glm::length(reach) > 0.45f) {
            reach *= 0.45f / glm::length(reach);
          }
          glm::vec3 landing = input.rootPosition +
                              horizontalVelocity * timeToLand +
                              landingYaw * legs[leg].neutral + reach;
          glm::vec3 ground, normal;
          if (probeGround(probe, landing, ground, normal)) {
            landing = ground;
          } else {
            landing.y = input.rootPosition.y;
            normal = UP;
          }
          float eased = smooth(progress);
          float lift = glm::mix(0.12f, 0.24f, gallop) *
                       std::clamp(effort / 2.5f, 0.35f, 1.0f);
          foot.position = glm::mix(foot.swingFrom, landing, eased) +
                          UP * (lift * std::sin(PI * progress));
          foot.normal = glm::normalize(glm::mix(foot.normal, normal, eased));
          foot.planted = landing;
          c.toePitch[leg] = 0.7f * std::sin(PI * progress);
        } else {
          foot.swinging = false;
          foot.swingProgress = 0.0f;
          foot.position = foot.planted;
        }
        foot.position = glm::mix(previous, foot.position, landingFollow);
      }

      // The body follows the ground under the feet: height over the hind legs,
      // slope from the hind to the front legs
      float hindGround =
          0.5f * (feet[HIND_LEFT].planted.y + feet[HIND_RIGHT].planted.y) -
          input.rootPosition.y;
      float frontGround =
          0.5f * (feet[FRONT_LEFT].planted.y + feet[FRONT_RIGHT].planted.y) -
          input.rootPosition.y;
      float length = legs[FRONT_LEFT].neutral.z - legs[HIND_LEFT].neutral.z;
      bodyHeight += (std::clamp(hindGround, -0.5f, 0.5f) - bodyHeight) *
                    smoothingFactor(0.06f, timeDelta);
      bodyPitch += (std::clamp(std::atan2(frontGround - hindGround, length),
                               -0.5f, 0.5f) -
                    bodyPitch) *
                   smoothingFactor(0.08f, timeDelta);
    } else {
      updateAirborne(timeDelta, input, probe, c);
    }
    bool swimming = input.swimming;
    wasSwimming = swimming;
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
      c.feet[leg] = feet[leg].position;
      c.footNormals[leg] = feet[leg].normal;
    }
    squash.update(0.0f, 170.0f, 13.0f, timeDelta);
    pitchKick.update(0.0f, 110.0f, 12.0f, timeDelta);
    if ((input.grounded && !input.jumped) || swimming) {
      airPose -= std::min(airPose, timeDelta / 0.25f);
    }

    // Bobbing with the steps: twice per cycle when walking, rocking back and
    // forth when bounding
    float cycle = 2.0f * PI * gaitPhase;
    float walking = moving * (1.0f - gallop), bounding = moving * gallop;
    float breathing =
        0.02f * std::sin(2.0f * PI * 0.33f * time) * (1.0f - moving);
    c.pelvisOffset.y = STAND_HEIGHT + bodyHeight + squash.value -
                       0.022f * std::cos(2.0f * cycle) * walking +
                       0.07f * std::sin(cycle - 0.9f) * bounding;
    c.pelvisPitch =
        bodyPitch + pitchKick.value + 0.08f * std::sin(cycle) * bounding;
    // Leaning into turns, the spine bends into them
    lean += (std::clamp(-input.yawRate * forwardSpeed * 0.03f, -0.28f, 0.28f) -
             lean) *
            smoothingFactor(0.12f, timeDelta);
    bend += (std::clamp(input.yawRate * 0.22f, -0.35f, 0.35f) - bend) *
            smoothingFactor(0.12f, timeDelta);
    c.pelvisRoll = lean + 0.03f * std::sin(cycle) * walking;
    c.spineYaw = bend;
    c.spinePitch = 0.16f * std::sin(cycle + 0.4f) * bounding +
                   0.8f * squash.value + breathing;
    c.neckYaw = 0.5f * bend;
    if (swimming) {
      // The body bobs and rocks a little with the strokes
      float stroke = 2.0f * PI * swimPhase;
      c.pelvisOffset.y =
          STAND_HEIGHT + bodyHeight + 0.025f * std::sin(2.0f * stroke);
      c.pelvisPitch = bodyPitch + 0.03f * std::sin(2.0f * stroke + 1.0f);
      c.pelvisRoll = lean + 0.05f * std::sin(stroke) * swimEffort;
      c.spinePitch = 0.03f * std::sin(2.0f * stroke);
    }

    updateSecondary(timeDelta, moving > 0.1f);
    // The head stays level and looks around, running flattens the ears a bit
    c.neckPitch = -0.4f * c.pelvisPitch;
    // In the air the head and the eyes look down to where the bear lands, the
    // ears fold back and the mouth opens a bit
    c.headPitch = lookPitch - 0.3f * c.pelvisPitch - 0.5f * c.spinePitch -
                  0.25f * reach * airPose;
    c.headYaw = lookYaw + 0.4f * bend;
    c.face.gazeYaw = gazeYaw + 0.3f * bend;
    c.face.gazePitch = gazePitch - 0.15f * reach * airPose;
    c.face.lids = blinkAmount() * (1.0f - airPose);
    c.face.earsBack = std::max(0.35f * bounding, 0.5f * airPose);
    c.face.jawOpen = std::max({0.12f * bounding, 0.2f * airPose,
                               carrying ? 0.34f : 0.0f});  // panting a little
    if (swimming) {
      // The head is held up out of the water, the ears flat against the
      // splashes
      c.neckPitch += 0.25f;
      c.headPitch += 0.12f;
      c.face.earsBack = 0.45f;
    }
  }

  c.face.jawOpen = std::max(c.face.jawOpen, mouthOpen);
  evaluate(bind, c, timeDelta);
  // Blending from the previous animation (e.g. sitting when the player
  // interrupts an idle animation)
  if (blendTime > 0.0f && blendSnapshot.size() == pose.size()) {
    float weight = smooth(1.0f - blendTime / BLEND_TIME);
    for (std::size_t i = 0; i < pose.size(); ++i) {
      pose[i] = Transform::mix(blendSnapshot[i], pose[i], weight);
      forwardKinematics(static_cast<int>(i));
    }
    blendTime -= timeDelta;
  }
}

void BearRig::startIdle(Idle animation) {
  if (!isValid()) {
    return;
  }
  idle = animation;
  idleTime = 0.0f;
  blendSnapshot = pose;
  blendTime = BLEND_TIME;
}

void BearRig::stopIdle() {
  if (idle < 0) {
    return;
  }
  idle = -1;
  blendSnapshot = pose;
  blendTime = BLEND_TIME;
  // The feet step from where they are in the animation (the paws in the air
  // land on the next step)
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    feet[leg].position = feet[leg].planted = feet[leg].swingFrom =
        controls.feet[leg];
    feet[leg].swinging = false;
  }
}

float BearRig::getIdleDuration(Idle animation) const {
  const std::vector<IdleKey>& keys = idleAnimations[animation];
  return keys.empty() ? 0.0f : keys.back().time;
}

BearRig::IdleKey BearRig::standingKey() const {
  IdleKey key;
  key.pelvisHeight = STAND_HEIGHT;
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    key.feet[leg] = legs[leg].neutral;
    key.grounded[leg] = 1.0f;
  }
  return key;
}

BearRig::IdleKey BearRig::sittingKey() const {
  // Sitting on its rump like a person, the hind legs in front, the front paws
  // resting on the belly
  IdleKey key = standingKey();
  key.pelvisHeight = -0.56f;
  key.pelvisPitch = 1.1f;
  key.spinePitch = 0.25f;
  key.neckPitch = -0.6f;
  key.headPitch = -0.5f;
  key.face.jawOpen = 0.07f;  // relaxed, the teeth show a little
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    float side = legs[leg].side;
    if (legs[leg].front) {
      key.feet[leg] = glm::vec3(0.28f * side, 0.62f, 0.08f);
      key.grounded[leg] = 0.0f;
      key.toePitch[leg] = 0.5f;
    } else {
      key.feet[leg] = glm::vec3(0.36f * side, 0.0f, 0.24f);
      key.toePitch[leg] = -0.5f;
    }
  }
  return key;
}

BearRig::IdleKey BearRig::lyingKey() const {
  // On its belly like a sphinx, the front paws stretched out in front, the hind
  // legs folded under the body, the head up
  IdleKey key = standingKey();
  key.pelvisHeight = -0.38f;
  key.pelvisPitch = -0.03f;
  key.spinePitch = 0.06f;
  key.neckPitch = 0.3f;
  key.headPitch = -0.08f;
  key.face.lids = 0.2f;  // relaxed
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    float side = legs[leg].side;
    if (legs[leg].front) {
      key.feet[leg] = legs[leg].neutral + glm::vec3(0.03f * side, 0.0f, 0.38f);
      key.toePitch[leg] = 0.15f;
    } else {
      key.feet[leg] = legs[leg].neutral + glm::vec3(0.12f * side, 0.0f, -0.08f);
      key.toePitch[leg] = -0.35f;
    }
  }
  return key;
}

void BearRig::buildIdleAnimations() {
  const IdleKey standing = standingKey();
  const IdleKey sitting = sittingKey();
  const IdleKey lying = lyingKey();
  auto at = [](IdleKey key, float time) {
    key.time = time;
    return key;
  };

  // Stretching (like a bear in the wild): sits down, pulls its left hind paw to
  // its chest with both front paws, looks up contentedly and rocks a bit, then
  // sticks out its tongue (with its head tilted), lets go and gets up again
  {
    std::vector<IdleKey>& keys = idleAnimations[IDLE_STRETCH];
    keys.push_back(at(standing, 0.0f));
    keys.push_back(at(sitting, 0.9f));
    IdleKey reach = sitting;
    reach.headPitch = -0.85f;  // looks at its paw
    reach.feet[HIND_LEFT] = glm::vec3(0.3f, 0.32f, 0.3f);
    reach.grounded[HIND_LEFT] = 0.0f;
    reach.toePitch[HIND_LEFT] = -0.9f;
    reach.feet[FRONT_LEFT] = glm::vec3(0.26f, 0.75f, 0.2f);
    reach.feet[FRONT_RIGHT] = glm::vec3(-0.1f, 0.75f, 0.22f);
    keys.push_back(at(reach, 1.6f));
    IdleKey hold = reach;
    hold.feet[HIND_LEFT] = glm::vec3(0.15f, 0.92f, 0.16f);
    hold.toePitch[HIND_LEFT] = -1.4f;  // the sole faces forward
    hold.feet[FRONT_LEFT] = glm::vec3(0.25f, 0.98f, 0.2f);
    hold.feet[FRONT_RIGHT] = glm::vec3(0.04f, 0.95f, 0.22f);
    hold.toePitch[FRONT_LEFT] = hold.toePitch[FRONT_RIGHT] = 0.7f;
    hold.neckYaw = 0.25f;
    hold.headYaw = 0.45f;
    hold.headPitch = -0.35f;  // looks up to its left
    hold.face.lids = 0.45f;
    hold.face.gazeYaw = 0.15f;
    keys.push_back(at(hold, 2.5f));
    IdleKey content = hold;
    content.headYaw = 0.6f;
    content.headPitch = -0.25f;
    content.face.lids = 0.75f;
    content.sway = 0.07f;
    content.spineRoll = 0.05f;
    keys.push_back(at(content, 4.3f));
    IdleKey goofy = content;
    goofy.headYaw = 0.1f;
    goofy.headRoll = -0.34f;
    goofy.headPitch = -0.45f;
    goofy.face.jawOpen = 0.22f;
    goofy.face.tongueOut = 1.0f;
    goofy.face.tongueSide = 0.35f;
    goofy.face.tongueCurl = 0.45f;
    goofy.face.lids = 0.25f;
    goofy.face.gazeYaw = -0.1f;
    goofy.sway = 0.12f;
    keys.push_back(at(goofy, 5.0f));
    goofy.headRoll = -0.4f;
    goofy.sway = 0.14f;
    keys.push_back(at(goofy, 6.6f));
    IdleKey tongueIn = hold;
    tongueIn.headYaw = 0.0f;
    tongueIn.headPitch = -0.5f;
    tongueIn.neckYaw = 0.0f;
    tongueIn.face = FaceControls();
    tongueIn.face.lids = 0.15f;
    keys.push_back(at(tongueIn, 7.2f));
    IdleKey release = sitting;
    release.feet[FRONT_LEFT] = glm::vec3(0.3f, 0.62f, 0.14f);
    release.feet[FRONT_RIGHT] = glm::vec3(-0.3f, 0.62f, 0.14f);
    keys.push_back(at(release, 8.0f));
    keys.push_back(at(sitting, 8.8f));
    keys.push_back(at(standing, 9.7f));
  }

  // Yawning: sits down, takes a breath, opens its mouth wide with closed eyes
  // and the ears back, smacks its lips and looks around
  {
    std::vector<IdleKey>& keys = idleAnimations[IDLE_YAWN];
    keys.push_back(at(standing, 0.0f));
    keys.push_back(at(sitting, 0.9f));
    IdleKey inhale = sitting;
    inhale.headPitch = -0.35f;
    inhale.neckPitch = -0.5f;
    inhale.spinePitch = 0.35f;
    inhale.face.jawOpen = 0.3f;
    inhale.face.lids = 0.45f;
    inhale.face.earsBack = 0.35f;
    keys.push_back(at(inhale, 1.7f));
    IdleKey yawn = inhale;
    yawn.headPitch = -0.25f;  // head back
    yawn.spinePitch = 0.4f;
    yawn.face.jawOpen = 1.0f;
    yawn.face.lids = 1.0f;
    yawn.face.earsBack = 0.9f;
    yawn.face.tongueCurl = -0.9f;
    yawn.face.tongueOut = 0.2f;
    for (int leg : {FRONT_LEFT, FRONT_RIGHT}) {
      yawn.feet[leg] = glm::vec3(0.34f * legs[leg].side, 0.72f,
                                 0.28f);  // the front paws stretch
      yawn.toePitch[leg] = 0.9f;
    }
    keys.push_back(at(yawn, 2.6f));
    yawn.headRoll = 0.12f;
    keys.push_back(at(yawn, 3.4f));
    IdleKey close = sitting;
    close.headPitch = -0.45f;
    close.face.jawOpen = 0.12f;
    close.face.lids = 0.7f;
    close.face.earsBack = 0.25f;
    keys.push_back(at(close, 4.0f));
    close.face.jawOpen = 0.0f;
    keys.push_back(at(close, 4.2f));
    close.face.jawOpen = 0.1f;
    close.face.tongueOut = 0.12f;  // licks its lips
    keys.push_back(at(close, 4.4f));
    close.face.jawOpen = 0.0f;
    close.face.tongueOut = 0.0f;
    close.face.lids = 0.3f;
    keys.push_back(at(close, 4.65f));
    IdleKey lookLeft = sitting;
    lookLeft.headYaw = -0.45f;
    lookLeft.face.lids = 0.2f;
    lookLeft.face.gazeYaw = -0.2f;
    keys.push_back(at(lookLeft, 5.6f));
    IdleKey lookRight = sitting;
    lookRight.headYaw = 0.25f;
    lookRight.face.gazeYaw = 0.15f;
    keys.push_back(at(lookRight, 6.6f));
    keys.push_back(at(standing, 7.5f));
  }

  // Eating a salmon: sits down and takes the fish from its mouth into its front
  // paws, looks at it, takes three big bites (chewing happily with closed eyes
  // and rocking in between, the last one with the head back), licks its lips
  // and gets up
  {
    std::vector<IdleKey>& keys = idleAnimations[IDLE_EAT];
    IdleKey carry = standing;
    carry.face.jawOpen = 0.34f;  // the fish is still in its mouth
    keys.push_back(at(carry, 0.0f));
    IdleKey hold = sitting;
    hold.headPitch = -0.95f;  // looks at the fish
    hold.neckPitch = -0.6f;
    hold.face.gazePitch = -0.15f;
    hold.face.earsBack = 0.0f;
    for (int leg : {FRONT_LEFT, FRONT_RIGHT}) {
      hold.feet[leg] =
          glm::vec3(0.12f * legs[leg].side, 1.0f,
                    0.42f);  // the paws hold the fish in front of the chest
      hold.toePitch[leg] = 0.9f;
    }
    IdleKey take = hold;
    take.headPitch = -1.05f;
    take.face.jawOpen = 0.34f;
    keys.push_back(at(take, EAT_TAKE_START));
    take.face.jawOpen = 0.1f;
    keys.push_back(at(take, EAT_TAKE_END));
    IdleKey look = hold;
    look.headRoll = 0.18f;  // tilts its head: yummy
    look.face.gazeYaw = 0.05f;
    keys.push_back(at(look, 1.05f));

    for (int bite = 0; bite < 3; ++bite) {
      float time = EAT_BITES[bite];
      bool last = bite == 2;
      IdleKey open = hold;
      open.headPitch = last ? -0.2f : -0.55f;  // the last bite with the head
                                               // back, like gulping it down
      open.neckPitch = last ? -0.65f : -0.25f;
      open.face.jawOpen = last ? 0.85f : 0.65f;
      open.face.earsBack = 0.3f;
      for (int leg : {FRONT_LEFT, FRONT_RIGHT}) {
        open.feet[leg] = glm::vec3(0.1f * legs[leg].side, last ? 1.25f : 1.12f,
                                   0.4f);  // lifts the fish to its mouth
      }
      keys.push_back(at(open, time - 0.18f));
      IdleKey chomp = open;
      chomp.face.jawOpen = 0.0f;
      chomp.face.lids = 0.7f;
      chomp.headPitch += 0.08f;
      keys.push_back(at(chomp, time));
      // Chewing with closed eyes, rocking happily
      IdleKey chew = hold;
      chew.face.lids = 0.85f;
      chew.face.earsBack = 0.0f;
      chew.sway = 0.08f;
      chew.headPitch = -0.8f;
      float chewEnd = last ? time + 0.85f : EAT_BITES[bite + 1] - 0.3f;
      int chews = 0;
      for (float t = time + 0.14f; t < chewEnd - 0.05f; t += 0.14f) {
        chew.face.jawOpen = chews++ % 2 == 0 ? 0.17f : 0.02f;
        chew.headRoll = chews % 4 < 2 ? 0.1f : -0.06f;
        keys.push_back(at(chew, t));
      }
      chew.face.jawOpen = 0.0f;
      keys.push_back(at(chew, chewEnd));
    }
    // Licks its lips, left and right, with the paws on its belly
    IdleKey lick = sitting;
    lick.headPitch = -0.35f;
    lick.face.lids = 0.6f;
    lick.face.jawOpen = 0.14f;
    lick.face.tongueOut = 0.4f;
    lick.face.tongueSide = 0.7f;
    lick.face.tongueCurl = -0.4f;
    lick.sway = 0.06f;
    float lickStart = EAT_BITES[2] + 1.0f;
    keys.push_back(at(lick, lickStart + 0.2f));
    lick.face.tongueSide = -0.7f;
    keys.push_back(at(lick, lickStart + 0.55f));
    IdleKey content = sitting;
    content.face.lids = 0.9f;  // blissful
    content.headPitch = -0.3f;
    content.headRoll = -0.12f;
    content.sway = 0.1f;
    keys.push_back(at(content, lickStart + 0.85f));
    keys.push_back(at(content, lickStart + 1.25f));
    keys.push_back(at(sitting, lickStart + 1.6f));
    keys.push_back(at(standing, lickStart + 2.3f));
  }

  // Lying (a loop): looks around with its head up, then rests its chin on the
  // ground between its paws for a while with sleepy eyes and the ears relaxed,
  // sighs and lifts its head again
  {
    std::vector<IdleKey>& keys = idleAnimations[IDLE_LIE];
    keys.push_back(at(lying, 0.0f));
    keys.push_back(at(lying, 5.0f));
    IdleKey chin = lying;
    chin.neckPitch = -0.1f;
    chin.headPitch = -0.12f;
    chin.spinePitch = 0.0f;
    chin.face.lids = 0.6f;
    chin.face.earsBack = 0.2f;
    keys.push_back(at(chin, 6.2f));
    chin.face.lids = 0.75f;
    chin.sway = 0.02f;
    keys.push_back(at(chin, 9.5f));
    IdleKey sigh = chin;
    sigh.spinePitch = 0.08f;  // a deep breath
    sigh.face.jawOpen = 0.08f;
    sigh.face.lids = 0.5f;
    keys.push_back(at(sigh, 10.3f));
    keys.push_back(at(lying, 11.4f));
    keys.push_back(at(lying, 14.0f));
  }
}

BearRig::Controls BearRig::idleControls(const LocomotionInput& input,
                                        const GroundProbe& probe) {
  const std::vector<IdleKey>& keys = idleAnimations[idle];
  std::size_t next = 1;
  while (next + 1 < keys.size() && keys[next].time <= idleTime) {
    ++next;
  }
  const IdleKey& from = keys[next - 1];
  const IdleKey& to = keys[next];
  IdleKey key = IdleKey::mix(
      from, to,
      smooth((idleTime - from.time) / std::max(to.time - from.time, 1e-3f)));
  const bool lying = idle == IDLE_LIE;
  if (lying) {
    alert += ((looking ? 1.0f : 0.0f) - alert) *
             smoothingFactor(0.25f, lastTimeDelta);
    key = IdleKey::mix(key, keys.front(), alert);
  }

  Controls c;
  c.rootPosition = input.rootPosition;
  c.yaw = std::atan2(input.direction.x, input.direction.z);
  float rocking = key.sway * std::sin(2.0f * PI * 0.45f * idleTime);
  c.pelvisOffset.y = key.pelvisHeight;
  c.pelvisPitch = key.pelvisPitch;
  c.pelvisRoll = key.pelvisRoll + rocking;
  c.spinePitch =
      key.spinePitch + 0.02f * std::sin(2.0f * PI * 0.33f * time);  // breathing
  c.spineYaw = key.spineYaw;
  c.spineRoll = key.spineRoll - 0.6f * rocking;
  c.neckPitch = key.neckPitch;
  c.neckYaw = key.neckYaw;
  c.headPitch = key.headPitch;
  c.headYaw = key.headYaw;
  c.headRoll = key.headRoll - 0.4f * rocking;
  if (lying) {
    c.neckYaw += 0.35f * lookYaw;
    c.headYaw += 0.65f * lookYaw;
    c.headPitch += lookPitch;
  }
  c.face = key.face;
  c.face.lids = std::max(c.face.lids, blinkAmount());
  c.face.gazeYaw += 0.5f * gazeYaw;
  c.face.gazePitch += 0.5f * gazePitch;

  glm::mat4 frame = bodyFrame(c);
  glm::vec3 bodyUp = glm::vec3(frame * glm::vec4(UP, 0.0f));
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    glm::vec3 foot = glm::vec3(frame * glm::vec4(key.feet[leg], 1.0f));
    glm::vec3 normal = bodyUp;
    glm::vec3 ground, groundNormal;
    if (key.grounded[leg] > 0.0f &&
        probeGround(probe, foot, ground, groundNormal)) {
      foot = glm::mix(foot, ground, key.grounded[leg]);
      normal =
          glm::normalize(glm::mix(bodyUp, groundNormal, key.grounded[leg]));
    }
    c.feet[leg] = foot;
    c.footNormals[leg] = normal;
    c.toePitch[leg] = key.toePitch[leg];
  }
  return c;
}

std::vector<physics::RagdollPart> BearRig::ragdollParts() const {
  std::vector<physics::RagdollPart> parts(PART_COUNT);
  if (!isValid()) {
    return {};
  }
  auto position = [&](int bone) { return positionOf(bindModel[bone]); };
  auto setPart = [&](int index, int parent, glm::vec3 start, glm::vec3 end,
                     float radius, float mass) {
    physics::RagdollPart& part = parts[index];
    part.parent = parent;
    part.bindTransform = rigid(bindModel[partBones[index]]);
    part.capsuleStart = start;
    part.capsuleEnd = end;
    part.radius = radius;
    part.mass = mass;
  };
  auto ballJoint = [&](int index, glm::vec3 position, glm::vec3 along,
                       glm::vec3 plane, float swing, float twist) {
    physics::RagdollJoint& joint = parts[index].joint;
    joint.type = physics::RagdollJoint::SWING_TWIST;
    joint.position = position;
    joint.twistAxis = along;
    joint.planeAxis = plane;
    joint.swingAngle = swing;
    joint.twistAngle = twist;
    joint.friction = 2.0f;
  };
  // Elbows bend so that the paw goes forward, knees so that the foot goes
  // backward (rotation around the left axis +X)
  auto hingeJoint = [&](int index, glm::vec3 position, glm::vec3 along,
                        float minAngle, float maxAngle) {
    physics::RagdollJoint& joint = parts[index].joint;
    joint.type = physics::RagdollJoint::HINGE;
    joint.position = position;
    joint.twistAxis = LEFT;
    joint.planeAxis = along;
    joint.minAngle = minAngle;
    joint.maxAngle = maxAngle;
    joint.friction = 0.5f;
  };
  const glm::vec3 forward(0.0f, 0.0f, 1.0f);

  glm::vec3 spinePosition = position(spine), chestPosition = position(chest),
            neckPosition = position(neck), headPosition = position(head);
  setPart(PART_PELVIS, -1, position(pelvis) + glm::vec3(0.0f, -0.04f, -0.08f),
          spinePosition, 0.45f, 9.0f);
  setPart(PART_CHEST, PART_PELVIS, chestPosition, neckPosition, 0.44f, 9.0f);
  ballJoint(PART_CHEST, 0.5f * (spinePosition + chestPosition),
            glm::normalize(chestPosition - spinePosition), UP, 0.45f, 0.3f);
  setPart(PART_HEAD, PART_CHEST, headPosition + glm::vec3(0.0f, 0.18f, 0.1f),
          headPosition + glm::vec3(0.0f, 0.06f, 0.42f), 0.4f, 5.0f);
  ballJoint(PART_HEAD, 0.5f * (neckPosition + headPosition),
            glm::normalize(headPosition - neckPosition), UP, 0.7f, 0.5f);

  const int upperParts[4] = {PART_UPPERARM_L, PART_UPPERARM_R, PART_THIGH_L,
                             PART_THIGH_R};
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    const LegRig& rig = legs[leg];
    int upperPart = upperParts[leg], lowerPart = upperPart + 1;
    glm::vec3 upper = position(rig.upper), lower = position(rig.lower),
              paw = position(rig.paw);
    glm::vec3 toes = paw + glm::vec3(0.0f, -0.03f, rig.front ? 0.1f : 0.12f);
    setPart(upperPart, rig.front ? PART_CHEST : PART_PELVIS, upper, lower,
            rig.front ? 0.17f : 0.2f, rig.front ? 1.6f : 2.2f);
    ballJoint(upperPart, upper, glm::normalize(lower - upper), forward, 0.9f,
              0.3f);
    setPart(lowerPart, upperPart, lower, toes, rig.front ? 0.15f : 0.14f,
            rig.front ? 1.2f : 1.3f);
    if (rig.front) {
      hingeJoint(lowerPart, lower, glm::normalize(paw - lower), -1.6f, 0.2f);
    } else {
      hingeJoint(lowerPart, lower, glm::normalize(paw - lower), -0.2f, 1.6f);
    }
  }
  return parts;
}

void BearRig::ragdollTransforms(std::vector<glm::mat4>& transforms) const {
  transforms.resize(PART_COUNT);
  for (int i = 0; i < PART_COUNT; ++i) {
    transforms[i] = rigid(world[partBones[i]]);
  }
}

void BearRig::ragdollVelocities(std::vector<glm::vec3>& velocities) const {
  velocities.assign(PART_COUNT, glm::vec3(0.0f));
  if (lastTimeDelta <= 0.0f || previousWorld.size() != world.size()) {
    return;
  }
  for (int i = 0; i < PART_COUNT; ++i) {
    velocities[i] = (positionOf(world[partBones[i]]) -
                     positionOf(previousWorld[partBones[i]])) /
                    lastTimeDelta;
  }
}

void BearRig::updateFromRagdoll(float timeDelta,
                                const std::vector<glm::mat4>& partTransforms) {
  if (!isValid() || partTransforms.size() != PART_COUNT) {
    return;
  }
  previousWorld = world;
  lastTimeDelta = timeDelta;
  time += timeDelta;
  idle = -1;
  blendTime = 0.0f;
  // Dazed: eyes shut, the mouth a bit open and the tongue flopping out
  FaceControls face;
  face.lids = 1.0f;
  face.jawOpen = 0.18f;
  face.tongueOut = 0.35f;
  face.tongueCurl = 0.3f;
  face.earsBack = 0.4f;
  controls.face = face;

  // Spine and neck are between two ragdoll parts: each of their two bones turns
  // half of the way
  glm::quat pendingRotation(1.0f, 0.0f, 0.0f, 0.0f);
  auto splitBetween = [&](int first, int second, const glm::mat4& target) {
    glm::quat firstBind = bind[first].rotation,
              secondBind = bind[second].rotation;
    glm::quat atBind = rotationOf(parentWorld(first)) * firstBind * secondBind;
    glm::quat half =
        glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                   glm::inverse(atBind) * rotationOf(target), 0.5f);
    pose[first].rotation = glm::normalize(
        firstBind * (secondBind * half * glm::inverse(secondBind)));
    pendingRotation = glm::normalize(secondBind * half);
  };

  for (int i = 0; i < static_cast<int>(pose.size()); ++i) {
    // The face starts from its bind pose (applyFace turns it from there)
    bool faceBone = i == jaw || i == tongue[0] || i == tongue[1] ||
                    i == tongue[2] || i == earLeft || i == earRight ||
                    i == eyes[0].eye || i == eyes[0].upperLid ||
                    i == eyes[0].lowerLid || i == eyes[1].eye ||
                    i == eyes[1].upperLid || i == eyes[1].lowerLid;
    if (faceBone) {
      pose[i] = bind[i];
    }
    if (i == pelvis) {
      pose[i] = Transform::fromMatrix(glm::inverse(parentWorld(i)) *
                                      rigid(partTransforms[PART_PELVIS]));
    } else if (i == spine) {
      splitBetween(spine, chest, partTransforms[PART_CHEST]);
    } else if (i == neck) {
      splitBetween(neck, head, partTransforms[PART_HEAD]);
    } else if (i == chest || i == head) {
      pose[i].rotation = pendingRotation;
    } else {
      for (int part = PART_UPPERARM_L; part < PART_COUNT; ++part) {
        if (partBones[part] == i) {
          pose[i].rotation =
              glm::normalize(glm::inverse(rotationOf(parentWorld(i))) *
                             rotationOf(partTransforms[part]));
        }
      }
    }
    forwardKinematics(i);
    if (i == tail) {
      updateSprings(i, tailSpring, TAIL_LENGTH, 140.0f, 7.0f, 4.0f, 1.0f,
                    timeDelta);
    } else if (faceBone) {
      applyFace(i, face, timeDelta);
    }
  }
}

BearRig::GetUpStart BearRig::beginGetUp(const GroundProbe& probe,
                                        glm::vec3 previousDirection) {
  glm::vec3 pelvisPosition = positionOf(world[pelvis]),
            chestPosition = positionOf(world[chest]);
  glm::vec3 along = chestPosition - pelvisPosition;
  glm::vec3 horizontal(along.x, 0.0f, along.z);
  glm::vec3 direction =
      glm::length(horizontal) > 0.3f * glm::length(along)
          ? glm::normalize(horizontal)
          : glm::normalize(
                glm::vec3(previousDirection.x, 0.0f, previousDirection.z));

  // Ground under the body: the ray starts inside of the body, so that it does
  // not hit an obstacle the bear lies against
  glm::vec3 center = 0.5f * (pelvisPosition + chestPosition);
  glm::vec3 ground, normal;
  glm::vec3 rootPosition =
      probe && probe(center + UP * 0.1f, center - UP * 2.5f, ground, normal)
          ? ground
          : center - UP * 0.5f;

  // How far the body is rolled around its length axis (0: on the belly, +-pi:
  // on the back)
  glm::vec3 left = glm::cross(UP, direction);
  glm::vec3 bodyUp =
      rotationOf(world[pelvis]) * (glm::inverse(pelvisBindRotation) * UP);
  glm::vec3 projected = bodyUp - direction * glm::dot(bodyUp, direction);
  getUpRoll = std::atan2(-glm::dot(projected, left), glm::dot(projected, UP));
  getUpRollsOver = std::abs(getUpRoll) > 0.75f;
  getUpTime = 0.0f;

  Controls c;
  c.rootPosition = rootPosition;
  c.yaw = std::atan2(direction.x, direction.z);
  c.bodyRoll = getUpRoll;
  c.face = controls.face;
  controls = c;
  // The root bone was still where the bear ran before it fell: it moves to
  // where it gets up, so that only the bones below it carry the pose of the
  // ragdoll (blending a far away root would sweep the body through the ground)
  pose[root] = bind[root];
  forwardKinematics(root);
  getUpSnapshot = skeleton.localPose(world, bodyFrame(c));
  return {rootPosition, direction};
}

namespace {
const float SETTLE_TIME = 0.35f;
const float ROLL_TIME = 0.6f;
const float PUSH_TIME = 0.4f;
const float STAND_TIME = 0.35f;
const float SHAKE_TIME = 0.75f;
}  // namespace

float BearRig::getGetUpDuration() const {
  return SETTLE_TIME + (getUpRollsOver ? ROLL_TIME : 0.0f) + PUSH_TIME +
         STAND_TIME + SHAKE_TIME;
}

bool BearRig::updateGetUp(float timeDelta, const GroundProbe& probe,
                          glm::vec3 rootPosition) {
  if (!isValid()) {
    return true;
  }
  previousWorld = world;
  lastTimeDelta = timeDelta;
  time += timeDelta;
  getUpTime += timeDelta;
  updateSecondary(timeDelta, false);

  // Key poses, the feet relative to the body (model space offsets from where
  // they are in the bind pose)
  struct Key {
    float pelvisHeight, pelvisPitch, neckPitch, headPitch;
    glm::vec3 front, hind;
    FaceControls face;
  };
  FaceControls dazed;
  dazed.lids = 0.7f;
  dazed.jawOpen = 0.12f;
  dazed.tongueOut = 0.2f;
  dazed.earsBack = 0.3f;
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
      STAND_HEIGHT,    0.0f,          0.0f, 0.0f, glm::vec3(0.0f),
      glm::vec3(0.0f), FaceControls()};
  auto mix = [](const Key& a, const Key& b, float t) {
    return Key{glm::mix(a.pelvisHeight, b.pelvisHeight, t),
               glm::mix(a.pelvisPitch, b.pelvisPitch, t),
               glm::mix(a.neckPitch, b.neckPitch, t),
               glm::mix(a.headPitch, b.headPitch, t),
               glm::mix(a.front, b.front, t),
               glm::mix(a.hind, b.hind, t),
               FaceControls::mix(a.face, b.face, t)};
  };

  const float rollEnd = SETTLE_TIME + (getUpRollsOver ? ROLL_TIME : 0.0f);
  const float pushEnd = rollEnd + PUSH_TIME;
  const float standEnd = pushEnd + STAND_TIME;
  const float shakeEnd = standEnd + SHAKE_TIME;
  const float t = getUpTime;

  Controls c;
  c.rootPosition = rootPosition;
  c.yaw = controls.yaw;
  Key key = lying;
  float roll = getUpRoll;
  float kick = 0.0f, shake = 0.0f, shakePhase = 0.0f;
  if (t < SETTLE_TIME) {
    roll = getUpRoll;
  } else if (t < rollEnd) {
    // On the back or the side: rolls over onto the belly, kicking with the legs
    float u = (t - SETTLE_TIME) / ROLL_TIME;
    roll = getUpRoll * (1.0f - smooth(u));
    kick = std::sin(PI * u);
  } else if (t < pushEnd) {
    roll = 0.0f;
    key = mix(lying, pushed, smooth((t - rollEnd) / PUSH_TIME));
  } else if (t < standEnd) {
    roll = 0.0f;
    key = mix(pushed, standing, smooth((t - pushEnd) / STAND_TIME));
  } else {
    // Shakes the snow out of its fur
    roll = 0.0f;
    key = standing;
    float u = std::min((t - standEnd) / SHAKE_TIME, 1.0f);
    shakePhase = 2.0f * PI * 4.0f * u;
    shake = (1.0f - u) * std::min(1.0f, 4.0f * u);
  }
  c.bodyRoll = roll;
  c.pelvisOffset.y = key.pelvisHeight;
  c.pelvisPitch = key.pelvisPitch;
  c.neckPitch = key.neckPitch;
  c.headPitch = key.headPitch;
  c.pelvisRoll = 0.3f * shake * std::sin(shakePhase);
  c.spineRoll = -0.4f * shake * std::sin(shakePhase);
  c.headRoll = -0.5f * shake * std::sin(shakePhase + 0.6f);
  c.neckYaw = 0.2f * shake * std::sin(shakePhase + 1.2f);
  c.face = key.face;
  c.face.lids = std::max({c.face.lids, 0.8f * shake, blinkAmount()});
  c.face.earsBack = std::max(c.face.earsBack, 0.5f * shake);
  c.face.gazeYaw = gazeYaw;
  c.face.gazePitch = gazePitch;

  glm::mat4 frame = bodyFrame(c);
  glm::vec3 bodyUp = rotationOf(frame) * UP;
  for (int leg = 0; leg < LEG_COUNT; ++leg) {
    const LegRig& rig = legs[leg];
    glm::vec3 offset = rig.front ? key.front : key.hind;
    offset.x *= rig.side;
    float wave = 2.0f * PI * 3.0f * t + (rig.front ? 0.0f : PI) +
                 (rig.side > 0.0f ? 0.0f : PI * 0.5f);
    offset +=
        kick * glm::vec3(0.0f, 0.14f * std::sin(wave), 0.08f * std::cos(wave));
    glm::vec3 foot = glm::vec3(frame * glm::vec4(rig.neutral + offset, 1.0f));
    glm::vec3 normal = bodyUp;
    glm::vec3 ground, groundNormal;
    if (roll == 0.0f && probeGround(probe, foot, ground, groundNormal)) {
      foot = ground;
      normal = groundNormal;
    }
    c.feet[leg] = foot;
    c.footNormals[leg] = normal;
  }

  evaluate(bind, c, timeDelta);
  // At first the pose of the ragdoll blends into the animation
  float blend = smooth(t / SETTLE_TIME);
  if (blend < 1.0f && getUpSnapshot.size() == pose.size()) {
    for (std::size_t i = 0; i < pose.size(); ++i) {
      pose[i] = Transform::mix(getUpSnapshot[i], pose[i], blend);
      forwardKinematics(static_cast<int>(i));
    }
  }

  if (t >= shakeEnd) {
    // Walking starts from where the feet are now
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
      feet[leg].position = feet[leg].planted = feet[leg].swingFrom =
          c.feet[leg];
      feet[leg].normal = c.footNormals[leg];
      feet[leg].swinging = false;
    }
    gaitPhase = 0.0f;
    bodyHeight = bodyPitch = lean = bend = 0.0f;
    airborneTime = 0.0f;
    launchTime = -1.0f;
    airPose = reach = 0.0f;
    landingBlend = 1.0f;
    return true;
  }
  return false;
}

void BearRig::skinMatrices(std::vector<glm::mat4>& result) const {
  skeleton.skinMatrices(world, result);
}

glm::vec3 BearRig::getCenter() const {
  return 0.5f * (positionOf(world[pelvis]) + positionOf(world[chest]));
}

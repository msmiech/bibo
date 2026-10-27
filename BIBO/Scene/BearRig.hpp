#pragma once
#include <array>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <string>
#include <vector>

#include "../Animation/Skeleton.hpp"
#include "../Physics/Physics.hpp"

namespace bibo {
namespace scene {
/**
 * Finds the ground between two points (e.g. a ray cast into the physics world)
 * @return false if there is no ground on the way
 */
using GroundProbe = std::function<bool(glm::vec3 from, glm::vec3 to,
                                       glm::vec3& point, glm::vec3& normal)>;

/**
 * How the character moves (from the character controller)
 */
struct LocomotionInput {
  // on the ground under the center of the body
  glm::vec3 rootPosition = glm::vec3(0.0f);
  // where the bear looks (horizontal)
  glm::vec3 direction = glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 velocity = glm::vec3(0.0f);
  float yawRate = 0.0f;  // radians per second, positive: turning left
  bool grounded = true;
  // took off for a jump in this frame (not when it walks over an edge)
  bool jumped = false;
  bool swimming = false;  // floating in deep water (the paws paddle)
};

/**
 * Expression of the face
 */
struct FaceControls {
  float jawOpen = 0.0f;     // 0: mouth closed, 1: wide open (yawning)
  float tongueOut = 0.0f;   // 0: in the mouth, 1: sticking out
  float tongueCurl = 0.0f;  // positive: the tip curls down, negative: up
  float tongueSide = 0.0f;  //-1 right, 1 left
  float lids = 0.0f;        // 0: eyes open, 1: closed
  float gazeYaw = 0.0f,
        // where the eyes look (radians, relative to the head)
      gazePitch = 0.0f;
  float earsBack = 0.0f;  // 0: up, 1: flat on the head

  static FaceControls mix(const FaceControls& a, const FaceControls& b,
                          float t);
};

/**
 * Procedural animation of the bear cub: no animation clips, the pose is
 * computed every frame.
 * - Walking and bounding: the feet are planted on the ground where they land
 * (found with the ground probe) and stay there until they step again, the legs
 * follow with two bone inverse kinematics (thigh/shin, upperarm/forearm). The
 * body bobs with the steps, follows the slope of the ground, leans into turns
 * and the spine bends into them.
 * - Face: the jaw opens, the tongue comes out, the eyes look around (saccades)
 * and blink with their eyelids, the upper lids follow the gaze, the ears fold
 * back.
 * - Secondary motion: ears and tail are springs, the head looks around when the
 * bear stands.
 * - Idle animations when the bear stands still for a while (startIdle):
 * stretching (sits down, pulls a hind paw to its chest and sticks its tongue
 * out) and yawning (sits down and yawns). Key poses of the body, the feet and
 * the face. Eating a salmon works the same way: it sits down, holds the fish
 * with its front paws, takes three big bites, chews happily with closed eyes
 * and licks its lips. Lying on the belly like a sphinx (for a bear that rests
 * and watches something): the head looks around or follows lookAt, now and then
 * it rests its chin a while.
 * - Swimming: dog paddle, the paws go round in circles under the body (the
 * front and the hind legs of opposite sides together), the head stays up out of
 * the water, the body rocks with the strokes.
 * - Ragdoll: the bones follow the bodies of the ragdoll (ragdollParts), the
 * bones in between are interpolated.
 * - Getting up: from the pose of the ragdoll the bear rolls onto its belly,
 * pushes itself up and shakes its fur.
 *
 * The model looks along +Z with +Y up in model space (glTF), its left side is
 * +X.
 */
class BearRig {
 public:
  enum Leg { FRONT_LEFT, FRONT_RIGHT, HIND_LEFT, HIND_RIGHT, LEG_COUNT };
  /**
   * Parts of the ragdoll (index into ragdollParts)
   */
  enum RagdollPartIndex {
    PART_PELVIS,
    PART_CHEST,
    PART_HEAD,
    PART_UPPERARM_L,
    PART_FOREARM_L,
    PART_UPPERARM_R,
    PART_FOREARM_R,
    PART_THIGH_L,
    PART_SHIN_L,
    PART_THIGH_R,
    PART_SHIN_R,
    PART_COUNT
  };
  enum Idle { IDLE_STRETCH, IDLE_YAWN, IDLE_EAT, IDLE_LIE, IDLE_COUNT };
  /**
   * The idle animations the bear plays on its own when it stands still (the
   * first ones of Idle)
   */
  static constexpr int RESTING_IDLE_COUNT = 2;
  /**
   * Times in the eating animation (seconds) when the fish moves from the mouth
   * into the paws, and the three bites
   */
  static constexpr float EAT_TAKE_START = 0.3f, EAT_TAKE_END = 0.85f;
  static constexpr float EAT_BITES[3] = {1.35f, 2.35f, 3.3f};
  /**
   * The mouth of the model is open in its bind pose (the inside is modelled
   * with it), the jaw closes it by this angle (degrees)
   */
  static constexpr float JAW_BIND_OPEN_DEGREES = 24.0f;

  /**
   * @param skeleton Must stay alive as long as the rig
   */
  explicit BearRig(const animation::Skeleton& skeleton);

  /**
   * @return false if the skeleton lacks a bone the rig needs (see
   * getMissingBones)
   */
  bool isValid() const { return missingBones.empty(); }
  const std::string& getMissingBones() const { return missingBones; }

  /**
   * Stands still with all feet on the ground (e.g. at the start)
   */
  void reset(const LocomotionInput& input, const GroundProbe& probe);
  /**
   * Walking, running, jumping, standing and the idle animations
   */
  void updateLocomotion(float timeDelta, const LocomotionInput& input,
                        const GroundProbe& probe);

  /**
   * Starts an idle animation (the bear must stand still while it plays). Lying
   * (IDLE_LIE) starts lying on the belly and goes on until stopIdle, the head
   * follows lookAt meanwhile.
   */
  void startIdle(Idle idle);
  /**
   * Ends the idle animation, the pose blends into walking (e.g. when the player
   * moves the bear)
   */
  void stopIdle();
  bool isIdling() const { return idle >= 0; }
  /**
   * @return The idle animation that plays, -1 if none
   */
  int getIdle() const { return idle; }
  float getIdleDuration(Idle idle) const;
  /**
   * @return Seconds since the idle animation started
   */
  float getIdleTime() const { return idleTime; }
  /**
   * Holding something in the mouth (e.g. a fish) while it walks, runs and
   * jumps: the mouth stays a bit open
   */
  void setCarrying(bool carrying) { this->carrying = carrying; }
  /**
   * Opens the mouth at least this far (0 to 1), e.g. while it talks
   */
  void setMouthOpen(float amount) { mouthOpen = amount; }
  /**
   * The head turns to a point and the eyes look at it (e.g. the face of an
   * animal it talks to) instead of looking around
   */
  void lookAt(glm::vec3 target);
  /**
   * The head and the eyes look around again
   */
  void stopLooking() { looking = false; }
  bool isLooking() const { return looking; }
  /**
   * Position of the head bone (world space)
   */
  glm::vec3 getHeadPosition() const { return getBonePosition(head); }

  /**
   * Bodies and joints of the ragdoll in the bind pose (model space)
   */
  std::vector<physics::RagdollPart> ragdollParts() const;
  /**
   * World transforms of the ragdoll parts in the current pose, to start the
   * ragdoll where the bear is
   */
  void ragdollTransforms(std::vector<glm::mat4>& transforms) const;
  /**
   * Velocities of the ragdoll parts in the current animation (from the last two
   * frames)
   */
  void ragdollVelocities(std::vector<glm::vec3>& velocities) const;
  /**
   * The bones follow the bodies of the ragdoll
   */
  void updateFromRagdoll(float timeDelta,
                         const std::vector<glm::mat4>& partTransforms);

  struct GetUpStart {
    glm::vec3 rootPosition;  // where the bear stands up (on the ground)
    glm::vec3 direction;     // where it looks then
  };
  /**
   * Starts getting up from the current pose (when the ragdoll lies still)
   * @param previousDirection Used if the body is upright (the direction cannot
   * be found from the body)
   */
  GetUpStart beginGetUp(const GroundProbe& probe, glm::vec3 previousDirection);
  /**
   * @param rootPosition Where the bear stands (it may be pushed while it gets
   * up)
   * @return true when the bear stands again
   */
  bool updateGetUp(float timeDelta, const GroundProbe& probe,
                   glm::vec3 rootPosition);
  /**
   * @return Duration of the get up animation that was started last (seconds)
   */
  float getGetUpDuration() const;

  /**
   * World transforms of all bones in the current pose
   */
  const std::vector<glm::mat4>& getWorldMatrices() const { return world; }
  void skinMatrices(std::vector<glm::mat4>& result) const;
  glm::vec3 getBonePosition(int bone) const {
    return glm::vec3(world[bone][3]);
  }
  int getBone(const std::string& name) const { return skeleton.find(name); }
  /**
   * Center of the body (between pelvis and chest)
   */
  glm::vec3 getCenter() const;
  /**
   * Where the paw of a leg touches the ground (or would touch it)
   */
  glm::vec3 getFootTarget(Leg leg) const { return feet[leg].position; }
  bool isFootPlanted(Leg leg) const { return !feet[leg].swinging; }
  /**
   * Height of the paw bones above the ground in the bind pose
   */
  float getPawHeight(Leg leg) const { return legs[leg].pawHeight; }
  /**
   * Expression of the face in the current pose (including blinking)
   */
  const FaceControls& getFace() const { return controls.face; }

 private:
  struct Controls {
    glm::vec3 rootPosition = glm::vec3(0.0f);
    float yaw = 0.0f;
    // whole body around its length axis (lying on the side or back)
    float bodyRoll = 0.0f;
    glm::vec3 pelvisOffset = glm::vec3(0.0f);  // in the frame of the body
    float pelvisPitch = 0.0f, pelvisYaw = 0.0f,
          // pitch: nose up, yaw: left, roll: right side down
        pelvisRoll = 0.0f;
    float spinePitch = 0.0f, spineYaw = 0.0f,
          spineRoll = 0.0f;  // split between spine and chest
    float neckPitch = 0.0f, neckYaw = 0.0f;
    float headPitch = 0.0f, headYaw = 0.0f, headRoll = 0.0f;
    // world positions where the paws are on the ground
    std::array<glm::vec3, LEG_COUNT> feet{};
    std::array<glm::vec3, LEG_COUNT> footNormals{};
    // paw tilted (toes down) while the leg swings
    std::array<float, LEG_COUNT> toePitch{};
    FaceControls face;
  };

  /**
   * Key pose of an idle animation, in the frame of the body
   */
  struct IdleKey {
    float time = 0.0f;  // seconds from the start
    float pelvisHeight = 0.0f, pelvisPitch = 0.0f, pelvisRoll = 0.0f;
    float spinePitch = 0.0f, spineYaw = 0.0f, spineRoll = 0.0f;
    float neckPitch = 0.0f, neckYaw = 0.0f;
    float headPitch = 0.0f, headYaw = 0.0f, headRoll = 0.0f;
    // where the paws are, in the frame of the body
    std::array<glm::vec3, LEG_COUNT> feet{};
    // 1: on the ground (found with the probe), 0: in the air
    std::array<float, LEG_COUNT> grounded{};
    std::array<float, LEG_COUNT> toePitch{};
    FaceControls face;
    float sway = 0.0f;  // slow rocking from side to side

    static IdleKey mix(const IdleKey& a, const IdleKey& b, float t);
  };

  struct LegRig {
    int upper = -1, lower = -1, paw = -1;
    float upperLength = 0.0f, lowerLength = 0.0f;
    float pawHeight = 0.0f;
    // ground point under the paw in the bind pose (model space)
    glm::vec3 neutral = glm::vec3(0.0f);
    glm::quat pawRotation =
        glm::quat(1.0f, 0.0f, 0.0f,
                  0.0f);  // model space rotation of the paw in the bind pose
    bool front = false;
    float side = 1.0f;  //+1 left, -1 right
  };

  struct Foot {
    // current ground point of the paw (world)
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 planted = glm::vec3(0.0f);
    glm::vec3 swingFrom = glm::vec3(0.0f);
    bool swinging = false;
    float swingProgress = 0.0f;
  };

  struct Eye {
    int eye = -1, upperLid = -1, lowerLid = -1;
    // model space axis the upper lid closes around (the lower one the other
    // way)
    glm::vec3 lidAxis = glm::vec3(1.0f, 0.0f, 0.0f);
  };

  glm::mat4 bodyFrame(const Controls& controls) const;
  /**
   * Computes pose and world from the base pose and the controls
   */
  void evaluate(const animation::Pose& base, const Controls& controls,
                float timeDelta);
  /**
   * Moves the bones of the face (after the bone was placed with forward
   * kinematics)
   */
  void applyFace(int bone, const FaceControls& face, float timeDelta);
  /**
   * Rotation of a bone relative to its bind pose in model space (for rotations
   * around the model axes of the head)
   */
  glm::quat boneFrame(int bone) const;
  void updateSprings(int bone, animation::SpringPoint& spring, float length,
                     float stiffness, float damping, float gravity,
                     float maxAngle, float timeDelta);
  void rotateBone(int bone, const glm::quat& worldDelta);
  glm::mat4 parentWorld(int bone) const;
  void forwardKinematics(int bone);
  bool probeGround(const GroundProbe& probe, glm::vec3 point, glm::vec3& ground,
                   glm::vec3& normal) const;
  void plantFeet(const GroundProbe& probe, const Controls& controls);
  /**
   * Landing: the paws are put on the ground under where they are in the air
   * (close to where they stand)
   */
  void landFeet(const GroundProbe& probe, const Controls& controls);
  /**
   * Legs in the air: pushing off, tucked in, reaching for the ground before
   * landing
   */
  void updateAirborne(float timeDelta, const LocomotionInput& input,
                      const GroundProbe& probe, Controls& controls);
  /**
   * Swimming: the paws paddle in circles relative to the body
   */
  void updateSwimming(float timeDelta, const LocomotionInput& input,
                      Controls& controls);
  void updateSecondary(float timeDelta, bool moving);
  float blinkAmount() const;
  /**
   * Controls of the idle animation that plays
   */
  Controls idleControls(const LocomotionInput& input, const GroundProbe& probe);
  IdleKey standingKey() const;
  IdleKey sittingKey() const;
  IdleKey lyingKey() const;
  void buildIdleAnimations();

  const animation::Skeleton& skeleton;
  std::string missingBones;
  int root, pelvis, spine, chest, neck, head, jaw, earLeft, earRight, tail;
  std::array<int, 3> tongue{};
  std::array<Eye, 2> eyes;
  std::array<LegRig, LEG_COUNT> legs;
  std::array<int, PART_COUNT> partBones{};
  std::vector<glm::mat4> bindModel;
  std::vector<glm::quat> bindRotation;
  glm::quat pelvisBindRotation;
  glm::vec3 tongueDirection = glm::vec3(
      0.0f, 0.0f,
      1.0f);  // model space, from the root to the tip in the bind pose
  // of the tongue bones from the root along the tongue
  std::array<float, 3> tongueBoneDistance{};

  animation::Pose bind;
  animation::Pose pose;
  std::vector<glm::mat4> world;
  std::vector<glm::mat4> previousWorld;
  float lastTimeDelta = 0.0f;
  Controls controls;

  // Locomotion
  std::array<Foot, LEG_COUNT> feet;
  float gaitPhase = 0.0f;
  float gallop = 0.0f;  // 0: walk, 1: bound
  float bodyHeight = 0.0f, bodyPitch = 0.0f, lean = 0.0f, bend = 0.0f;
  float airborneTime = 0.0f;
  float lastVerticalSpeed = 0.0f;
  // since the jump took off (the hind legs push off), -1 if the bear did not
  // jump
  float launchTime = -1.0f;
  float airPose = 0.0f;  // 0: on the ground, 1: legs in the air pose
  // 0: legs tucked, 1: reaching for the ground before landing
  float reach = 0.0f;
  // paws in the air, relative to the root (they move with the body)
  std::array<glm::vec3, LEG_COUNT> airOffsets{};
  // 0 right after landing: the steps start smoothly from where the paws landed
  float landingBlend = 1.0f;
  float swimPhase = 0.0f;   // strokes (1 per cycle of every leg)
  float swimEffort = 0.0f;  // 0 treading water, 1 swimming fast
  bool wasSwimming = false;
  animation::SpringValue squash;
  // nose up when taking off, down when the front legs take the landing
  animation::SpringValue pitchKick;
  std::array<animation::SpringPoint, 2> earSprings;
  animation::SpringPoint tailSpring;

  // Secondary motion
  std::mt19937 random{4711};
  float time = 0.0f;
  float lookYaw = 0.0f, lookPitch = 0.0f, lookTargetYaw = 0.0f,
        lookTargetPitch = 0.0f, lookTimer = 2.0f;
  float gazeYaw = 0.0f, gazePitch = 0.0f, gazeTargetYaw = 0.0f,
        gazeTargetPitch = 0.0f, saccadeTimer = 1.0f;
  float blinkTimer = 3.0f, blinkProgress = -1.0f;
  float earTwitchTimer = 4.0f;
  bool carrying = false;
  float mouthOpen = 0.0f;
  // lying: 0 the key poses (resting its chin), 1 the head up (looking at
  // something)
  float alert = 0.0f;
  bool looking = false;
  glm::vec3 lookTarget = glm::vec3(0.0f);
  float fixationYaw = 0.0f,
        // tiny jumps of the eyes around the point they look at
      fixationPitch = 0.0f;

  // Idle animations
  std::array<std::vector<IdleKey>, IDLE_COUNT> idleAnimations;
  int idle = -1;
  float idleTime = 0.0f;
  float blendTime = 0.0f;  // blends from blendSnapshot into the animation
  animation::Pose blendSnapshot;

  // Getting up
  animation::Pose getUpSnapshot;
  float getUpTime = 0.0f;
  float getUpRoll = 0.0f;
  bool getUpRollsOver = false;
};
}  // namespace scene
}  // namespace bibo

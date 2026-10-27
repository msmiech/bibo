#ifndef BIBO_SCENE_BEARRIG_HPP_
#define BIBO_SCENE_BEARRIG_HPP_
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
  glm::vec3 root_position = glm::vec3(0.0f);
  // where the bear looks (horizontal)
  glm::vec3 direction = glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 velocity = glm::vec3(0.0f);
  float yaw_rate = 0.0f;  // radians per second, positive: turning left
  bool grounded = true;
  // took off for a jump in this frame (not when it walks over an edge)
  bool jumped = false;
  bool swimming = false;  // floating in deep water (the paws paddle)
};

/**
 * Expression of the face
 */
struct FaceControls {
  float jaw_open = 0.0f;     // 0: mouth closed, 1: wide open (yawning)
  float tongue_out = 0.0f;   // 0: in the mouth, 1: sticking out
  float tongue_curl = 0.0f;  // positive: the tip curls down, negative: up
  float tongue_side = 0.0f;  //-1 right, 1 left
  float lids = 0.0f;         // 0: eyes open, 1: closed
  float gaze_yaw = 0.0f,
        // where the eyes look (radians, relative to the head)
      gaze_pitch = 0.0f;
  float ears_back = 0.0f;  // 0: up, 1: flat on the head

  static FaceControls Mix(const FaceControls& a, const FaceControls& b,
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
 * - Idle animations when the bear stands still for a while (StartIdle):
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
 * - Ragdoll: the bones follow the bodies of the ragdoll (RagdollParts), the
 * bones in between are interpolated.
 * - Getting up: from the pose of the ragdoll the bear rolls onto its belly,
 * pushes itself up and shakes its fur.
 *
 * The model looks along +Z with +Y up in model space (glTF), its left side is
 * +X.
 */
class BearRig {
 public:
  enum Leg { kFrontLeft, kFrontRight, kHindLeft, kHindRight, kLegCount };
  /**
   * Parts of the ragdoll (index into RagdollParts)
   */
  enum RagdollPartIndex {
    kPartPelvis,
    kPartChest,
    kPartHead,
    kPartUpperarmL,
    kPartForearmL,
    kPartUpperarmR,
    kPartForearmR,
    kPartThighL,
    kPartShinL,
    kPartThighR,
    kPartShinR,
    kPartCount
  };
  enum Idle { kIdleStretch, kIdleYawn, kIdleEat, kIdleLie, kIdleCount };
  /**
   * The idle animations the bear plays on its own when it stands still (the
   * first ones of Idle)
   */
  static constexpr int kRestingIdleCount = 2;
  /**
   * Times in the eating animation (seconds) when the fish moves from the mouth
   * into the paws, and the three bites
   */
  static constexpr float kEatTakeStart = 0.3f, kEatTakeEnd = 0.85f;
  static constexpr float kEatBites[3] = {1.35f, 2.35f, 3.3f};
  /**
   * The mouth of the model is open in its bind pose (the inside is modelled
   * with it), the jaw closes it by this angle (degrees)
   */
  static constexpr float kJawBindOpenDegrees = 24.0f;

  /**
   * @param skeleton Must stay alive as long as the rig
   */
  explicit BearRig(const animation::Skeleton& skeleton);

  /**
   * @return false if the skeleton lacks a bone the rig needs (see
   * GetMissingBones)
   */
  bool IsValid() const { return missing_bones_.empty(); }
  const std::string& GetMissingBones() const { return missing_bones_; }

  /**
   * Stands still with all feet on the ground (e.g. at the start)
   */
  void Reset(const LocomotionInput& input, const GroundProbe& probe);
  /**
   * Walking, running, jumping, standing and the idle animations
   */
  void UpdateLocomotion(float time_delta, const LocomotionInput& input,
                        const GroundProbe& probe);

  /**
   * Starts an idle animation (the bear must stand still while it plays). Lying
   * (kIdleLie) starts lying on the belly and goes on until StopIdle, the head
   * follows lookAt meanwhile.
   */
  void StartIdle(Idle idle);
  /**
   * Ends the idle animation, the pose blends into walking (e.g. when the player
   * moves the bear)
   */
  void StopIdle();
  bool IsIdling() const { return idle_ >= 0; }
  /**
   * @return The idle animation that plays, -1 if none
   */
  int GetIdle() const { return idle_; }
  float GetIdleDuration(Idle idle) const;
  /**
   * @return Seconds since the idle animation started
   */
  float GetIdleTime() const { return idle_time_; }
  /**
   * Holding something in the mouth (e.g. a fish) while it walks, runs and
   * jumps: the mouth stays a bit open
   */
  void SetCarrying(bool carrying) { this->carrying_ = carrying; }
  /**
   * Opens the mouth at least this far (0 to 1), e.g. while it talks
   */
  void SetMouthOpen(float amount) { mouth_open_ = amount; }
  /**
   * The head turns to a point and the eyes look at it (e.g. the face of an
   * animal it talks to) instead of looking around
   */
  void LookAt(glm::vec3 target);
  /**
   * The head and the eyes look around again
   */
  void StopLooking() { looking_ = false; }
  bool IsLooking() const { return looking_; }
  /**
   * Position of the head bone (world space)
   */
  glm::vec3 GetHeadPosition() const { return GetBonePosition(head_); }

  /**
   * Bodies and joints of the ragdoll in the bind pose (model space)
   */
  std::vector<physics::RagdollPart> RagdollParts() const;
  /**
   * World transforms of the ragdoll parts in the current pose, to start the
   * ragdoll where the bear is
   */
  void RagdollTransforms(std::vector<glm::mat4>& transforms) const;
  /**
   * Velocities of the ragdoll parts in the current animation (from the last two
   * frames)
   */
  void RagdollVelocities(std::vector<glm::vec3>& velocities) const;
  /**
   * The bones follow the bodies of the ragdoll
   */
  void UpdateFromRagdoll(float time_delta,
                         const std::vector<glm::mat4>& part_transforms);

  struct GetUpStart {
    glm::vec3 root_position;  // where the bear stands up (on the ground)
    glm::vec3 direction;      // where it looks then
  };
  /**
   * Starts getting up from the current pose (when the ragdoll lies still)
   * @param previous_direction Used if the body is upright (the direction cannot
   * be found from the body)
   */
  GetUpStart BeginGetUp(const GroundProbe& probe, glm::vec3 previous_direction);
  /**
   * @param root_position Where the bear stands (it may be pushed while it gets
   * up)
   * @return true when the bear stands again
   */
  bool UpdateGetUp(float time_delta, const GroundProbe& probe,
                   glm::vec3 root_position);
  /**
   * @return Duration of the get up animation that was started last (seconds)
   */
  float GetGetUpDuration() const;

  /**
   * World transforms of all bones in the current pose
   */
  const std::vector<glm::mat4>& GetWorldMatrices() const { return world_; }
  void SkinMatrices(std::vector<glm::mat4>& result) const;
  glm::vec3 GetBonePosition(int bone) const {
    return glm::vec3(world_[bone][3]);
  }
  int GetBone(const std::string& name) const { return skeleton_.Find(name); }
  /**
   * Center of the body (between pelvis and chest)
   */
  glm::vec3 GetCenter() const;
  /**
   * Where the paw of a leg touches the ground (or would touch it)
   */
  glm::vec3 GetFootTarget(Leg leg) const { return feet_[leg].position; }
  bool IsFootPlanted(Leg leg) const { return !feet_[leg].swinging; }
  /**
   * Height of the paw bones above the ground in the bind pose
   */
  float GetPawHeight(Leg leg) const { return legs_[leg].paw_height; }
  /**
   * Expression of the face in the current pose (including blinking)
   */
  const FaceControls& GetFace() const { return controls_.face; }

 private:
  struct Controls {
    glm::vec3 root_position = glm::vec3(0.0f);
    float yaw = 0.0f;
    // whole body around its length axis (lying on the side or back)
    float body_roll = 0.0f;
    glm::vec3 pelvis_offset = glm::vec3(0.0f);  // in the frame of the body
    float pelvis_pitch = 0.0f, pelvis_yaw = 0.0f,
          // pitch: nose up, yaw: left, roll: right side down
        pelvis_roll = 0.0f;
    float spine_pitch = 0.0f, spine_yaw = 0.0f,
          spine_roll = 0.0f;  // split between spine and chest
    float neck_pitch = 0.0f, neck_yaw = 0.0f;
    float head_pitch = 0.0f, head_yaw = 0.0f, head_roll = 0.0f;
    // world positions where the paws are on the ground
    std::array<glm::vec3, kLegCount> feet{};
    std::array<glm::vec3, kLegCount> foot_normals{};
    // paw tilted (toes down) while the leg swings
    std::array<float, kLegCount> toe_pitch{};
    FaceControls face;
  };

  /**
   * Key pose of an idle animation, in the frame of the body
   */
  struct IdleKey {
    float time = 0.0f;  // seconds from the start
    float pelvis_height = 0.0f, pelvis_pitch = 0.0f, pelvis_roll = 0.0f;
    float spine_pitch = 0.0f, spine_yaw = 0.0f, spine_roll = 0.0f;
    float neck_pitch = 0.0f, neck_yaw = 0.0f;
    float head_pitch = 0.0f, head_yaw = 0.0f, head_roll = 0.0f;
    // where the paws are, in the frame of the body
    std::array<glm::vec3, kLegCount> feet{};
    // 1: on the ground (found with the probe), 0: in the air
    std::array<float, kLegCount> grounded{};
    std::array<float, kLegCount> toe_pitch{};
    FaceControls face;
    float sway = 0.0f;  // slow rocking from side to side

    static IdleKey Mix(const IdleKey& a, const IdleKey& b, float t);
  };

  struct LegRig {
    int upper = -1, lower = -1, paw = -1;
    float upper_length = 0.0f, lower_length = 0.0f;
    float paw_height = 0.0f;
    // ground point under the paw in the bind pose (model space)
    glm::vec3 neutral = glm::vec3(0.0f);
    glm::quat paw_rotation =
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
    glm::vec3 swing_from = glm::vec3(0.0f);
    bool swinging = false;
    float swing_progress = 0.0f;
  };

  struct Eye {
    int eye = -1, upper_lid = -1, lower_lid = -1;
    // model space axis the upper lid closes around (the lower one the other
    // way)
    glm::vec3 lid_axis = glm::vec3(1.0f, 0.0f, 0.0f);
  };

  glm::mat4 BodyFrame(const Controls& controls) const;
  /**
   * Computes pose and world from the base pose and the controls
   */
  void Evaluate(const animation::Pose& base, const Controls& controls,
                float time_delta);
  /**
   * Moves the bones of the face (after the bone was placed with forward
   * kinematics)
   */
  void ApplyFace(int bone, const FaceControls& face, float time_delta);
  /**
   * Rotation of a bone relative to its bind pose in model space (for rotations
   * around the model axes of the head)
   */
  glm::quat BoneFrame(int bone) const;
  void UpdateSprings(int bone, animation::SpringPoint& spring, float length,
                     float stiffness, float damping, float gravity,
                     float max_angle, float time_delta);
  void RotateBone(int bone, const glm::quat& world_delta);
  glm::mat4 ParentWorld(int bone) const;
  void ForwardKinematics(int bone);
  bool ProbeGround(const GroundProbe& probe, glm::vec3 point, glm::vec3& ground,
                   glm::vec3& normal) const;
  void PlantFeet(const GroundProbe& probe, const Controls& controls);
  /**
   * Landing: the paws are put on the ground under where they are in the air
   * (close to where they stand)
   */
  void LandFeet(const GroundProbe& probe, const Controls& controls);
  /**
   * Legs in the air: pushing off, tucked in, reaching for the ground before
   * landing
   */
  void UpdateAirborne(float time_delta, const LocomotionInput& input,
                      const GroundProbe& probe, Controls& controls);
  /**
   * Swimming: the paws paddle in circles relative to the body
   */
  void UpdateSwimming(float time_delta, const LocomotionInput& input,
                      Controls& controls);
  void UpdateSecondary(float time_delta, bool moving);
  float BlinkAmount() const;
  /**
   * Controls of the idle animation that plays
   */
  Controls IdleControls(const LocomotionInput& input, const GroundProbe& probe);
  IdleKey StandingKey() const;
  IdleKey SittingKey() const;
  IdleKey LyingKey() const;
  void BuildIdleAnimations();

  const animation::Skeleton& skeleton_;
  std::string missing_bones_;
  int root_, pelvis_, spine_, chest_, neck_, head_, jaw_, ear_left_, ear_right_,
      tail_;
  std::array<int, 3> tongue_{};
  std::array<Eye, 2> eyes_;
  std::array<LegRig, kLegCount> legs_;
  std::array<int, kPartCount> part_bones_{};
  std::vector<glm::mat4> bind_model_;
  std::vector<glm::quat> bind_rotation_;
  glm::quat pelvis_bind_rotation_;
  glm::vec3 tongue_direction_ = glm::vec3(
      0.0f, 0.0f,
      1.0f);  // model space, from the root to the tip in the bind pose
  // of the tongue bones from the root along the tongue
  std::array<float, 3> tongue_bone_distance_{};

  animation::Pose bind_;
  animation::Pose pose_;
  std::vector<glm::mat4> world_;
  std::vector<glm::mat4> previous_world_;
  float last_time_delta_ = 0.0f;
  Controls controls_;

  // Locomotion
  std::array<Foot, kLegCount> feet_;
  float gait_phase_ = 0.0f;
  float gallop_ = 0.0f;  // 0: walk, 1: bound
  float body_height_ = 0.0f, body_pitch_ = 0.0f, lean_ = 0.0f, bend_ = 0.0f;
  float airborne_time_ = 0.0f;
  float last_vertical_speed_ = 0.0f;
  // since the jump took off (the hind legs push off), -1 if the bear did not
  // jump
  float launch_time_ = -1.0f;
  float air_pose_ = 0.0f;  // 0: on the ground, 1: legs in the air pose
  // 0: legs tucked, 1: reaching for the ground before landing
  float reach_ = 0.0f;
  // paws in the air, relative to the root (they move with the body)
  std::array<glm::vec3, kLegCount> air_offsets_{};
  // 0 right after landing: the steps start smoothly from where the paws landed
  float landing_blend_ = 1.0f;
  float swim_phase_ = 0.0f;   // strokes (1 per cycle of every leg)
  float swim_effort_ = 0.0f;  // 0 treading water, 1 swimming fast
  bool was_swimming_ = false;
  animation::SpringValue squash_;
  // nose up when taking off, down when the front legs take the landing
  animation::SpringValue pitch_kick_;
  std::array<animation::SpringPoint, 2> ear_springs_;
  animation::SpringPoint tail_spring_;

  // Secondary motion
  std::mt19937 random_{4711};
  float time_ = 0.0f;
  float look_yaw_ = 0.0f, look_pitch_ = 0.0f, look_target_yaw_ = 0.0f,
        look_target_pitch_ = 0.0f, look_timer_ = 2.0f;
  float gaze_yaw_ = 0.0f, gaze_pitch_ = 0.0f, gaze_target_yaw_ = 0.0f,
        gaze_target_pitch_ = 0.0f, saccade_timer_ = 1.0f;
  float blink_timer_ = 3.0f, blink_progress_ = -1.0f;
  float ear_twitch_timer_ = 4.0f;
  bool carrying_ = false;
  float mouth_open_ = 0.0f;
  // lying: 0 the key poses (resting its chin), 1 the head up (looking at
  // something)
  float alert_ = 0.0f;
  bool looking_ = false;
  glm::vec3 look_target_ = glm::vec3(0.0f);
  float fixation_yaw_ = 0.0f,
        // tiny jumps of the eyes around the point they look at
      fixation_pitch_ = 0.0f;

  // Idle animations
  std::array<std::vector<IdleKey>, kIdleCount> idle_animations_;
  int idle_ = -1;
  float idle_time_ = 0.0f;
  float blend_time_ = 0.0f;  // blends from blend_snapshot_ into the animation
  animation::Pose blend_snapshot_;

  // Getting up
  animation::Pose get_up_snapshot_;
  float get_up_time_ = 0.0f;
  float get_up_roll_ = 0.0f;
  bool get_up_rolls_over_ = false;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEARRIG_HPP_

#ifndef BIBO_SCENE_BEARCHARACTER_HPP_
#define BIBO_SCENE_BEARCHARACTER_HPP_
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"
#include "BearRig.hpp"
#include "Fur.hpp"

namespace bibo {
namespace scene {
/**
 * What the player wants the bear to do
 */
struct BearInput {
  /**
   * Where the bear should go in world space (y is ignored), length 0 to 1 (how
   * fast): the bear turns towards it and runs. Used instead of forward and turn
   * if it is not zero (e.g. the stick of a gamepad relative to the camera).
   */
  glm::vec3 move = glm::vec3(0.0f);
  float forward = 0.0f;  // relative to the bear: 1 forward, -1 backward
  float turn = 0.0f;     // 1: left, -1: right
  // held: the longer, the higher the jump (a new jump needs a new press)
  bool jump = false;
};

/**
 * The player character without graphics: the character controller, the
 * animation (BearRig), the ragdoll and the fur dynamics. After a knock down
 * (e.g. running into an obstacle) the bear tumbles as a ragdoll until it lies
 * still, then it gets up and the player controls it again. A fish that the bear
 * catches stays in its mouth until it stands on the ground, then the bear sits
 * down and eats it (the player can only move it again after the last bite). In
 * deep water the bear swims: it floats with its head and back out of the water
 * and paddles (slower than it runs), a jump at the surface helps it out onto
 * the shore.
 */
class BearCharacter {
 public:
  enum class State {
    kControl,  // the player moves the bear
    kRagdoll,  // tumbling after a hit
    kGettingUp
  };
  enum class Fish {
    kNone,
    kCarried,  // in the mouth (running, jumping)
    kEating
  };
  /**
   * Where the fish is that the bear holds (in the frame of the fish model: +z
   * towards its head)
   */
  struct HeldFish {
    glm::vec3 center = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    int bites = 0;        // taken so far (3: eaten)
    float wiggle = 0.0f;  // how far the tail is turned (radians)
  };

  /**
   * @param model Must stay alive as long as the character
   * @param startPosition Center of the character capsule
   * @param onHit Called when the bear touches a body (e.g. to knock it down
   * with KnockDown)
   */
  BearCharacter(physics::PhysicsWorld& world,
                const resources::SkinnedModel& model, glm::vec3 start_position,
                glm::vec3 start_direction,
                physics::CharacterController::HitCallback on_hit);
  ~BearCharacter();
  BearCharacter(const BearCharacter&) = delete;
  BearCharacter& operator=(const BearCharacter&) = delete;

  /**
   * Shape and movement limits of the bear's physics capsule
   */
  static physics::CharacterControllerDesc ControllerDesc(glm::vec3 position);

  // Running speed (m/s)
  static constexpr float kForwardSpeed = 10.0f;
  // Jump: how high the bear gets when the button is held until the highest
  // point, and how long it rises (a shorter press jumps lower)
  static constexpr float kJumpHeight = 3.0f;
  static constexpr float kJumpRiseTime = 0.4f;
  // Swimming speed (m/s)
  static constexpr float kSwimSpeed = 4.0f;
  // The bear swims when the ground under it is this deep under the water
  // surface (it stops in shallower water), it floats with its paws this deep in
  // the water
  static constexpr float kSwimDepth = 0.75f, kWadeDepth = 0.55f,
                         kFloatDepth = 0.85f;

  /**
   * Call after the physics world was updated
   */
  void Update(float time_delta, const BearInput& input);
  /**
   * The bear falls over and tumbles (at the end of the next update). Ignored
   * while it is down or getting up, and for a moment after it got up.
   */
  void KnockDown(const physics::CharacterController::Hit& hit);
  /**
   * Back to the start, standing (e.g. when the game is restarted)
   */
  void Reset();
  /**
   * Where reset puts the bear
   * @param position Center of the character capsule
   */
  void SetStart(glm::vec3 position, glm::vec3 direction);
  glm::vec3 GetStartPosition() const { return start_position_; }
  /**
   * From the center of the character capsule down to the ground it stands on
   */
  static float GroundOffset();
  /**
   * Puts the bear somewhere else, standing and looking in a direction (e.g. at
   * the start of a race)
   * @param position Center of the character capsule
   */
  void PlaceAt(glm::vec3 position, glm::vec3 direction);
  /**
   * Height of the water surface (the sea and the pools of the island), the bear
   * swims where it is deep enough. Without water (the default) it never swims.
   */
  void SetWaterLevel(float level) { water_level_ = level; }
  float GetWaterLevel() const { return water_level_; }
  /**
   * The bear looks at a point with its head and eyes (e.g. the face of the
   * animal it talks to); it does not start idle animations meanwhile (it stops
   * stretching or yawning)
   */
  void LookAt(glm::vec3 target) { rig_.LookAt(target); }
  void StopLooking() { rig_.StopLooking(); }
  /**
   * Between the eyes (world space)
   */
  glm::vec3 GetHeadPosition() const { return rig_.GetHeadPosition(); }
  /**
   * Plays an idle animation now (they also start on their own when the player
   * does not move the bear for a while)
   */
  void StartIdle(BearRig::Idle idle);
  /**
   * @return true if the bear can catch a fish now (it is not tumbling and does
   * not hold one already)
   */
  bool CanCatchFish() const {
    return state_ == State::kControl && fish_ == Fish::kNone;
  }
  /**
   * The bear caught a fish: it holds it in its mouth and eats it as soon as it
   * stands on the ground
   * @return false if it cannot catch one now (see CanCatchFish)
   */
  bool CatchFish();
  Fish GetFish() const { return fish_; }
  /**
   * @return How many fish the bear ate up since the last call (it took the last
   * bite; a fish it dropped does not count)
   */
  int TakeEatenFish();
  /**
   * @return false if the bear holds no fish
   */
  bool GetHeldFish(HeldFish& held) const;
  /**
   * Between the front teeth (world space)
   */
  glm::vec3 GetMouthPosition() const;

  State GetState() const { return state_; }
  /**
   * Center of the character capsule, or where it would be while the bear
   * tumbles (smooth when the state changes)
   */
  glm::vec3 GetPosition() const;
  glm::vec3 GetDirection() const { return direction_; }
  /**
   * How fast the bear moves (m/s), zero while it tumbles or gets up
   */
  glm::vec3 GetVelocity() const {
    return state_ == State::kControl ? velocity_ : glm::vec3(0.0f);
  }
  /**
   * @return true while the bear is in the air (jumping or falling)
   */
  bool IsAirborne() const {
    return state_ == State::kControl && !standing_on_ground_ && !swimming_;
  }
  /**
   * @return true while the bear swims (floats in deep water)
   */
  bool IsSwimming() const { return state_ == State::kControl && swimming_; }
  /**
   * @return How deep the paws are in the water (negative: above the water
   * surface), also while the bear tumbles
   */
  float GetWaterDepth() const;
  /**
   * @return How often the bear went into deep water since the last call (e.g.
   * for a splash)
   */
  int TakeSplashes();
  /**
   * @return true for a moment after getting up, when hits do not knock the bear
   * down
   */
  bool IsProtected() const { return protected_time_ > 0.0f; }
  /**
   * Obstacles that the fur lies on (e.g. rocks)
   */
  void SetObstacles(const std::vector<fur::SphereCollider>& obstacles) {
    this->obstacles_ = obstacles;
  }

  const BearRig& GetRig() const { return rig_; }
  const resources::SkinnedModel& GetModel() const { return model_; }
  // Skinned vertices of the model in world space (updated every frame)
  const std::vector<glm::vec3>& GetPositions() const { return positions_; }
  const std::vector<glm::vec3>& GetNormals() const { return normals_; }
  const std::vector<glm::vec3>& GetCombDirections() const {
    return comb_directions_;
  }
  const std::vector<glm::vec3>& GetFurOffsets() const {
    return guide_hairs_.GetOffsets();
  }
  /**
   * Sphere around the whole bear (with its fur)
   */
  glm::vec3 GetBoundsCenter() const { return rig_.GetCenter(); }
  float GetBoundsRadius() const { return 2.2f; }
  /**
   * Counts the frames in which the skinned vertices changed
   */
  unsigned int GetVersion() const { return version_; }

 private:
  glm::vec3 RawPosition() const;
  void SetState(State new_state);
  void UpdateControl(float time_delta, const BearInput& input);
  /**
   * @return How deep the paws of the character capsule are in the water
   * (negative: above the water surface)
   */
  float CapsuleWaterDepth() const;
  /**
   * Turns the bear and changes its speed (on the ground)
   * @return The speed the bear should run at (m/s)
   */
  float Steer(float time_delta, const BearInput& input, bool& has_input);
  void UpdateRagdoll(float time_delta);
  void UpdateGettingUp(float time_delta);
  void StartRagdoll();
  void StartGettingUp();
  void UpdateSkin(float time_delta);
  LocomotionInput MakeLocomotionInput(bool jumped = false) const;
  HeldFish CarriedFish() const;

  physics::PhysicsWorld& world_;
  const resources::SkinnedModel& model_;
  glm::vec3 start_position_;
  glm::vec3 start_direction_;
  physics::CharacterController::HitCallback on_hit_;
  std::unique_ptr<physics::CharacterController> controller_;
  BearRig rig_;
  std::unique_ptr<physics::Ragdoll> ragdoll_;
  GroundProbe probe_;

  State state_ = State::kControl;
  glm::vec3 direction_;
  float speed_ = 0.0f;  // along the direction
  float yaw_rate_ = 0.0f;
  glm::vec3 velocity_ = glm::vec3(0.0f);
  // Jumping and falling
  // horizontal velocity in the air (the bear keeps its momentum)
  glm::vec3 air_velocity_ = glm::vec3(0.0f);
  float vertical_speed_ = 0.0f;
  float air_time_ = 0.0f;  // since the bear left the ground
  // jump was pressed this long ago (it still jumps if it lands soon)
  float jump_buffer_ = 0.0f;
  bool jumping_ = false;  // rising from a jump (not falling off an edge)
  bool jump_was_held_ = false;
  float falling_time_ = 0.0f;  // getting up
  bool standing_on_ground_ = false;
  // Swimming
  float water_level_ = std::numeric_limits<float>::lowest();
  bool swimming_ = false;
  int splashes_ = 0;
  bool knock_down_pending_ = false;
  physics::CharacterController::Hit pending_hit_;
  float state_time_ = 0.0f, rest_time_ = 0.0f, protected_time_ = 0.0f;
  float idle_wait_time_ = 0.0f;  // standing without input
  int next_idle_ = BearRig::kIdleStretch;
  // smooths GetPosition when the state changes
  glm::vec3 position_offset_ = glm::vec3(0.0f);
  Fish fish_ = Fish::kNone;
  bool last_bite_taken_ = false;
  int eaten_fish_ = 0;
  int tongue_root_, tongue_tip_, left_paw_,
      right_paw_;  // bones that hold the fish

  // Skinning and fur
  std::vector<glm::mat4> skin_matrices_;
  std::vector<glm::vec3> positions_, normals_, comb_directions_,
      bind_comb_directions_;
  std::vector<float> fur_lengths_;
  fur::GuideHairs guide_hairs_;
  std::vector<fur::SphereCollider> obstacles_;
  float time_ = 0.0f;
  unsigned int version_ = 0;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEARCHARACTER_HPP_

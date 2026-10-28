#ifndef BIBO_SCENE_HEDGEHOG_HPP_
#define BIBO_SCENE_HEDGEHOG_HPP_
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>

namespace bibo {
namespace world {
class WorldGenerator;
}

namespace scene {
/**
 * A large hedgehog that strolls around its home on the track: it walks to a
 * spot nearby, sniffs around for a while and walks on. When the bear comes
 * close it scurries away from it. The bear is knocked over when it runs into
 * one (an obstacle). Only the behavior, the scene draws it (HedgehogRenderer)
 * and moves its collision sphere.
 */
class Hedgehog {
 public:
  /**
   * Can a hedgehog stand at a point of the ground plane?
   * @param height The height of the ground there
   */
  using Ground = std::function<bool(glm::vec2 position, float& height)>;
  /**
   * The floor of the race track (not too close to its walls, not in the water)
   * @param generator Must stay alive as long as the function is used
   */
  static Ground TrackFloor(const world::WorldGenerator& generator);
  /**
   * How the hedgehogs of the track start (every one looks somewhere else and
   * does something else)
   */
  static glm::vec2 StartDirection(int index) {
    return glm::vec2(std::sin(2.4f * index), std::cos(2.4f * index));
  }
  static std::uint32_t SeedOf(int index) {
    return 1000u + static_cast<std::uint32_t>(index);
  }

  // Size of the model (HedgehogModel) in the world
  static constexpr float kScale = 2.3f;
  static constexpr float kWalkSpeed = 1.1f;
  // How far it goes from its home
  static constexpr float kHomeRadius = 4.5f;
  // It scurries away from the bear when it comes this close, this much faster
  // than it walks
  static constexpr float kShyDistance = 4.0f, kScurryFactor = 1.8f;

  /**
   * @param home Where it lives (it starts there, looking along startDirection)
   */
  Hedgehog(glm::vec2 home, glm::vec2 start_direction, std::uint32_t seed,
           Ground ground);

  void Update(float time_delta);
  /**
   * Back home, as at the start
   */
  void Reset();
  /**
   * The bear ran into it: it hops up in surprise
   */
  void Startle();
  /**
   * Where the bear is: when it comes close, the hedgehog scurries away from it
   * (in the next update)
   */
  void Watch(glm::vec3 bear);

  /**
   * On the ground under the middle of the hedgehog
   */
  glm::vec3 GetPosition() const { return position_; }
  glm::vec2 GetHome() const { return home_; }
  /**
   * Its collision sphere (around the round body, without the spines)
   */
  glm::vec3 GetColliderCenter() const;
  float GetColliderRadius() const;
  bool IsWalking() const { return walking_; }
  bool IsScurrying() const { return scurrying_; }

  /**
   * What is drawn: the body and the feet
   */
  struct Pose {
    glm::vec3 body_position;  // origin of the model
    glm::quat body_rotation;
    // front left, front right, hind left, hind right (origins of the foot
    // models)
    std::array<glm::vec3, 4> feet;
    glm::quat foot_rotation;
    float bristle = 0.0f;  // 0 to 1: the spines stand up (startled)
  };
  Pose GetPose() const;

 private:
  void ChooseTarget();
  float Uniform(float from, float to);

  glm::vec2 home_, start_heading_;
  Ground ground_;
  std::uint32_t seed_;
  std::mt19937 random_;

  glm::vec3 position_;
  float yaw_ = 0.0f;  // 0: looking along +z
  glm::vec2 target_;
  bool walking_ = false;
  bool scurrying_ = false;  // away from the bear
  glm::vec2 bear_ = glm::vec2(1e9f);
  float wait_time_ = 0.0f;   // sniffing before it walks on
  float gait_phase_ = 0.0f;  // steps (1 per stride)
  float stride_ = 0.0f;  // 0 standing, 1 walking (blends the steps in and out)
  float time_ = 0.0f;
  float hop_time_ = -1.0f;  // since it was startled, -1 if it is not hopping
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_HEDGEHOG_HPP_

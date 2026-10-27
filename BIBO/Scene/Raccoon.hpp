#ifndef BIBO_SCENE_RACCOON_HPP_
#define BIBO_SCENE_RACCOON_HPP_
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

namespace bibo {
namespace scene {
/**
 * A raccoon of the forest. Fans come out of the woods while the bear is on a
 * quest: one trots to a spot beside the way of the bear, rears up on its hind
 * legs, turns to the bear, waves its paws, hops and chatters, and trots back
 * into the woods after a while. Chonk sits at its place instead: upright on its
 * haunches, it washes its paws every now and then and looks at the bear when it
 * comes close; while it talks its mouth moves with every sound and it gestures
 * with its paws. Only the behavior, the scene draws it (FurryRenderer with the
 * RaccoonModel).
 */
class Raccoon {
 public:
  /**
   * Can a raccoon walk at a point of the ground plane?
   * @param height The height of the ground there
   */
  using Ground = std::function<bool(glm::vec2 position, float& height)>;

  enum class Activity {
    kHidden,    // in the woods, not drawn
    kComing,    // trots out of the woods to its spot
    kCheering,  // stands up at its spot and cheers for the bear
    kLeaving,   // trots back into the woods
    kSitting    // sits at its place (Chonk)
  };

  // Size of the model (RaccoonModel) in the world
  static constexpr float kScale = 2.1f;
  static constexpr float kTrotSpeed = 3.2f;
  // It notices the bear within this distance (sitting)
  static constexpr float kAttentionDistance = 14.0f;

  /**
   * @param girth How round it is (RaccoonModel::Build), its collision sphere is
   * larger
   */
  Raccoon(Ground ground, std::uint32_t seed, float girth = 1.0f);

  /**
   * It appears at a point in the woods and trots to the spot, where it cheers
   */
  void ComeAndCheer(glm::vec2 from, glm::vec2 spot);
  /**
   * Trots back to where it came from and disappears there (nothing happens if
   * it is hidden or sitting)
   */
  void Leave();
  /**
   * Sits at a place, looking in a direction (when the bear is not close)
   */
  void SitAt(glm::vec3 position, glm::vec3 direction);
  /**
   * @param bear Where the head of the bear is (it turns to it and looks at it)
   */
  void Update(float time_delta, glm::vec3 bear);
  /**
   * A big jump with both paws up (if it is out of the woods)
   */
  void Cheer();
  /**
   * Talking (the dialog is open): it gestures with its paws and does not wash
   * them
   */
  void SetTalking(bool talking);
  /**
   * One sound while talking: the mouth opens and closes, the head nods
   */
  void Speak();
  /**
   * Back to how it was at the start (hidden, or at its place)
   */
  void Reset();

  Activity GetActivity() const { return activity_; }
  bool IsVisible() const { return activity_ != Activity::kHidden; }
  /**
   * Seconds since the activity changed
   */
  float GetActivityTime() const { return activity_time_; }
  /**
   * On the ground under its middle
   */
  glm::vec3 GetPosition() const { return position_; }
  glm::vec3 GetDirection() const {
    return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
  }
  /**
   * 0 on all fours, 1 upright on its hind legs
   */
  float GetUpright() const { return rear_; }
  float GetMouthOpen() const { return mouth_; }
  bool IsTalking() const { return talking_; }
  /**
   * Between its eyes (world space)
   */
  glm::vec3 GetFacePosition() const;
  /**
   * Where the exclamation mark floats (above its head), it bobs up and down; it
   * turns slowly
   */
  glm::vec3 GetMarkerPosition() const;
  glm::quat GetMarkerRotation() const;
  /**
   * Its collision sphere (the bear cannot walk through Chonk)
   */
  glm::vec3 GetColliderCenter() const;
  float GetColliderRadius() const;
  /**
   * @return The number of chatters since the last call (when it hops and
   * cheers)
   */
  int TakeChatters();

  /**
   * Model matrices of the parts of the RaccoonModel (with the scale), indexed
   * by RaccoonModel::Part
   */
  std::vector<glm::mat4> GetPose() const;

 private:
  float Uniform(float from, float to);
  void SetActivity(Activity next);
  // Walks towards the target, true when it is there (or cannot go on)
  bool Walk(float time_delta, float speed);
  void TurnTo(float wanted_yaw, float rate, float time_delta);

  Ground ground_;
  std::uint32_t seed_;
  float girth_;
  std::mt19937 random_;

  Activity activity_ = Activity::kHidden;
  float activity_time_ = 0.0f;
  glm::vec3 home_ = glm::vec3(0.0f);  // where it sits
  glm::vec3 home_direction_ = glm::vec3(0.0f, 0.0f, 1.0f);
  bool sits_ = false;  // it has a place (SitAt)
  glm::vec2 origin_ = glm::vec2(0.0f), target_ = glm::vec2(0.0f);

  glm::vec3 position_ = glm::vec3(0.0f);
  float yaw_ = 0.0f;
  float rear_ = 0.0f;  // 0 to 1: upright
  float gait_phase_ = 0.0f, stride_ = 0.0f;
  float head_yaw_ = 0.0f, head_pitch_ = 0.0f;
  float time_ = 0.0f;
  bool talking_ = false;
  float talk_blend_ = 0.0f;
  float mouth_ = 0.0f, mouth_time_ = -1.0f;
  float nod_ = 0.0f;
  float hop_time_ = -1.0f, next_hop_ = 1.0f;  // small hops while cheering
  float jump_time_ = -1.0f;                   // the big jump (cheer)
  float wash_time_ = -1.0f, next_wash_ = 4.0f;
  int chatters_ = 0;
};

/**
 * The raccoons that come out of the woods to cheer for the bear while it is on
 * a quest: every few seconds one comes to a spot ahead of the bear beside its
 * way (at most a few at a time), watches for a while and goes back into the
 * woods when the bear is past or the quest is over. They all jump when
 * something good happens (celebrate).
 */
class RaccoonFans {
 public:
  static constexpr std::size_t kCount = 4;
  static constexpr int kMaxWatching = 3;
  // A raccoon watches at most this long, and goes when the bear is this far
  // away
  static constexpr float kWatchTime = 9.0f, kGoneDistance = 38.0f;
  // They keep this distance to each other
  static constexpr float kSpacing = 3.5f;

  /**
   * Finds a spot for a raccoon ahead of the bear beside its way and where it
   * comes from (in the woods)
   * @param heading Where the bear goes (horizontal, length 1)
   * @return false if there is none
   */
  using Finder = std::function<bool(glm::vec3 bear, glm::vec3 heading,
                                    std::mt19937& random, glm::vec2& from,
                                    glm::vec2& spot)>;

  RaccoonFans(Raccoon::Ground ground, Finder finder, std::uint32_t seed = 515u);

  /**
   * @param cheering The bear is on a quest (the raccoons come), otherwise they
   * go back into the woods
   * @param bear Where the bear is, its head, where it goes (horizontal, length
   * 1)
   */
  void Update(float time_delta, bool cheering, glm::vec3 bear,
              glm::vec3 bear_head, glm::vec3 heading);
  /**
   * All raccoons that are out jump with their paws up
   */
  void Celebrate();
  /**
   * All back in the woods
   */
  void Reset();
  const std::vector<Raccoon>& GetRaccoons() const { return raccoons_; }
  int GetWatchingCount() const;
  /**
   * @return The number of chatters of all raccoons since the last call
   */
  int TakeChatters();

 private:
  Finder finder_;
  std::uint32_t seed_;
  std::mt19937 random_;
  std::vector<Raccoon> raccoons_;
  float next_arrival_ = 1.0f;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_RACCOON_HPP_

#ifndef BIBO_SCENE_BEAVER_HPP_
#define BIBO_SCENE_BEAVER_HPP_
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

namespace bibo {
namespace scene {
/**
 * Bober, the beaver at the start of the race track, who offers the race to the
 * bear (RaceQuest). It sits upright at the side of the track, turns to the bear
 * when it comes close, waves at it and looks at it. While it talks its mouth
 * moves with every sound it makes, it nods and gestures with its arms; between
 * the conversations it nibbles at something and slaps its tail on the ground
 * every now and then. Only the behavior, the scene draws it (FurryRenderer with
 * the BeaverModel).
 */
class Beaver {
 public:
  // Size of the model (BeaverModel) in the world
  static constexpr float kScale = 2.3f;
  // It notices the bear within this distance and turns to it
  static constexpr float kAttentionDistance = 14.0f;

  /**
   * @param position On the ground
   * @param direction Where it looks when the bear is not close (horizontal)
   */
  Beaver(glm::vec3 position, glm::vec3 direction, std::uint32_t seed = 4242u);

  /**
   * @param bear Where the head of the bear is (it turns to it when the bear is
   * close, its head and eyes look there)
   */
  void Update(float time_delta, glm::vec3 bear);
  /**
   * Back to how it was at the start
   */
  void Reset();
  /**
   * Talking (the dialog is open): it gestures with its arms and does not nibble
   */
  void SetTalking(bool talking);
  /**
   * One sound while talking: the mouth opens and closes, the head nods
   */
  void Speak();
  /**
   * Happy: it hops, raises its arms and slaps its tail on the ground
   */
  void Cheer();

  glm::vec3 GetPosition() const { return position_; }
  /**
   * Where the exclamation mark floats (above its head), it bobs up and down
   */
  glm::vec3 GetMarkerPosition() const;
  /**
   * The marker turns slowly around the vertical axis
   */
  glm::quat GetMarkerRotation() const;
  /**
   * Its collision sphere (the bear cannot walk through it)
   */
  glm::vec3 GetColliderCenter() const;
  float GetColliderRadius() const;
  /**
   * The point the camera looks at in a conversation (its face)
   */
  glm::vec3 GetHeadPosition() const;
  /**
   * Between its eyes (world space), e.g. for the bear to look at
   */
  glm::vec3 GetFacePosition() const;
  /**
   * @return 0 (closed) to 1 (wide open)
   */
  float GetMouthOpen() const { return mouth_; }
  bool IsTalking() const { return talking_; }
  /**
   * @return true while it waves at the bear (when the bear comes close)
   */
  bool IsWaving() const { return wave_time_ >= 0.0f; }

  /**
   * Model matrices of the parts of the BeaverModel (with the scale), indexed by
   * BeaverModel::Part
   */
  std::vector<glm::mat4> GetPose() const;

 private:
  float Uniform(float from, float to);

  glm::vec3 home_;
  glm::vec3 home_direction_;
  std::uint32_t seed_;
  std::mt19937 random_;

  glm::vec3 position_;
  float yaw_ = 0.0f;
  float head_yaw_ = 0.0f, head_pitch_ = 0.0f;
  float eye_yaw_ = 0.0f, eye_pitch_ = 0.0f;  // relative to the head
  float time_ = 0.0f;
  bool talking_ = false;
  float talk_blend_ = 0.0f;  // 0 to 1: blends the gestures in and out
  float mouth_ = 0.0f;
  // since the last sound started, -1 if the mouth is closed
  float mouth_time_ = -1.0f;
  float nod_ = 0.0f;
  float nibble_time_ = -1.0f, next_nibble_ = 3.0f;
  float cheer_time_ = -1.0f;
  float slap_time_ = -1.0f, next_slap_ = 8.0f;
  float wave_time_ = -1.0f;  // waves at the bear when it comes close
  bool attentive_ = false;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEAVER_HPP_

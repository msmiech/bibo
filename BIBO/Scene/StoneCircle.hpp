#ifndef BIBO_SCENE_STONECIRCLE_HPP_
#define BIBO_SCENE_STONECIRCLE_HPP_
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "TalkPartner.hpp"

namespace bibo {
namespace scene {
/**
 * The circle of standing stones on the hill, the goal of Chonk's quest
 * (StonesQuest): the bear inspects the flat stone in its middle (a
 * conversation with the stones). A large exclamation mark floats above it while
 * the bear is on the way, to be seen from afar.
 */
class StoneCircle : public TalkPartner {
 public:
  // The exclamation mark is larger than the ones above the animals and higher
  static constexpr float kGoalMarkerScale = 1.8f, kGoalMarkerHeight = 3.2f;

  StoneCircle() = default;
  /**
   * @param center The flat stone in the middle (on the ground)
   * @param radius Of the circle
   */
  StoneCircle(glm::vec3 center, float radius)
      : center_(center), radius_(radius) {}

  /**
   * The mark above it bobs up and down and turns
   */
  void Update(float time_delta) { time_ += time_delta; }
  float GetRadius() const { return radius_; }
  /**
   * @return true if a point is in the circle (horizontally)
   */
  bool Contains(glm::vec3 point) const {
    glm::vec3 offset = center_ - point;
    return glm::length(glm::vec2(offset.x, offset.z)) < radius_;
  }

  glm::vec3 GetPosition() const override { return center_; }
  glm::vec3 GetFacePosition() const override {
    return center_ + glm::vec3(0.0f, 0.3f, 0.0f);
  }
  glm::vec3 GetHeadPosition() const override {
    return center_ + glm::vec3(0.0f, 0.8f, 0.0f);
  }
  glm::vec3 GetMarkerPosition() const override {
    return center_ +
           glm::vec3(0.0f, kGoalMarkerHeight + 0.15f * std::sin(2.0f * time_),
                     0.0f);
  }
  glm::quat GetMarkerRotation() const override {
    return glm::angleAxis(time_ * 1.3f, glm::vec3(0.0f, 1.0f, 0.0f));
  }
  float GetMarkerScale() const override { return kGoalMarkerScale; }

 private:
  glm::vec3 center_ = glm::vec3(0.0f);
  float radius_ = 0.0f;
  float time_ = 0.0f;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_STONECIRCLE_HPP_

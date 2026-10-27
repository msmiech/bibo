#ifndef BIBO_SCENE_RESTINGBEAR_HPP_
#define BIBO_SCENE_RESTINGBEAR_HPP_
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "../Resources/SkinnedModel.hpp"
#include "BearRig.hpp"
#include "Fur.hpp"

namespace bibo {
namespace scene {
/**
 * Mishka, the bear who lies at the largest pool of water on the track and
 * watches the salmon jump, and who offers the salmon quest (SalmonQuest). An
 * older bear than the player's: bigger (the rig of the bear cub, scaled around
 * the point on the ground under it) with the coat of a moon bear (PaintCoat).
 * It does not get up: it lies on its belly (BearRig::kIdleLie), the head looks
 * around, follows a salmon in the air or the bear that comes to talk. While it
 * talks its mouth opens with every sound it makes. Only the behavior, the
 * skinned vertices and the fur; the scene draws it (BearRenderer) and gives it
 * a collision sphere.
 */
class RestingBear {
 public:
  static constexpr std::size_t kStrandCount = 45000;
  // Bigger than the player's bear cub
  static constexpr float kScale = 1.3f;
  // Its fur is longer (and shaggier) than the one of the cub, relative to its
  // size
  static constexpr float kFurLength = 1.15f * kScale;

  /**
   * @param model Must stay alive as long as the bear
   * @param position On the ground
   * @param direction Where it lies looking at (horizontal)
   * @param probe Finds the ground under a point (its paws and its fur lie on
   * it)
   */
  RestingBear(const resources::SkinnedModel& model, glm::vec3 position,
              glm::vec3 direction, GroundProbe probe);
  RestingBear(const RestingBear&) = delete;
  RestingBear& operator=(const RestingBear&) = delete;

  /**
   * @param watch Where it looks (e.g. a salmon in the air or the face of the
   * bear), nullptr: it looks around
   */
  void Update(float time_delta, const glm::vec3* watch);
  /**
   * One sound while talking: the mouth opens and closes
   */
  void Speak();

  /**
   * Paints the fur of a model like the coat of a moon bear (Asiatic black
   * bear): black, with a cream crescent on the chest and a tan muzzle (the
   * skin, eyes, nose and claws stay as they are, so does the length of the fur)
   */
  static void PaintCoat(resources::SkinnedModel& model);

  glm::vec3 GetPosition() const { return position_; }
  glm::vec3 GetDirection() const { return direction_; }
  /**
   * Between its eyes (world space), e.g. for the bear to look at
   */
  glm::vec3 GetFacePosition() const { return Scaled(rig_.GetHeadPosition()); }
  /**
   * Where the exclamation mark floats (above its head), it bobs up and down and
   * turns slowly
   */
  glm::vec3 GetMarkerPosition() const;
  glm::quat GetMarkerRotation() const;
  /**
   * Its collision sphere (the bear cannot walk through it)
   */
  glm::vec3 GetColliderCenter() const { return collider_center_; }
  float GetColliderRadius() const { return 0.95f * kScale; }
  /**
   * Sphere around the whole bear (with its fur)
   */
  glm::vec3 GetBoundsCenter() const { return Scaled(rig_.GetCenter()); }
  float GetBoundsRadius() const { return 2.2f * kScale; }
  /**
   * @return 0 (closed) to 1 (wide open), the mouth while it talks
   */
  float GetMouthOpen() const { return mouth_; }

  /**
   * The rig of the cub, before it is scaled (see scaled)
   */
  const BearRig& GetRig() const { return rig_; }
  /**
   * @return Where a point of the rig is in the world (scaled around the point
   * on the ground under the bear)
   */
  glm::vec3 Scaled(glm::vec3 rig_point) const {
    return position_ + (rig_point - position_) * kScale;
  }
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
   * Counts the frames in which the skinned vertices changed
   */
  unsigned int GetVersion() const { return version_; }

 private:
  LocomotionInput MakeLocomotionInput() const;
  void UpdateSkin(float time_delta);

  const resources::SkinnedModel& model_;
  glm::vec3 position_;
  glm::vec3 direction_;
  GroundProbe probe_;
  GroundProbe rig_probe_;  // the probe in the frame of the rig
  BearRig rig_;
  glm::vec3 collider_center_ = glm::vec3(0.0f);
  float time_ = 0.0f;
  float mouth_ = 0.0f;
  // since the last sound started, -1 if the mouth is closed
  float mouth_time_ = -1.0f;

  std::vector<glm::mat4> skin_matrices_;
  std::vector<glm::vec3> positions_, normals_, comb_directions_,
      bind_comb_directions_;
  std::vector<float> fur_lengths_;
  fur::GuideHairs guide_hairs_;
  unsigned int version_ = 0;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_RESTINGBEAR_HPP_

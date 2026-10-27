#pragma once
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
 * the point on the ground under it) with the coat of a moon bear (paintCoat).
 * It does not get up: it lies on its belly (BearRig::IDLE_LIE), the head looks
 * around, follows a salmon in the air or the bear that comes to talk. While it
 * talks its mouth opens with every sound it makes. Only the behavior, the
 * skinned vertices and the fur; the scene draws it (BearRenderer) and gives it
 * a collision sphere.
 */
class RestingBear {
 public:
  static constexpr std::size_t STRAND_COUNT = 45000;
  // Bigger than the player's bear cub
  static constexpr float SCALE = 1.3f;
  // Its fur is longer (and shaggier) than the one of the cub, relative to its
  // size
  static constexpr float FUR_LENGTH = 1.15f * SCALE;

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
  void update(float timeDelta, const glm::vec3* watch);
  /**
   * One sound while talking: the mouth opens and closes
   */
  void speak();

  /**
   * Paints the fur of a model like the coat of a moon bear (Asiatic black
   * bear): black, with a cream crescent on the chest and a tan muzzle (the
   * skin, eyes, nose and claws stay as they are, so does the length of the fur)
   */
  static void paintCoat(resources::SkinnedModel& model);

  glm::vec3 getPosition() const { return position; }
  glm::vec3 getDirection() const { return direction; }
  /**
   * Between its eyes (world space), e.g. for the bear to look at
   */
  glm::vec3 getFacePosition() const { return scaled(rig.getHeadPosition()); }
  /**
   * Where the exclamation mark floats (above its head), it bobs up and down and
   * turns slowly
   */
  glm::vec3 getMarkerPosition() const;
  glm::quat getMarkerRotation() const;
  /**
   * Its collision sphere (the bear cannot walk through it)
   */
  glm::vec3 getColliderCenter() const { return colliderCenter; }
  float getColliderRadius() const { return 0.95f * SCALE; }
  /**
   * Sphere around the whole bear (with its fur)
   */
  glm::vec3 getBoundsCenter() const { return scaled(rig.getCenter()); }
  float getBoundsRadius() const { return 2.2f * SCALE; }
  /**
   * @return 0 (closed) to 1 (wide open), the mouth while it talks
   */
  float getMouthOpen() const { return mouth; }

  /**
   * The rig of the cub, before it is scaled (see scaled)
   */
  const BearRig& getRig() const { return rig; }
  /**
   * @return Where a point of the rig is in the world (scaled around the point
   * on the ground under the bear)
   */
  glm::vec3 scaled(glm::vec3 rigPoint) const {
    return position + (rigPoint - position) * SCALE;
  }
  const resources::SkinnedModel& getModel() const { return model; }
  // Skinned vertices of the model in world space (updated every frame)
  const std::vector<glm::vec3>& getPositions() const { return positions; }
  const std::vector<glm::vec3>& getNormals() const { return normals; }
  const std::vector<glm::vec3>& getCombDirections() const {
    return combDirections;
  }
  const std::vector<glm::vec3>& getFurOffsets() const {
    return guideHairs.getOffsets();
  }
  /**
   * Counts the frames in which the skinned vertices changed
   */
  unsigned int getVersion() const { return version; }

 private:
  LocomotionInput locomotionInput() const;
  void updateSkin(float timeDelta);

  const resources::SkinnedModel& model;
  glm::vec3 position;
  glm::vec3 direction;
  GroundProbe probe;
  GroundProbe rigProbe;  // the probe in the frame of the rig
  BearRig rig;
  glm::vec3 colliderCenter = glm::vec3(0.0f);
  float time = 0.0f;
  float mouth = 0.0f;
  // since the last sound started, -1 if the mouth is closed
  float mouthTime = -1.0f;

  std::vector<glm::mat4> skinMatrices;
  std::vector<glm::vec3> positions, normals, combDirections, bindCombDirections;
  std::vector<float> furLengths;
  fur::GuideHairs guideHairs;
  unsigned int version = 0;
};
}  // namespace scene
}  // namespace bibo

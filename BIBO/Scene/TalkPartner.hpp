#ifndef BIBO_SCENE_TALKPARTNER_HPP_
#define BIBO_SCENE_TALKPARTNER_HPP_
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bibo {
namespace scene {
/**
 * Someone (or something) the bear talks to on a quest: the animals that offer
 * the quests (the beaver, Mishka, Chonk) and places like the flat stone in the
 * stone circle. In a conversation the bear walks up to it and they look at each
 * other, an exclamation mark floats above it while it has something for the
 * bear (Quest::GetMarker).
 */
class TalkPartner {
 public:
  // Size of the exclamation mark above an animal
  static constexpr float kMarkerScale = 0.8f;

  virtual ~TalkPartner() = default;

  /**
   * On the ground, the bear walks up to it
   */
  virtual glm::vec3 GetPosition() const = 0;
  /**
   * Between its eyes, the bear looks there
   */
  virtual glm::vec3 GetFacePosition() const = 0;
  /**
   * The point the camera of a conversation shows next to the bear
   */
  virtual glm::vec3 GetHeadPosition() const { return GetFacePosition(); }
  /**
   * Where the exclamation mark floats (above its head) and how it turns
   */
  virtual glm::vec3 GetMarkerPosition() const = 0;
  virtual glm::quat GetMarkerRotation() const = 0;
  virtual float GetMarkerScale() const { return kMarkerScale; }
  /**
   * One sound while its words appear in the conversation
   */
  virtual void Speak() {}
  /**
   * Something good happened on its quest
   */
  virtual void Cheer() {}
  /**
   * It talks in the conversation that is shown (e.g. it gestures)
   */
  virtual void SetTalking(bool talking) {}
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_TALKPARTNER_HPP_

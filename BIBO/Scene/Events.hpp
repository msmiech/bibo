#ifndef BIBO_SCENE_EVENTS_HPP_
#define BIBO_SCENE_EVENTS_HPP_
#include <variant>
#include <vector>

#include "RaceQuest.hpp"
#include "SalmonQuest.hpp"
#include "StonesQuest.hpp"

namespace bibo {
namespace scene {
/**
 * Something the animals did (for the sounds)
 */
enum class AnimalEvent {
  kRaccoonChatter,  // Chonk while it talks, the raccoons that cheer
  kSplash           // the bear fell into the water
};

/**
 * Something that happened in the scene: on a quest or by an animal
 */
using SceneEvent = std::variant<RaceQuest::Event, SalmonQuest::Event,
                                StonesQuest::Event, AnimalEvent>;

/**
 * What happened in the scene since it was taken last (e.g. for the sounds and
 * the messages of the game), in the order it happened
 */
class EventQueue {
 public:
  void Push(SceneEvent event) { events_.push_back(event); }
  /**
   * @return The events since the last call
   */
  std::vector<SceneEvent> Take() {
    std::vector<SceneEvent> result;
    result.swap(events_);
    return result;
  }
  void Clear() { events_.clear(); }

 private:
  std::vector<SceneEvent> events_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_EVENTS_HPP_

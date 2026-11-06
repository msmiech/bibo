#ifndef BIBO_SCENE_QUESTDIRECTOR_HPP_
#define BIBO_SCENE_QUESTDIRECTOR_HPP_
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../Util/Input.hpp"
#include "Events.hpp"
#include "Quest.hpp"
#include "TalkPartner.hpp"

namespace bibo {
namespace scene {
/**
 * Runs the quests of the island the same way, one at a time: while the bear is
 * on one quest no other is offered. The player talks to the closest partner of
 * a quest (the animal that offers it, or a place like the flat stone). The
 * animals that offer the quests speak while their words appear and cheer when
 * the bear accepts or completes them, the events of the quests go into the
 * event queue of the scene.
 *
 * The quests only know their rules and conversations, the scene adds what each
 * one needs to know about the bear (Describe).
 */
class QuestDirector {
 public:
  /**
   * Someone (or something) the bear talks to on a quest
   */
  struct Partner {
    TalkPartner* who = nullptr;
    // The player can talk to it within this distance (horizontally from it to
    // the center of the bear)
    float talk_distance = 0.0f;
    // In a conversation the bear keeps this distance to it (center to center,
    // so that their faces do not touch)
    float spacing = 0.0f;
  };
  /**
   * What happened on the quests in an update, for the raccoons that cheer
   */
  struct News {
    bool celebrate = false;  // they jump
    bool completed = false;  // a quest was completed
  };
  /**
   * @return true if the bear can talk now (e.g. it is not swimming) and is
   * within the distance of a point
   */
  using CanTalkAt = std::function<bool(glm::vec3 position, float distance)>;
  /**
   * Adds what the quest needs to know about the bear to its situation
   * @param near Per partner of the quest: the bear can talk to it
   */
  template <typename QuestType>
  using Describe = std::function<void(typename QuestType::Situation& situation,
                                      const std::vector<bool>& near)>;
  /**
   * An exclamation mark above a partner
   */
  struct MarkerSpot {
    const TalkPartner* partner;
    Quest::Marker marker;
  };

  explicit QuestDirector(CanTalkAt can_talk_at)
      : can_talk_at_(std::move(can_talk_at)) {}

  /**
   * @param partners The animal that offers the quest first, then the further
   * partners of the quest (Quest::GetPartnerCount)
   * @return The quest (it belongs to the director)
   */
  template <typename QuestType>
  QuestType& Add(std::unique_ptr<QuestType> quest,
                 std::vector<Partner> partners,
                 Describe<QuestType> describe = {});

  /**
   * The quests in the order they were added: who the player talks to, what the
   * bear does on them, the conversations
   * @param bear The center of the bear
   * @param events Gets the events of the quests
   */
  News Update(float time_delta, glm::vec3 bear,
              const input::PlayerInput& player, EventQueue& events);
  /**
   * Ends all quests and conversations without a result (e.g. when the game is
   * restarted)
   */
  void Abort();

  /**
   * @return true while the player moves the bear (not in a conversation or a
   * cutscene)
   */
  bool IsPlayerInControl() const;
  /**
   * @return true while the bear talks to someone
   */
  bool IsInConversation() const;
  /**
   * @return true while the raccoons come out of the woods to cheer
   */
  bool IsCheeredOn() const;
  /**
   * @return Who the bear talks to, or would talk to when the player presses the
   * button (the closest one it can talk to), nullptr if nobody
   */
  const Partner* GetTalkPartner() const;
  /**
   * @return Who the bear talks to in a conversation now, nullptr if it is not
   * in a conversation
   */
  const Partner* GetConversationPartner() const;
  /**
   * @return What the button does near the talk partner, empty if there is
   * nobody to talk to
   */
  std::string GetTalkPrompt() const;
  /**
   * @return The conversation that is shown (a real conversation before a line
   * in passing)
   */
  const Dialog& GetShownDialog() const;
  std::vector<MarkerSpot> GetMarkers() const;

 private:
  /**
   * Updates the quest with its situation, returns how its partner reacts
   */
  using Run = std::function<std::vector<Quest::Reaction>(
      float time_delta, const QuestSituation& common,
      const std::vector<bool>& near, EventQueue& events)>;
  struct Entry {
    std::unique_ptr<Quest> quest;
    std::vector<Partner> partners;
    Run run;
  };
  void ChooseTalkPartner(glm::vec3 bear);
  bool IsBusy(std::size_t entry) const;

  CanTalkAt can_talk_at_;
  std::vector<Entry> entries_;
  // Who the bear talks to: the entry and its partner, -1 if nobody
  int talk_entry_ = -1;
  int talk_partner_ = -1;
};

template <typename QuestType>
QuestType& QuestDirector::Add(std::unique_ptr<QuestType> quest,
                              std::vector<Partner> partners,
                              Describe<QuestType> describe) {
  QuestType& added = *quest;
  Entry entry;
  entry.run = [&added, describe = std::move(describe)](
                  float time_delta, const QuestSituation& common,
                  const std::vector<bool>& near, EventQueue& events) {
    typename QuestType::Situation situation;
    static_cast<QuestSituation&>(situation) = common;
    if (describe) {
      describe(situation, near);
    }
    added.Update(time_delta, situation);
    std::vector<Quest::Reaction> reactions;
    for (typename QuestType::Event event : added.TakeEvents()) {
      reactions.push_back(QuestType::React(event));
      events.Push(event);
    }
    return reactions;
  };
  entry.quest = std::move(quest);
  entry.partners = std::move(partners);
  entries_.push_back(std::move(entry));
  return added;
}
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_QUESTDIRECTOR_HPP_

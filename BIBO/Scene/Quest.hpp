#ifndef BIBO_SCENE_QUEST_HPP_
#define BIBO_SCENE_QUEST_HPP_
#include <string>

#include "Dialog.hpp"

namespace bibo {
namespace scene {
/**
 * What every quest is told in a frame, the situation of a quest adds what it
 * needs of the bear (where it is, whether it is close to the animal that offers
 * the quest, ...)
 */
struct QuestSituation {
  // the player wants to talk to someone of this quest (pressed in this frame)
  bool interact = false;
  bool busy = false;  // on another quest, this one is not offered
  DialogInput dialog;
};

/**
 * What the scene needs of every quest (RaceQuest, SalmonQuest, StonesQuest), so
 * that it runs them all the same way (QuestDirector): whether the bear is on
 * it, whether the player moves the bear, who the bear talks to on it (its
 * partners: the animal that offers it, and places like the flat stone of the
 * stones quest), the exclamation marks above them and the conversation.
 */
class Quest {
 public:
  /**
   * The exclamation mark above a partner: golden for a new quest (or the goal),
   * blue for a quest that was completed and can be done again
   */
  enum class Marker { kNone, kNew, kRepeatable };
  /**
   * What the scene shows when something happens on the quest
   */
  struct Reaction {
    bool speak = false;      // the animal that offers it makes a sound
    bool cheer = false;      // it cheers
    bool celebrate = false;  // the raccoons that cheer for the bear jump
    // the quest was completed: the raccoons go on cheering for a while
    bool completed = false;
  };

  virtual ~Quest() = default;

  /**
   * @return true from accepting the quest until it ends: no other quest is
   * offered meanwhile
   */
  virtual bool IsActive() const = 0;
  /**
   * @return true if the player moves the bear (not in a conversation or a
   * cutscene)
   */
  virtual bool IsPlayerInControl() const = 0;
  /**
   * @return true while the raccoons come out of the woods to cheer for the bear
   */
  virtual bool IsCheeredOn() const { return false; }
  /**
   * The name of the animal that offers the quest (partner 0)
   */
  virtual const char* GetSpeaker() const = 0;
  /**
   * @return Who or what the bear can talk to on the quest: 0 is the animal that
   * offers it, further ones are places
   */
  virtual int GetPartnerCount() const { return 1; }
  /**
   * @return true if the player can start talking to the partner now
   */
  virtual bool CanTalkTo(int partner) const = 0;
  /**
   * @return The partner the bear talks to, -1 if it is not in a conversation
   * of this quest
   */
  virtual int GetConversationPartner() const = 0;
  /**
   * @return What the button does near the partner
   */
  virtual std::string GetPrompt(int partner) const {
    return std::string("Talk to ") + GetSpeaker();
  }
  virtual Marker GetMarker(int partner) const = 0;
  virtual const Dialog& GetDialog() const = 0;
  /**
   * Ends the quest or a conversation without a result (e.g. when the game is
   * restarted)
   */
  virtual void Abort() = 0;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_QUEST_HPP_

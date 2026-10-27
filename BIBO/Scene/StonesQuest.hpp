#pragma once
#include <string>
#include <vector>

#include "Dialog.hpp"

namespace bibo {
namespace scene {
/**
 * The standing stones on the hill as a quest of Chonk, the well fed raccoon who
 * sits where the foot paths leave the race track: the stones hum at night and
 * lights dance between them, and Chonk is far too "busy" to look. Yes: the bear
 * climbs up the paths to the stone circle and inspects the flat stone in its
 * middle (a conversation that tells what it finds), then goes back to Chonk and
 * reports what is happening there. The player chooses what to report, the quest
 * is complete either way; Chonk thanks the bear afterwards but has no more
 * quests. Talking to Chonk in between tells where to go, the player can also
 * give up then. While the bear is on another quest Chonk does not offer it.
 *
 * Only the rules and the conversations, the scene tells it what the bear does.
 */
class StonesQuest {
 public:
  enum class Stage {
    IDLE,  // Chonk offers the quest (or it was completed)
    // conversation with Chonk (before, during or at the end of the quest)
    TALKING,
    EXPLORING,   // on the way to the stones, inspects the flat stone there
    INSPECTING,  // looks at the flat stone (a conversation with the stones)
    RETURNING    // on the way back to Chonk with the report
  };
  enum class Outcome { NONE, COMPLETED, STOPPED };
  /**
   * Something happened (for the sounds and the messages), see takeEvents
   */
  enum class Event {
    CHONK_SOUND,
    ACCEPTED,
    DECLINED,
    ARRIVED,
    INSPECTED,
    COMPLETED,
    STOPPED
  };
  /**
   * What the bear reported
   */
  enum class Report { NONE, TRUTH, HAUNTED };

  static constexpr const char* SPEAKER = "Chonk";
  // Who "talks" when the bear inspects the flat stone
  static constexpr const char* STONES = "The stone circle";
  // The player can talk to Chonk within this distance (horizontally from Chonk
  // to the center of the bear)
  static constexpr float TALK_DISTANCE = 5.0f;
  // The player can inspect the flat stone within this distance of the middle of
  // the circle
  static constexpr float INSPECT_DISTANCE = 4.5f;

  /**
   * What the bear does in this frame
   */
  struct Situation {
    // within TALK_DISTANCE, and the bear can talk (not tumbling, swimming or
    // eating)
    bool nearChonk = false;
    bool atStones = false;  // in the stone circle
    // within INSPECT_DISTANCE of the flat stone, and it can talk
    bool nearStone = false;
    bool interact = false;  // the player wants to talk (pressed in this frame)
    bool busy = false;      // on another quest, the quest is not offered
    DialogInput dialog;
  };

  void update(float timeDelta, const Situation& situation);
  /**
   * Ends the quest or a conversation (e.g. when the game is restarted), a
   * completed quest stays completed
   */
  void abort();

  Stage getStage() const { return stage; }
  /**
   * @return true from accepting the quest until the report (also while talking
   * to Chonk meanwhile)
   */
  bool isActive() const { return active; }
  bool isPlayerInControl() const {
    return stage != Stage::TALKING && stage != Stage::INSPECTING;
  }
  /**
   * @return true if Chonk offers the quest (the exclamation mark above it)
   */
  bool isOffered() const {
    return stage == Stage::IDLE && !busy && completions == 0;
  }
  /**
   * @return true if the bear has a report for Chonk (the exclamation mark above
   * it again)
   */
  bool hasReport() const { return stage == Stage::RETURNING; }
  /**
   * @return true if the player can start a conversation with Chonk now
   */
  bool canTalk() const;
  /**
   * @return true if the player can inspect the flat stone now
   */
  bool canInspect() const {
    return stage == Stage::EXPLORING && nearStone &&
           (!dialog.isOpen() || dialog.isPassing());
  }
  /**
   * @return true while the stones "talk" (the flat stone is inspected),
   * otherwise Chonk talks in the dialog
   */
  bool isInspecting() const { return stage == Stage::INSPECTING; }
  /**
   * @return true once the bear was in the stone circle on this quest
   */
  bool hasArrived() const { return arrived; }
  const Dialog& getDialog() const { return dialog; }
  /**
   * @return What the player should do now on the quest (empty when it is not
   * active)
   */
  std::string getObjective() const;

  Outcome getOutcome() const { return outcome; }
  float getOutcomeAge() const { return outcomeAge; }
  bool hasCompleted() const { return completions > 0; }
  Report getReport() const { return report; }

  std::vector<Event> takeEvents();

 private:
  void startConversation();
  void startInspection();
  void finish(Outcome result);

  Dialog dialog;
  Stage stage = Stage::IDLE;
  bool active = false;
  bool arrived = false;
  // Chonk answers the report, the quest is complete at the end of it
  bool reporting = false;
  bool nearChonk = false, nearStone = false, busy = false;
  Outcome outcome = Outcome::NONE;
  float outcomeAge = 0.0f;
  int completions = 0;
  Report report = Report::NONE;
  std::vector<Event> events;
};
}  // namespace scene
}  // namespace bibo

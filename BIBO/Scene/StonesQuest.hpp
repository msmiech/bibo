#ifndef BIBO_SCENE_STONESQUEST_HPP_
#define BIBO_SCENE_STONESQUEST_HPP_
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
    kIdle,  // Chonk offers the quest (or it was completed)
    // conversation with Chonk (before, during or at the end of the quest)
    kTalking,
    kExploring,   // on the way to the stones, inspects the flat stone there
    kInspecting,  // looks at the flat stone (a conversation with the stones)
    kReturning    // on the way back to Chonk with the report
  };
  enum class Outcome { kNone, kCompleted, kStopped };
  /**
   * Something happened (for the sounds and the messages), see TakeEvents
   */
  enum class Event {
    kChonkSound,
    kAccepted,
    kDeclined,
    kArrived,
    kInspected,
    kCompleted,
    kStopped
  };
  /**
   * What the bear reported
   */
  enum class Report { kNone, kTruth, kHaunted };

  static constexpr const char* kSpeaker = "Chonk";
  // Who "talks" when the bear inspects the flat stone
  static constexpr const char* kStones = "The stone circle";
  // The player can talk to Chonk within this distance (horizontally from Chonk
  // to the center of the bear)
  static constexpr float kTalkDistance = 5.0f;
  // The player can inspect the flat stone within this distance of the middle of
  // the circle
  static constexpr float kInspectDistance = 4.5f;

  /**
   * What the bear does in this frame
   */
  struct Situation {
    // within kTalkDistance, and the bear can talk (not tumbling, swimming or
    // eating)
    bool near_chonk = false;
    bool at_stones = false;  // in the stone circle
    // within kInspectDistance of the flat stone, and it can talk
    bool near_stone = false;
    bool interact = false;  // the player wants to talk (pressed in this frame)
    bool busy = false;      // on another quest, the quest is not offered
    DialogInput dialog;
  };

  void Update(float time_delta, const Situation& situation);
  /**
   * Ends the quest or a conversation (e.g. when the game is restarted), a
   * completed quest stays completed
   */
  void Abort();

  Stage GetStage() const { return stage_; }
  /**
   * @return true from accepting the quest until the report (also while talking
   * to Chonk meanwhile)
   */
  bool IsActive() const { return active_; }
  bool IsPlayerInControl() const {
    return stage_ != Stage::kTalking && stage_ != Stage::kInspecting;
  }
  /**
   * @return true if Chonk offers the quest (the exclamation mark above it)
   */
  bool IsOffered() const {
    return stage_ == Stage::kIdle && !busy_ && completions_ == 0;
  }
  /**
   * @return true if the bear has a report for Chonk (the exclamation mark above
   * it again)
   */
  bool HasReport() const { return stage_ == Stage::kReturning; }
  /**
   * @return true if the player can start a conversation with Chonk now
   */
  bool CanTalk() const;
  /**
   * @return true if the player can inspect the flat stone now
   */
  bool CanInspect() const {
    return stage_ == Stage::kExploring && near_stone_ &&
           (!dialog_.IsOpen() || dialog_.IsPassing());
  }
  /**
   * @return true while the stones "talk" (the flat stone is inspected),
   * otherwise Chonk talks in the dialog
   */
  bool IsInspecting() const { return stage_ == Stage::kInspecting; }
  /**
   * @return true once the bear was in the stone circle on this quest
   */
  bool HasArrived() const { return arrived_; }
  const Dialog& GetDialog() const { return dialog_; }
  /**
   * @return What the player should do now on the quest (empty when it is not
   * active)
   */
  std::string GetObjective() const;

  Outcome GetOutcome() const { return outcome_; }
  float GetOutcomeAge() const { return outcome_age_; }
  bool HasCompleted() const { return completions_ > 0; }
  Report GetReport() const { return report_; }

  std::vector<Event> TakeEvents();

 private:
  void StartConversation();
  void StartInspection();
  void Finish(Outcome result);

  Dialog dialog_;
  Stage stage_ = Stage::kIdle;
  bool active_ = false;
  bool arrived_ = false;
  // Chonk answers the report, the quest is complete at the end of it
  bool reporting_ = false;
  bool near_chonk_ = false, near_stone_ = false, busy_ = false;
  Outcome outcome_ = Outcome::kNone;
  float outcome_age_ = 0.0f;
  int completions_ = 0;
  Report report_ = Report::kNone;
  std::vector<Event> events_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_STONESQUEST_HPP_

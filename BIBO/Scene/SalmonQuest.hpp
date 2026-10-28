#ifndef BIBO_SCENE_SALMONQUEST_HPP_
#define BIBO_SCENE_SALMONQUEST_HPP_
#include <vector>

#include "Dialog.hpp"

namespace bibo {
namespace scene {
/**
 * The salmon of the pools on the track as a quest of Mishka, the bear who lies
 * at the largest pool and watches the salmon jump: the player talks to Mishka,
 * who asks whether the bear would like to go fishing. Yes: every salmon the
 * bear eats up gives points, one per pool (no more salmon jump out of a pool
 * that is done). The quest is complete when the bear ate one from every pool.
 * Talking to Mishka in between tells how many are left, the player can also
 * stop fishing then. After a completed quest Mishka offers it again (to be
 * faster). Outside of the quest the bear catches and eats salmon without
 * points. While the bear is on another quest (the race) Mishka does not offer
 * it.
 *
 * Only the rules and the conversation, the scene tells it what the bear does.
 */
class SalmonQuest {
 public:
  enum class Stage {
    kIdle,     // not fishing, Mishka offers the quest
    kTalking,  // conversation with Mishka (before or during the quest)
    kFishing   // the quest runs, the player moves the bear
  };
  /**
   * How the last quest ended
   */
  enum class Outcome { kNone, kCompleted, kStopped };
  /**
   * Something happened (for the sounds), see TakeEvents
   */
  enum class Event {
    kBearSound,
    kAccepted,
    kDeclined,
    kEaten,
    kCompleted,
    kStopped
  };

  // The name of the bear
  static constexpr const char* kSpeaker = "Mishka";
  // For every salmon the bear eats up on the quest
  static constexpr int kSalmonPoints = 100;
  // The player can talk to Mishka within this distance (horizontally from
  // Mishka to the center of the bear)
  static constexpr float kTalkDistance = 5.5f;

  /**
   * What the bear does in this frame
   */
  struct Situation {
    // within kTalkDistance, and the bear can talk (not tumbling, swimming or
    // eating)
    bool near_mishka = false;
    bool interact = false;  // the player wants to talk (pressed in this frame)
    bool busy = false;      // on another quest, the quest is not offered
    // the bear ate up a salmon of this pool in this frame, -1 if none
    int eaten_pool = -1;
    DialogInput dialog;
  };

  explicit SalmonQuest(int pool_count);

  void Update(float time_delta, const Situation& situation);
  /**
   * Ends the quest or a conversation without a result (e.g. when the game is
   * restarted), the best time is kept
   */
  void Abort();

  Stage GetStage() const { return stage_; }
  /**
   * @return true from accepting the quest until it is complete or the player
   * stopped (also while talking to Mishka meanwhile)
   */
  bool IsActive() const { return active_; }
  /**
   * @return true if the player moves the bear (not in a conversation)
   */
  bool IsPlayerInControl() const { return stage_ != Stage::kTalking; }
  /**
   * @return true if Mishka offers the quest (the exclamation mark above it)
   */
  bool IsOffered() const { return stage_ == Stage::kIdle && !busy_; }
  /**
   * @return true if the player can start a conversation now (near Mishka, it
   * may still say something in passing)
   */
  bool CanTalk() const {
    return stage_ != Stage::kTalking && !busy_ && near_mishka_ &&
           (!dialog_.IsOpen() || dialog_.IsPassing());
  }
  const Dialog& GetDialog() const { return dialog_; }

  int GetPoolCount() const { return static_cast<int>(done_.size()); }
  /**
   * Per pool: true if the bear ate a salmon of it on this quest (the pool is
   * closed then)
   */
  const std::vector<bool>& GetDonePools() const { return done_; }
  int GetEatenCount() const { return eaten_; }
  /**
   * @return Points of this quest, or of the last one after it ended
   */
  int GetScore() const { return eaten_ * kSalmonPoints; }
  /**
   * @return Seconds of fishing since the quest was accepted (not while
   * talking), also after it ended: the time of the last one
   */
  float GetTime() const { return time_; }

  Outcome GetOutcome() const { return outcome_; }
  /**
   * @return Seconds since the last quest ended (for showing the result for a
   * while)
   */
  float GetOutcomeAge() const { return outcome_age_; }
  bool HasCompleted() const { return completions_ > 0; }
  int GetCompletions() const { return completions_; }
  /**
   * @return The best time of a completed quest (seconds), 0 if none
   */
  float GetBestTime() const { return best_time_; }
  /**
   * @return true if the last quest was completed with a new best time
   */
  bool IsNewBest() const { return new_best_; }

  /**
   * @return What happened since the last call
   */
  std::vector<Event> TakeEvents();

 private:
  void SetStage(Stage new_stage);
  void StartConversation();
  void Finish(Outcome result);

  Dialog dialog_;
  Stage stage_ = Stage::kIdle;
  bool active_ = false;
  bool near_mishka_ = false;
  bool busy_ = false;
  std::vector<bool> done_;
  int eaten_ = 0;
  float time_ = 0.0f;
  Outcome outcome_ = Outcome::kNone;
  float outcome_age_ = 0.0f;
  int completions_ = 0;
  float best_time_ = 0.0f;
  bool new_best_ = false;
  std::vector<Event> events_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_SALMONQUEST_HPP_

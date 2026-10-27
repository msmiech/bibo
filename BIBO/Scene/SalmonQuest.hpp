#pragma once
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
    IDLE,     // not fishing, Mishka offers the quest
    TALKING,  // conversation with Mishka (before or during the quest)
    FISHING   // the quest runs, the player moves the bear
  };
  /**
   * How the last quest ended
   */
  enum class Outcome { NONE, COMPLETED, STOPPED };
  /**
   * Something happened (for the sounds), see takeEvents
   */
  enum class Event {
    BEAR_SOUND,
    ACCEPTED,
    DECLINED,
    EATEN,
    COMPLETED,
    STOPPED
  };

  // The name of the bear
  static constexpr const char* SPEAKER = "Mishka";
  // For every salmon the bear eats up on the quest
  static constexpr int SALMON_POINTS = 100;
  // The player can talk to Mishka within this distance (horizontally from
  // Mishka to the center of the bear)
  static constexpr float TALK_DISTANCE = 5.5f;

  /**
   * What the bear does in this frame
   */
  struct Situation {
    bool nearMishka = false;  // within TALK_DISTANCE, and the bear can talk
                              // (not tumbling, swimming or eating)
    bool interact = false;  // the player wants to talk (pressed in this frame)
    bool busy = false;      // on another quest, the quest is not offered
    int eatenPool =
        -1;  // the bear ate up a salmon of this pool in this frame, -1 if none
    DialogInput dialog;
  };

  explicit SalmonQuest(int poolCount);

  void update(float timeDelta, const Situation& situation);
  /**
   * Ends the quest or a conversation without a result (e.g. when the game is
   * restarted), the best time is kept
   */
  void abort();

  Stage getStage() const { return stage; }
  /**
   * @return true from accepting the quest until it is complete or the player
   * stopped (also while talking to Mishka meanwhile)
   */
  bool isActive() const { return active; }
  /**
   * @return true if the player moves the bear (not in a conversation)
   */
  bool isPlayerInControl() const { return stage != Stage::TALKING; }
  /**
   * @return true if Mishka offers the quest (the exclamation mark above it)
   */
  bool isOffered() const { return stage == Stage::IDLE && !busy; }
  /**
   * @return true if the player can start a conversation now (near Mishka, it
   * may still say something in passing)
   */
  bool canTalk() const {
    return stage != Stage::TALKING && !busy && nearMishka &&
           (!dialog.isOpen() || dialog.isPassing());
  }
  const Dialog& getDialog() const { return dialog; }

  int getPoolCount() const { return static_cast<int>(done.size()); }
  /**
   * Per pool: true if the bear ate a salmon of it on this quest (the pool is
   * closed then)
   */
  const std::vector<bool>& getDonePools() const { return done; }
  int getEatenCount() const { return eaten; }
  /**
   * @return Points of this quest, or of the last one after it ended
   */
  int getScore() const { return eaten * SALMON_POINTS; }
  /**
   * @return Seconds of fishing since the quest was accepted (not while
   * talking), also after it ended: the time of the last one
   */
  float getTime() const { return time; }

  Outcome getOutcome() const { return outcome; }
  /**
   * @return Seconds since the last quest ended (for showing the result for a
   * while)
   */
  float getOutcomeAge() const { return outcomeAge; }
  bool hasCompleted() const { return completions > 0; }
  int getCompletions() const { return completions; }
  /**
   * @return The best time of a completed quest (seconds), 0 if none
   */
  float getBestTime() const { return bestTime; }
  /**
   * @return true if the last quest was completed with a new best time
   */
  bool isNewBest() const { return newBest; }

  /**
   * @return What happened since the last call
   */
  std::vector<Event> takeEvents();

 private:
  void setStage(Stage newStage);
  void startConversation();
  void finish(Outcome result);

  Dialog dialog;
  Stage stage = Stage::IDLE;
  bool active = false;
  bool nearMishka = false;
  bool busy = false;
  std::vector<bool> done;
  int eaten = 0;
  float time = 0.0f;
  Outcome outcome = Outcome::NONE;
  float outcomeAge = 0.0f;
  int completions = 0;
  float bestTime = 0.0f;
  bool newBest = false;
  std::vector<Event> events;
};
}  // namespace scene
}  // namespace bibo

#ifndef BIBO_SCENE_RACEQUEST_HPP_
#define BIBO_SCENE_RACEQUEST_HPP_
#include <cstddef>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "../World/TrackPath.hpp"
#include "Dialog.hpp"
#include "GameRules.hpp"

namespace bibo {
namespace scene {
/**
 * The race around the track as a quest of the beaver at the start: the player
 * talks to the beaver, who asks whether the bear would like to give the race
 * track a shot. Yes: the bear walks up to the starting line (a short cutscene,
 * the scene moves it), a countdown 3, 2, 1 starts the timer. The lap is
 * complete at the finish line after all checkpoints in their order. It fails
 * when the lap takes too long, the bear leaves the track or falls into the
 * water; the beaver offers the race again then. After a completed lap the
 * beaver offers to improve the best time. While the bear is on another quest
 * (e.g. fishing, SalmonQuest) the beaver does not offer the race, it only says
 * that the bear looks busy.
 *
 * Only the rules and the conversation, the scene tells it where the bear is and
 * moves the bear and the camera.
 */
class RaceQuest {
 public:
  enum class Stage {
    kIdle,            // the bear roams freely, the beaver offers the race
    kTalking,         // conversation with the beaver
    kWalkingToStart,  // cutscene: the bear walks up to the starting line
    kCountdown,       // 3, 2, 1
    kRacing           // the timer runs
  };
  /**
   * How the last race ended
   */
  enum class Outcome {
    kNone,
    kCompleted,
    kTooSlow,
    kLeftTrack,
    kFellIntoWater
  };
  /**
   * Something happened (for the sounds and the animation of the beaver), see
   * TakeEvents
   */
  enum class Event {
    kBeaverSound,
    kAccepted,
    kDeclined,
    kCountdownTick,
    kStart,
    kCheckpoint,
    kCompleted,
    kFailed
  };

  // The name of the beaver
  static constexpr const char* kSpeaker = "Bober";
  // The lap must be done within this time (seconds)
  static constexpr float kTimeLimit = 120.0f;
  static constexpr int kCountdownSeconds = 3;
  // The player can talk to the beaver within this distance (horizontally from
  // the beaver to the center of the bear)
  static constexpr float kTalkDistance = 5.5f;
  // The cutscene ends after this time even if the bear did not reach the
  // starting line (e.g. it was knocked over)
  static constexpr float kWalkTimeout = 12.0f;
  // How long "GO!" is shown after the countdown
  static constexpr float kGoDuration = 1.0f;
  // The bear left the track when it is this far beside the floor of the track
  // (e.g. up the walls of the canyon into the forest)
  static constexpr float kOffTrackMargin = 6.0f;

  /**
   * @return true if the position is on the race track (on its floor or close to
   * it, e.g. on the slope of its walls)
   */
  static bool IsOnTrack(const world::TrackPath& path, glm::vec3 position);

  /**
   * What the bear does in this frame
   */
  struct Situation {
    glm::vec3 bear = glm::vec3(0.0f);  // center of the bear
    // within kTalkDistance, and the bear can talk (not tumbling)
    bool near_beaver = false;
    bool swimming = false;
    bool on_track = true;
    // at the starting line, looking along the track (end of the cutscene)
    bool at_start = false;
    bool interact = false;  // the player wants to talk (pressed in this frame)
    bool busy = false;      // on another quest, the race is not offered
    DialogInput dialog;
  };

  /**
   * @param finishLine Start and finish of the lap
   * @param startDirection Driving direction at the start (horizontal)
   */
  RaceQuest(const rules::FinishLine& finish_line, glm::vec3 start_direction);

  void Update(float time_delta, const Situation& situation);
  /**
   * Ends a race or a conversation without a result (e.g. when the game is
   * restarted), the best time is kept
   */
  void Abort();

  Stage GetStage() const { return stage_; }
  /**
   * @return true if the player moves the bear (not in a conversation or the
   * cutscene)
   */
  bool IsPlayerInControl() const {
    return stage_ == Stage::kIdle || stage_ == Stage::kRacing;
  }
  /**
   * @return true if the beaver offers the race (the exclamation mark above it)
   */
  bool IsOffered() const { return stage_ == Stage::kIdle && !busy_; }
  /**
   * @return true if the player can start a conversation now (near the beaver,
   * it may still say something in passing)
   */
  bool CanTalk() const {
    return stage_ == Stage::kIdle && near_beaver_ &&
           (!dialog_.IsOpen() || dialog_.IsPassing());
  }
  const Dialog& GetDialog() const { return dialog_; }

  /**
   * Where the bear stands at the starting line (on the ground plane, y is the
   * height of the finish line) and where it looks
   */
  glm::vec3 GetStartPosition() const { return start_position_; }
  glm::vec3 GetStartDirection() const { return start_direction_; }
  const rules::FinishLine& GetFinishLine() const { return finish_line_; }

  /**
   * @return Seconds left in the countdown (kCountdownSeconds to 0)
   */
  float GetCountdown() const { return countdown_; }
  /**
   * @return The number of the countdown that is shown (3, 2, 1), 0 for "GO!",
   * -1 if nothing is shown
   */
  int GetCountdownNumber() const;
  /**
   * @return Seconds since the start of the race (also after it ended: the time
   * of the last race)
   */
  float GetRaceTime() const { return race_time_; }
  std::size_t GetCheckpointsPassed() const { return next_checkpoint_; }
  std::size_t GetCheckpointCount() const { return rules::kCheckpointCount; }
  /**
   * Seconds since the stage changed
   */
  float GetStageTime() const { return stage_time_; }

  Outcome GetOutcome() const { return outcome_; }
  /**
   * @return Seconds since the last race ended (for showing the result for a
   * while)
   */
  float GetOutcomeAge() const { return outcome_age_; }
  bool HasCompleted() const { return completions_ > 0; }
  int GetCompletions() const { return completions_; }
  /**
   * @return The best time of a completed lap (seconds), 0 if none
   */
  float GetBestTime() const { return best_time_; }
  /**
   * @return true if the last race was completed with a new best time
   */
  bool IsNewBest() const { return new_best_; }

  /**
   * @return What happened since the last call
   */
  std::vector<Event> TakeEvents();

  /**
   * @return The time as minutes, seconds and hundredths, e.g. "1:23.45"
   */
  static std::string FormatTime(float seconds);

 private:
  void SetStage(Stage new_stage);
  void StartConversation();
  void Fail(Outcome reason);

  rules::FinishLine finish_line_;
  glm::vec3 start_direction_;
  glm::vec3 start_position_;
  Dialog dialog_;
  Stage stage_ = Stage::kIdle;
  float stage_time_ = 0.0f;
  bool near_beaver_ = false;
  bool busy_ = false;
  float countdown_ = 0.0f;
  float race_time_ = 0.0f;
  std::size_t next_checkpoint_ = 0;
  Outcome outcome_ = Outcome::kNone;
  float outcome_age_ = 0.0f;
  int completions_ = 0;
  float best_time_ = 0.0f;
  bool new_best_ = false;
  std::vector<Event> events_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_RACEQUEST_HPP_

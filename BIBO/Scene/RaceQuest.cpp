#include "RaceQuest.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bibo::scene {

namespace {
// The bear stands this far behind the finish line at the start
const float kStartBehindLine = 1.0f;
const int kYes = 0;

/**
 * The number of the countdown that is shown (3 from 3 to 2 seconds before the
 * start, ...), robust against rounding
 */
int CountdownNumber(float seconds_left) {
  return static_cast<int>(std::ceil(seconds_left - 1e-3f));
}
}  // namespace

RaceQuest::RaceQuest(const rules::FinishLine& finish_line,
                     glm::vec3 start_direction)
    : finish_line_(finish_line) {
  glm::vec3 direction(start_direction.x, 0.0f, start_direction.z);
  this->start_direction_ = glm::length(direction) > 1e-6f
                               ? glm::normalize(direction)
                               : glm::vec3(0.0f, 0.0f, -1.0f);
  start_position_ =
      finish_line.center - this->start_direction_ * kStartBehindLine;
}

bool RaceQuest::IsOnTrack(const world::TrackPath& path, glm::vec3 position) {
  world::TrackPath::Nearest nearest =
      path.FindNearest(glm::vec2(position.x, position.z));
  return nearest.found && std::abs(nearest.lateral) <=
                              nearest.floor_half_width + kOffTrackMargin;
}

std::string RaceQuest::FormatTime(float seconds) {
  int hundredths =
      static_cast<int>(std::floor(std::max(seconds, 0.0f) * 100.0f));
  char text[32];
  std::snprintf(text, sizeof(text), "%d:%02d.%02d", hundredths / 6000,
                hundredths / 100 % 60, hundredths % 100);
  return text;
}

void RaceQuest::SetStage(Stage new_stage) {
  stage_ = new_stage;
  stage_time_ = 0.0f;
}

void RaceQuest::Abort() {
  dialog_.Close();
  int choice;
  dialog_.TakeEnded(choice);
  dialog_.TakeSounds();
  SetStage(Stage::kIdle);
  countdown_ = 0.0f;
  race_time_ = 0.0f;
  next_checkpoint_ = 0;
  outcome_ = Outcome::kNone;
  outcome_age_ = 0.0f;
  new_best_ = false;
  events_.clear();
}

int RaceQuest::GetCountdownNumber() const {
  if (stage_ == Stage::kCountdown) {
    return std::clamp(CountdownNumber(countdown_), 1, kCountdownSeconds);
  }
  if (stage_ == Stage::kRacing && stage_time_ < kGoDuration) {
    return 0;
  }
  return -1;
}

void RaceQuest::StartConversation() {
  std::vector<std::string> pages;
  if (HasCompleted()) {
    pages.push_back(
        "Well, if it isn't the champion! Your best time around the island is " +
        FormatTime(best_time_) + ".");
    pages.push_back(
        "Think you can be even faster? Would you like to try again and improve "
        "your time?");
  } else if (outcome_ != Outcome::kNone) {
    switch (outcome_) {
      case Outcome::kTooSlow:
        pages.push_back(
            "Two minutes go by fast out there, don't they? Don't worry, nobody "
            "makes it on the first try.");
        break;
      case Outcome::kLeftTrack:
        pages.push_back(
            "Whoa, that was not the track! No shortcuts through the forest, "
            "the lap goes along the trail.");
        break;
      default:
        pages.push_back(
            "A swim in the middle of the race? Leave that to us beavers! The "
            "water is the end of the race.");
        break;
    }
    pages.push_back("Want to give the race track another shot?");
  } else {
    pages.push_back(
        "Oh, hello there, young bear! I'm Bober. Lovely day for a run, isn't "
        "it?");
    pages.push_back(
        "This is the race track around the island. One lap: through all the "
        "checkpoints, then back across this line.");
    pages.push_back(
        "You have two minutes. Stay on the track, keep out of the water, and "
        "mind the hedgehogs!");
    pages.push_back("So... would you like to give the race track a shot?");
  }
  dialog_.Open(kSpeaker, pages, {"Yes", "No"});
  SetStage(Stage::kTalking);
}

void RaceQuest::Fail(Outcome reason) {
  outcome_ = reason;
  outcome_age_ = 0.0f;
  new_best_ = false;
  SetStage(Stage::kIdle);
  events_.push_back(Event::kFailed);
}

void RaceQuest::Update(float time_delta, const Situation& situation) {
  if (time_delta <= 0.0f) {
    return;
  }
  stage_time_ += time_delta;
  outcome_age_ += time_delta;
  near_beaver_ = situation.near_beaver;
  busy_ = situation.busy;
  // The button that answered the beaver does not start the next conversation
  // right away
  const Stage before = stage_;

  // The conversation, or a line the beaver says in passing
  dialog_.Update(time_delta, situation.dialog);
  for (int sound = dialog_.TakeSounds(); sound > 0; --sound) {
    events_.push_back(Event::kBeaverSound);
  }
  int choice;
  if (dialog_.TakeEnded(choice) && stage_ == Stage::kTalking) {
    if (choice == kYes) {
      events_.push_back(Event::kAccepted);
      dialog_.Say(kSpeaker,
                  HasCompleted() || outcome_ != Outcome::kNone
                      ? "That's the spirit! Off to the starting line!"
                      : "Splendid! Off to the starting line with you!");
      SetStage(Stage::kWalkingToStart);
    } else {
      events_.push_back(Event::kDeclined);
      dialog_.Say(kSpeaker,
                  "Suit yourself. I'll be right here, nibbling on something.");
      SetStage(Stage::kIdle);
    }
  }

  switch (stage_) {
    case Stage::kIdle:
      if (situation.interact && before == Stage::kIdle && CanTalk()) {
        if (busy_) {
          dialog_.Say(kSpeaker,
                      "You look busy! Come back for a race when you're done.");
        } else {
          StartConversation();
        }
      }
      break;
    case Stage::kTalking:
      break;
    case Stage::kWalkingToStart:
      if (situation.at_start || stage_time_ > kWalkTimeout) {
        SetStage(Stage::kCountdown);
        countdown_ = static_cast<float>(kCountdownSeconds);
        events_.push_back(Event::kCountdownTick);
      }
      break;
    case Stage::kCountdown: {
      int before = CountdownNumber(countdown_);
      countdown_ = std::max(0.0f, countdown_ - time_delta);
      if (CountdownNumber(countdown_) <= 0) {
        SetStage(Stage::kRacing);
        race_time_ = 0.0f;
        next_checkpoint_ = 0;
        outcome_ = Outcome::kNone;
        new_best_ = false;
        events_.push_back(Event::kStart);
      } else if (CountdownNumber(countdown_) < before) {
        events_.push_back(Event::kCountdownTick);
      }
      break;
    }
    case Stage::kRacing: {
      race_time_ += time_delta;
      std::size_t passed =
          rules::PassCheckpoint(situation.bear, next_checkpoint_);
      if (passed != next_checkpoint_) {
        next_checkpoint_ = passed;
        events_.push_back(Event::kCheckpoint);
      }
      if (rules::HasCompletedLap(next_checkpoint_, situation.bear,
                                 finish_line_)) {
        new_best_ = completions_ == 0 || race_time_ < best_time_;
        best_time_ = new_best_ ? race_time_ : best_time_;
        ++completions_;
        outcome_ = Outcome::kCompleted;
        outcome_age_ = 0.0f;
        SetStage(Stage::kIdle);
        events_.push_back(Event::kCompleted);
      } else if (situation.swimming) {
        Fail(Outcome::kFellIntoWater);
      } else if (!situation.on_track) {
        Fail(Outcome::kLeftTrack);
      } else if (race_time_ > kTimeLimit) {
        race_time_ = kTimeLimit;
        Fail(Outcome::kTooSlow);
      }
      break;
    }
  }
}

std::vector<RaceQuest::Event> RaceQuest::TakeEvents() {
  std::vector<Event> result;
  result.swap(events_);
  return result;
}

}  // namespace bibo::scene

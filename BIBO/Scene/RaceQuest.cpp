#include "RaceQuest.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace bibo;
using namespace bibo::scene;

namespace {
// The bear stands this far behind the finish line at the start
const float START_BEHIND_LINE = 1.0f;
const int YES = 0;

/**
 * The number of the countdown that is shown (3 from 3 to 2 seconds before the
 * start, ...), robust against rounding
 */
int countdownNumber(float secondsLeft) {
  return static_cast<int>(std::ceil(secondsLeft - 1e-3f));
}
}  // namespace

RaceQuest::RaceQuest(const rules::FinishLine& finishLine,
                     glm::vec3 startDirection)
    : finishLine(finishLine) {
  glm::vec3 direction(startDirection.x, 0.0f, startDirection.z);
  this->startDirection = glm::length(direction) > 1e-6f
                             ? glm::normalize(direction)
                             : glm::vec3(0.0f, 0.0f, -1.0f);
  startPosition = finishLine.center - this->startDirection * START_BEHIND_LINE;
}

bool RaceQuest::isOnTrack(const world::TrackPath& path, glm::vec3 position) {
  world::TrackPath::Nearest nearest =
      path.nearest(glm::vec2(position.x, position.z));
  return nearest.found &&
         std::abs(nearest.lateral) <= nearest.floorHalfWidth + OFF_TRACK_MARGIN;
}

std::string RaceQuest::formatTime(float seconds) {
  int hundredths =
      static_cast<int>(std::floor(std::max(seconds, 0.0f) * 100.0f));
  char text[32];
  std::snprintf(text, sizeof(text), "%d:%02d.%02d", hundredths / 6000,
                hundredths / 100 % 60, hundredths % 100);
  return text;
}

void RaceQuest::setStage(Stage newStage) {
  stage = newStage;
  stageTime = 0.0f;
}

void RaceQuest::abort() {
  dialog.close();
  int choice;
  dialog.takeEnded(choice);
  dialog.takeSounds();
  setStage(Stage::IDLE);
  countdown = 0.0f;
  raceTime = 0.0f;
  nextCheckpoint = 0;
  outcome = Outcome::NONE;
  outcomeAge = 0.0f;
  newBest = false;
  events.clear();
}

int RaceQuest::getCountdownNumber() const {
  if (stage == Stage::COUNTDOWN) {
    return std::clamp(countdownNumber(countdown), 1, COUNTDOWN_SECONDS);
  }
  if (stage == Stage::RACING && stageTime < GO_DURATION) {
    return 0;
  }
  return -1;
}

void RaceQuest::startConversation() {
  std::vector<std::string> pages;
  if (hasCompleted()) {
    pages.push_back(
        "Well, if it isn't the champion! Your best time around the island is " +
        formatTime(bestTime) + ".");
    pages.push_back(
        "Think you can be even faster? Would you like to try again and improve "
        "your time?");
  } else if (outcome != Outcome::NONE) {
    switch (outcome) {
      case Outcome::TOO_SLOW:
        pages.push_back(
            "Two minutes go by fast out there, don't they? Don't worry, nobody "
            "makes it on the first try.");
        break;
      case Outcome::LEFT_TRACK:
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
  dialog.open(SPEAKER, pages, {"Yes", "No"});
  setStage(Stage::TALKING);
}

void RaceQuest::fail(Outcome reason) {
  outcome = reason;
  outcomeAge = 0.0f;
  newBest = false;
  setStage(Stage::IDLE);
  events.push_back(Event::FAILED);
}

void RaceQuest::update(float timeDelta, const Situation& situation) {
  if (timeDelta <= 0.0f) {
    return;
  }
  stageTime += timeDelta;
  outcomeAge += timeDelta;
  nearBeaver = situation.nearBeaver;
  busy = situation.busy;
  // The button that answered the beaver does not start the next conversation
  // right away
  const Stage before = stage;

  // The conversation, or a line the beaver says in passing
  dialog.update(timeDelta, situation.dialog);
  for (int sound = dialog.takeSounds(); sound > 0; --sound) {
    events.push_back(Event::BEAVER_SOUND);
  }
  int choice;
  if (dialog.takeEnded(choice) && stage == Stage::TALKING) {
    if (choice == YES) {
      events.push_back(Event::ACCEPTED);
      dialog.say(SPEAKER, hasCompleted() || outcome != Outcome::NONE
                              ? "That's the spirit! Off to the starting line!"
                              : "Splendid! Off to the starting line with you!");
      setStage(Stage::WALKING_TO_START);
    } else {
      events.push_back(Event::DECLINED);
      dialog.say(SPEAKER,
                 "Suit yourself. I'll be right here, nibbling on something.");
      setStage(Stage::IDLE);
    }
  }

  switch (stage) {
    case Stage::IDLE:
      if (situation.interact && before == Stage::IDLE && canTalk()) {
        if (busy) {
          dialog.say(SPEAKER,
                     "You look busy! Come back for a race when you're done.");
        } else {
          startConversation();
        }
      }
      break;
    case Stage::TALKING:
      break;
    case Stage::WALKING_TO_START:
      if (situation.atStart || stageTime > WALK_TIMEOUT) {
        setStage(Stage::COUNTDOWN);
        countdown = static_cast<float>(COUNTDOWN_SECONDS);
        events.push_back(Event::COUNTDOWN_TICK);
      }
      break;
    case Stage::COUNTDOWN: {
      int before = countdownNumber(countdown);
      countdown = std::max(0.0f, countdown - timeDelta);
      if (countdownNumber(countdown) <= 0) {
        setStage(Stage::RACING);
        raceTime = 0.0f;
        nextCheckpoint = 0;
        outcome = Outcome::NONE;
        newBest = false;
        events.push_back(Event::START);
      } else if (countdownNumber(countdown) < before) {
        events.push_back(Event::COUNTDOWN_TICK);
      }
      break;
    }
    case Stage::RACING: {
      raceTime += timeDelta;
      std::size_t passed =
          rules::passCheckpoint(situation.bear, nextCheckpoint);
      if (passed != nextCheckpoint) {
        nextCheckpoint = passed;
        events.push_back(Event::CHECKPOINT);
      }
      if (rules::hasCompletedLap(nextCheckpoint, situation.bear, finishLine)) {
        newBest = completions == 0 || raceTime < bestTime;
        bestTime = newBest ? raceTime : bestTime;
        ++completions;
        outcome = Outcome::COMPLETED;
        outcomeAge = 0.0f;
        setStage(Stage::IDLE);
        events.push_back(Event::COMPLETED);
      } else if (situation.swimming) {
        fail(Outcome::FELL_INTO_WATER);
      } else if (!situation.onTrack) {
        fail(Outcome::LEFT_TRACK);
      } else if (raceTime > TIME_LIMIT) {
        raceTime = TIME_LIMIT;
        fail(Outcome::TOO_SLOW);
      }
      break;
    }
  }
}

std::vector<RaceQuest::Event> RaceQuest::takeEvents() {
  std::vector<Event> result;
  result.swap(events);
  return result;
}

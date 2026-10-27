#include "SalmonQuest.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "RaceQuest.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace {
const int YES = 0;
// During the quest: go on (also what cancel chooses) or stop fishing
const int KEEP_FISHING = 0;

std::string numberWord(int number) {
  const char* const WORDS[] = {"no",  "one",   "two",   "three", "four", "five",
                               "six", "seven", "eight", "nine",  "ten"};
  return number >= 0 && number <= 10 ? WORDS[number] : std::to_string(number);
}
}  // namespace

SalmonQuest::SalmonQuest(int poolCount)
    : done(static_cast<std::size_t>(std::max(poolCount, 0)), false) {}

void SalmonQuest::setStage(Stage newStage) { stage = newStage; }

void SalmonQuest::abort() {
  dialog.close();
  int choice;
  dialog.takeEnded(choice);
  dialog.takeSounds();
  setStage(Stage::IDLE);
  active = false;
  done.assign(done.size(), false);
  eaten = 0;
  time = 0.0f;
  outcome = Outcome::NONE;
  outcomeAge = 0.0f;
  newBest = false;
  events.clear();
}

void SalmonQuest::startConversation() {
  const int pools = getPoolCount();
  std::vector<std::string> pages;
  if (active) {
    int left = pools - eaten;
    if (eaten == 0) {
      pages.push_back(
          "No salmon yet? Jump over the water when they leap and snatch one "
          "out of the air!");
    } else {
      std::string remaining =
          numberWord(left) + (left == 1 ? " pool" : " pools");
      remaining[0] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(remaining[0])));
      pages.push_back("Mmm, " + numberWord(eaten) + " salmon so far. " +
                      remaining + " to go!");
    }
    pages.push_back("Keep on fishing?");
    dialog.open(SPEAKER, pages, {"Keep fishing", "Stop fishing"}, KEEP_FISHING);
    setStage(Stage::TALKING);
    return;
  }
  if (hasCompleted()) {
    pages.push_back("You again! A salmon from every pool in " +
                    RaceQuest::formatTime(bestTime) +
                    ". My mouth still waters thinking of it.");
    pages.push_back("Want to go fishing again and be even quicker?");
  } else if (outcome == Outcome::STOPPED) {
    pages.push_back("Hungry again? The salmon are still jumping.");
    pages.push_back("Want to go fishing once more?");
  } else {
    pages.push_back(
        "Mmm... hello there, little one. I'm Mishka. Shh, look: the salmon are "
        "jumping.");
    pages.push_back("There are " + numberWord(pools) +
                    " pools of water along the track. When you come close, the "
                    "salmon leap out of them.");
    pages.push_back(
        "Catch one from every pool and eat it up. Each one is worth " +
        std::to_string(SALMON_POINTS) +
        " points, but a salmon you drop doesn't count!");
    pages.push_back("So... would you like to go fishing?");
  }
  dialog.open(SPEAKER, pages, {"Yes", "No"});
  setStage(Stage::TALKING);
}

void SalmonQuest::finish(Outcome result) {
  outcome = result;
  outcomeAge = 0.0f;
  active = false;
  if (result == Outcome::COMPLETED) {
    newBest = completions == 0 || time < bestTime;
    bestTime = newBest ? time : bestTime;
    ++completions;
    events.push_back(Event::COMPLETED);
  } else {
    newBest = false;
    events.push_back(Event::STOPPED);
  }
}

void SalmonQuest::update(float timeDelta, const Situation& situation) {
  if (timeDelta <= 0.0f) {
    return;
  }
  outcomeAge += timeDelta;
  nearMishka = situation.nearMishka;
  busy = situation.busy;
  // The button that answered Mishka does not start the next conversation right
  // away
  const Stage before = stage;

  // The conversation, or a line Mishka says in passing
  dialog.update(timeDelta, situation.dialog);
  for (int sound = dialog.takeSounds(); sound > 0; --sound) {
    events.push_back(Event::BEAR_SOUND);
  }
  int choice;
  if (dialog.takeEnded(choice) && stage == Stage::TALKING) {
    if (active) {
      if (choice == KEEP_FISHING || choice < 0) {
        dialog.say(SPEAKER,
                   "Off you go, then. The salmon won't catch themselves!");
        setStage(Stage::FISHING);
      } else {
        dialog.say(SPEAKER, "Full already? Come back whenever you're hungry.");
        finish(Outcome::STOPPED);
        setStage(Stage::IDLE);
      }
    } else if (choice == YES) {
      events.push_back(Event::ACCEPTED);
      dialog.say(SPEAKER, "Wonderful! Go on, and leave a few for me.");
      active = true;
      done.assign(done.size(), false);
      eaten = 0;
      time = 0.0f;
      outcome = Outcome::NONE;
      newBest = false;
      setStage(Stage::FISHING);
    } else {
      events.push_back(Event::DECLINED);
      dialog.say(SPEAKER, "Mmm, maybe later. I'll be right here, watching.");
      setStage(Stage::IDLE);
    }
  }

  // A salmon of a pool that is not done yet (the bear may also eat it while it
  // talks, e.g. caught right before)
  if (active && situation.eatenPool >= 0 &&
      situation.eatenPool < getPoolCount() && !done[situation.eatenPool]) {
    done[situation.eatenPool] = true;
    ++eaten;
    events.push_back(Event::EATEN);
    if (eaten == getPoolCount()) {
      if (stage == Stage::TALKING) {
        dialog.close();
        dialog.takeEnded(choice);
      }
      finish(Outcome::COMPLETED);
      setStage(Stage::IDLE);
    }
  }

  switch (stage) {
    case Stage::IDLE:
    case Stage::FISHING:
      if (situation.interact && before == stage && canTalk()) {
        startConversation();
      }
      break;
    case Stage::TALKING:
      break;
  }
  if (stage == Stage::FISHING && before == Stage::FISHING) {
    time += timeDelta;
  }
}

std::vector<SalmonQuest::Event> SalmonQuest::takeEvents() {
  std::vector<Event> result;
  result.swap(events);
  return result;
}

#include "SalmonQuest.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "RaceQuest.hpp"

namespace bibo::scene {

namespace {
const int kYes = 0;
// During the quest: go on (also what cancel chooses) or stop fishing
const int kKeepFishing = 0;

std::string NumberWord(int number) {
  const char* const words[] = {"no",  "one",   "two",   "three", "four", "five",
                               "six", "seven", "eight", "nine",  "ten"};
  return number >= 0 && number <= 10 ? words[number] : std::to_string(number);
}
}  // namespace

SalmonQuest::SalmonQuest(int pool_count)
    : done_(static_cast<std::size_t>(std::max(pool_count, 0)), false) {}

void SalmonQuest::SetStage(Stage new_stage) { stage_ = new_stage; }

void SalmonQuest::Abort() {
  dialog_.Close();
  int choice;
  dialog_.TakeEnded(choice);
  dialog_.TakeSounds();
  SetStage(Stage::kIdle);
  active_ = false;
  done_.assign(done_.size(), false);
  eaten_ = 0;
  time_ = 0.0f;
  outcome_ = Outcome::kNone;
  outcome_age_ = 0.0f;
  new_best_ = false;
  events_.clear();
}

void SalmonQuest::StartConversation() {
  const int pools = GetPoolCount();
  std::vector<std::string> pages;
  if (active_) {
    int left = pools - eaten_;
    if (eaten_ == 0) {
      pages.push_back(
          "No salmon yet? Jump over the water when they leap and snatch one "
          "out of the air!");
    } else {
      std::string remaining =
          NumberWord(left) + (left == 1 ? " pool" : " pools");
      remaining[0] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(remaining[0])));
      pages.push_back("Mmm, " + NumberWord(eaten_) + " salmon so far. " +
                      remaining + " to go!");
    }
    pages.push_back("Keep on fishing?");
    dialog_.Open(kSpeaker, pages, {"Keep fishing", "Stop fishing"},
                 kKeepFishing);
    SetStage(Stage::kTalking);
    return;
  }
  if (HasCompleted()) {
    pages.push_back("You again! A salmon from every pool in " +
                    RaceQuest::FormatTime(best_time_) +
                    ". My mouth still waters thinking of it.");
    pages.push_back("Want to go fishing again and be even quicker?");
  } else if (outcome_ == Outcome::kStopped) {
    pages.push_back("Hungry again? The salmon are still jumping.");
    pages.push_back("Want to go fishing once more?");
  } else {
    pages.push_back(
        "Mmm... hello there, little one. I'm Mishka. Shh, look: the salmon are "
        "jumping.");
    pages.push_back("There are " + NumberWord(pools) +
                    " pools of water along the track. When you come close, the "
                    "salmon leap out of them.");
    pages.push_back(
        "Catch one from every pool and eat it up. Each one is worth " +
        std::to_string(kSalmonPoints) +
        " points, but a salmon you drop doesn't count!");
    pages.push_back("So... would you like to go fishing?");
  }
  dialog_.Open(kSpeaker, pages, {"Yes", "No"});
  SetStage(Stage::kTalking);
}

void SalmonQuest::Finish(Outcome result) {
  outcome_ = result;
  outcome_age_ = 0.0f;
  active_ = false;
  if (result == Outcome::kCompleted) {
    new_best_ = completions_ == 0 || time_ < best_time_;
    best_time_ = new_best_ ? time_ : best_time_;
    ++completions_;
    events_.push_back(Event::kCompleted);
  } else {
    new_best_ = false;
    events_.push_back(Event::kStopped);
  }
}

void SalmonQuest::Update(float time_delta, const Situation& situation) {
  if (time_delta <= 0.0f) {
    return;
  }
  outcome_age_ += time_delta;
  near_mishka_ = situation.near_mishka;
  busy_ = situation.busy;
  // The button that answered Mishka does not start the next conversation right
  // away
  const Stage before = stage_;

  // The conversation, or a line Mishka says in passing
  dialog_.Update(time_delta, situation.dialog);
  for (int sound = dialog_.TakeSounds(); sound > 0; --sound) {
    events_.push_back(Event::kBearSound);
  }
  int choice;
  if (dialog_.TakeEnded(choice) && stage_ == Stage::kTalking) {
    if (active_) {
      if (choice == kKeepFishing || choice < 0) {
        dialog_.Say(kSpeaker,
                    "Off you go, then. The salmon won't catch themselves!");
        SetStage(Stage::kFishing);
      } else {
        dialog_.Say(kSpeaker,
                    "Full already? Come back whenever you're hungry.");
        Finish(Outcome::kStopped);
        SetStage(Stage::kIdle);
      }
    } else if (choice == kYes) {
      events_.push_back(Event::kAccepted);
      dialog_.Say(kSpeaker, "Wonderful! Go on, and leave a few for me.");
      active_ = true;
      done_.assign(done_.size(), false);
      eaten_ = 0;
      time_ = 0.0f;
      outcome_ = Outcome::kNone;
      new_best_ = false;
      SetStage(Stage::kFishing);
    } else {
      events_.push_back(Event::kDeclined);
      dialog_.Say(kSpeaker, "Mmm, maybe later. I'll be right here, watching.");
      SetStage(Stage::kIdle);
    }
  }

  // A salmon of a pool that is not done yet (the bear may also eat it while it
  // talks, e.g. caught right before)
  if (active_ && situation.eaten_pool >= 0 &&
      situation.eaten_pool < GetPoolCount() && !done_[situation.eaten_pool]) {
    done_[situation.eaten_pool] = true;
    ++eaten_;
    events_.push_back(Event::kEaten);
    if (eaten_ == GetPoolCount()) {
      if (stage_ == Stage::kTalking) {
        dialog_.Close();
        dialog_.TakeEnded(choice);
      }
      Finish(Outcome::kCompleted);
      SetStage(Stage::kIdle);
    }
  }

  switch (stage_) {
    case Stage::kIdle:
    case Stage::kFishing:
      if (situation.interact && before == stage_ && CanTalk()) {
        StartConversation();
      }
      break;
    case Stage::kTalking:
      break;
  }
  if (stage_ == Stage::kFishing && before == Stage::kFishing) {
    time_ += time_delta;
  }
}

std::vector<SalmonQuest::Event> SalmonQuest::TakeEvents() {
  std::vector<Event> result;
  result.swap(events_);
  return result;
}

}  // namespace bibo::scene

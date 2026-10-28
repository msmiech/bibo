#include "StonesQuest.hpp"

namespace bibo::scene {

namespace {
const int kYes = 0;
// During the quest: go on (also what cancel chooses) or give up
const int kGoOn = 0;
// The report: what the bear found (also what cancel chooses) or a tall tale
const int kTruth = 0;
}  // namespace

bool StonesQuest::CanTalk() const {
  return stage_ != Stage::kTalking && stage_ != Stage::kInspecting &&
         (!busy_ || active_) && near_chonk_ &&
         (!dialog_.IsOpen() || dialog_.IsPassing());
}

void StonesQuest::Abort() {
  dialog_.Close();
  int choice;
  dialog_.TakeEnded(choice);
  dialog_.TakeSounds();
  stage_ = Stage::kIdle;
  active_ = arrived_ = reporting_ = false;
  outcome_ = Outcome::kNone;
  outcome_age_ = 0.0f;
  events_.clear();
}

std::string StonesQuest::GetObjective() const {
  if (!active_) {
    return "";
  }
  if (stage_ == Stage::kReturning ||
      (stage_ == Stage::kTalking && reporting_)) {
    return "Report back to Chonk";
  }
  return arrived_ ? "Inspect the flat stone in the middle of the circle"
                  : "Explore the stone circle on the hill";
}

void StonesQuest::StartConversation() {
  std::vector<std::string> pages;
  if (stage_ == Stage::kReturning) {
    pages.push_back("You're back! And all in one piece!");
    pages.push_back("So? What's happening up at the stones?");
    dialog_.Open(
        kSpeaker, pages,
        {"Wind, glow-worms and a snack stash", "The stones are haunted!"},
        kTruth);
    stage_ = Stage::kTalking;
    reporting_ = true;
    return;
  }
  if (active_) {
    pages.push_back(arrived_
                        ? "You found the circle? Then have a close look at "
                          "the flat stone in the middle!"
                        : "Still here? The stone circle is up the paths, "
                          "on top of the hill. Just follow them uphill!");
    pages.push_back("You're not giving up, are you?");
    dialog_.Open(kSpeaker, pages, {"On my way", "I give up"}, kGoOn);
    stage_ = Stage::kTalking;
    return;
  }
  if (HasCompleted()) {
    pages.push_back(report_ == Report::kTruth
                        ? "Mmm, acorns from my long lost stash. Thanks again "
                          "for the report, friend!"
                        : "I went up there after all. Just wind and "
                          "glow-worms! You fibber... thanks anyway, friend.");
    dialog_.Open(kSpeaker, pages);
    stage_ = Stage::kTalking;
    return;
  }
  if (outcome_ == Outcome::kStopped) {
    pages.push_back(
        "Oh, it's you again. The stones are still humming every night...");
    pages.push_back("Feeling brave today? Will you go and have a look?");
  } else {
    pages.push_back(
        "Psst! Down here. The name's Chonk. Don't mind the belly, it's mostly "
        "courage.");
    pages.push_back(
        "Every night the old standing stones on the hill start to hum, and "
        "little lights dance between them.");
    pages.push_back(
        "The other raccoons won't go near them. They say the stones are "
        "haunted... and I'm, uh, far too busy to look.");
    pages.push_back(
        "You look brave. Would you climb up to the stone circle, have a good "
        "look at the flat stone in the middle and tell me what's going on?");
  }
  dialog_.Open(kSpeaker, pages, {"I'll go", "Not now"});
  stage_ = Stage::kTalking;
}

void StonesQuest::StartInspection() {
  std::vector<std::string> pages;
  pages.push_back(
      "The flat stone is warm from the sun and covered in tiny paw prints. "
      "Five long fingers... raccoon paws!");
  pages.push_back(
      "The wind whistles through the holes in the standing stones: that's the "
      "humming. Glow-worms shine in the moss under the flat stone: those are "
      "the little lights.");
  pages.push_back(
      "And tucked under the stone: a heap of acorns, berries, shiny bottle "
      "caps and the bones of a salmon. Someone's secret snack stash!");
  pages.push_back("Time to go back and tell Chonk what is happening up here.");
  dialog_.Open(kStones, pages);
  stage_ = Stage::kInspecting;
}

void StonesQuest::Finish(Outcome result) {
  outcome_ = result;
  outcome_age_ = 0.0f;
  active_ = arrived_ = reporting_ = false;
  if (result == Outcome::kCompleted) {
    ++completions_;
    events_.push_back(Event::kCompleted);
  } else {
    events_.push_back(Event::kStopped);
  }
}

void StonesQuest::Update(float time_delta, const Situation& situation) {
  if (time_delta <= 0.0f) {
    return;
  }
  outcome_age_ += time_delta;
  near_chonk_ = situation.near_chonk;
  near_stone_ = situation.near_stone;
  busy_ = situation.busy;
  // The button that answered does not start the next conversation right away
  const Stage before = stage_;

  dialog_.Update(time_delta, situation.dialog);
  for (int sound = dialog_.TakeSounds(); sound > 0; --sound) {
    if (stage_ != Stage::kInspecting && dialog_.GetSpeaker() == kSpeaker) {
      events_.push_back(Event::kChonkSound);
    }
  }
  int choice;
  if (dialog_.TakeEnded(choice)) {
    if (stage_ == Stage::kInspecting) {
      events_.push_back(Event::kInspected);
      stage_ = Stage::kReturning;
    } else if (stage_ == Stage::kTalking && reporting_ &&
               report_ == Report::kNone) {
      // Chonk answers the report, the quest is complete at the end of the
      // answer
      report_ =
          choice == kTruth || choice < 0 ? Report::kTruth : Report::kHaunted;
      std::vector<std::string> pages;
      if (report_ == Report::kTruth) {
        pages.push_back(
            "Wind and glow-worms? Ha! I knew it all along. Totally knew it.");
        pages.push_back(
            "And a snack stash under the flat stone, with acorns and bottle "
            "caps... wait. THAT'S MY STASH! I hid it up there last autumn and "
            "forgot where!");
        pages.push_back(
            "Thank you, friend. Now if you'll excuse me, I have some very "
            "important snacking to do.");
      } else {
        pages.push_back(
            "H-haunted?! I knew it! I'm never, ever going up there again...");
        pages.push_back(
            "...wait. Are you grinning? You're teasing me! I bet it's just the "
            "wind.");
        pages.push_back(
            "Thanks for checking, friend. I'll go and see for myself. Right "
            "after a snack.");
      }
      dialog_.Open(kSpeaker, pages);
    } else if (stage_ == Stage::kTalking && reporting_) {
      Finish(Outcome::kCompleted);
      stage_ = Stage::kIdle;
    } else if (stage_ == Stage::kTalking && active_) {
      if (choice == kGoOn || choice < 0) {
        dialog_.Say(
            kSpeaker,
            "That's the spirit! I'll be right here. Guarding. Bravely.");
        stage_ = Stage::kExploring;
      } else {
        dialog_.Say(kSpeaker,
                    "Oh. Well... the stones aren't going anywhere, I suppose.");
        Finish(Outcome::kStopped);
        stage_ = Stage::kIdle;
      }
    } else if (stage_ == Stage::kTalking && HasCompleted()) {
      stage_ = Stage::kIdle;
    } else if (stage_ == Stage::kTalking) {
      if (choice == kYes) {
        events_.push_back(Event::kAccepted);
        dialog_.Say(kSpeaker,
                    "You will? The paths behind me lead uphill. Off you go, "
                    "and... be careful!");
        active_ = true;
        arrived_ = false;
        reporting_ = false;
        report_ = Report::kNone;
        outcome_ = Outcome::kNone;
        stage_ = Stage::kExploring;
      } else {
        events_.push_back(Event::kDeclined);
        dialog_.Say(kSpeaker,
                    "Fine, fine. I'll just keep guarding these bushes, then.");
        stage_ = Stage::kIdle;
      }
    }
  }

  if (active_ && !arrived_ && situation.at_stones) {
    arrived_ = true;
    events_.push_back(Event::kArrived);
  }

  if (situation.interact && before == stage_ && stage_ != Stage::kTalking &&
      stage_ != Stage::kInspecting) {
    if (CanInspect()) {
      StartInspection();
    } else if (CanTalk()) {
      StartConversation();
    }
  }
}

std::vector<StonesQuest::Event> StonesQuest::TakeEvents() {
  std::vector<Event> result;
  result.swap(events_);
  return result;
}

}  // namespace bibo::scene

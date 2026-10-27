#include "StonesQuest.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace {
const int YES = 0;
// During the quest: go on (also what cancel chooses) or give up
const int GO_ON = 0;
// The report: what the bear found (also what cancel chooses) or a tall tale
const int TRUTH = 0;
}  // namespace

bool StonesQuest::canTalk() const {
  return stage != Stage::TALKING && stage != Stage::INSPECTING &&
         (!busy || active) && nearChonk &&
         (!dialog.isOpen() || dialog.isPassing());
}

void StonesQuest::abort() {
  dialog.close();
  int choice;
  dialog.takeEnded(choice);
  dialog.takeSounds();
  stage = Stage::IDLE;
  active = arrived = reporting = false;
  outcome = Outcome::NONE;
  outcomeAge = 0.0f;
  events.clear();
}

std::string StonesQuest::getObjective() const {
  if (!active) {
    return "";
  }
  if (stage == Stage::RETURNING || (stage == Stage::TALKING && reporting)) {
    return "Report back to Chonk";
  }
  return arrived ? "Inspect the flat stone in the middle of the circle"
                 : "Explore the stone circle on the hill";
}

void StonesQuest::startConversation() {
  std::vector<std::string> pages;
  if (stage == Stage::RETURNING) {
    pages.push_back("You're back! And all in one piece!");
    pages.push_back("So? What's happening up at the stones?");
    dialog.open(
        SPEAKER, pages,
        {"Wind, glow-worms and a snack stash", "The stones are haunted!"},
        TRUTH);
    stage = Stage::TALKING;
    reporting = true;
    return;
  }
  if (active) {
    pages.push_back(arrived ? "You found the circle? Then have a close look at "
                              "the flat stone in the middle!"
                            : "Still here? The stone circle is up the paths, "
                              "on top of the hill. Just follow them uphill!");
    pages.push_back("You're not giving up, are you?");
    dialog.open(SPEAKER, pages, {"On my way", "I give up"}, GO_ON);
    stage = Stage::TALKING;
    return;
  }
  if (hasCompleted()) {
    pages.push_back(report == Report::TRUTH
                        ? "Mmm, acorns from my long lost stash. Thanks again "
                          "for the report, friend!"
                        : "I went up there after all. Just wind and "
                          "glow-worms! You fibber... thanks anyway, friend.");
    dialog.open(SPEAKER, pages);
    stage = Stage::TALKING;
    return;
  }
  if (outcome == Outcome::STOPPED) {
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
  dialog.open(SPEAKER, pages, {"I'll go", "Not now"});
  stage = Stage::TALKING;
}

void StonesQuest::startInspection() {
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
  dialog.open(STONES, pages);
  stage = Stage::INSPECTING;
}

void StonesQuest::finish(Outcome result) {
  outcome = result;
  outcomeAge = 0.0f;
  active = arrived = reporting = false;
  if (result == Outcome::COMPLETED) {
    ++completions;
    events.push_back(Event::COMPLETED);
  } else {
    events.push_back(Event::STOPPED);
  }
}

void StonesQuest::update(float timeDelta, const Situation& situation) {
  if (timeDelta <= 0.0f) {
    return;
  }
  outcomeAge += timeDelta;
  nearChonk = situation.nearChonk;
  nearStone = situation.nearStone;
  busy = situation.busy;
  // The button that answered does not start the next conversation right away
  const Stage before = stage;

  dialog.update(timeDelta, situation.dialog);
  for (int sound = dialog.takeSounds(); sound > 0; --sound) {
    if (stage != Stage::INSPECTING && dialog.getSpeaker() == SPEAKER) {
      events.push_back(Event::CHONK_SOUND);
    }
  }
  int choice;
  if (dialog.takeEnded(choice)) {
    if (stage == Stage::INSPECTING) {
      events.push_back(Event::INSPECTED);
      stage = Stage::RETURNING;
    } else if (stage == Stage::TALKING && reporting && report == Report::NONE) {
      // Chonk answers the report, the quest is complete at the end of the
      // answer
      report = choice == TRUTH || choice < 0 ? Report::TRUTH : Report::HAUNTED;
      std::vector<std::string> pages;
      if (report == Report::TRUTH) {
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
      dialog.open(SPEAKER, pages);
    } else if (stage == Stage::TALKING && reporting) {
      finish(Outcome::COMPLETED);
      stage = Stage::IDLE;
    } else if (stage == Stage::TALKING && active) {
      if (choice == GO_ON || choice < 0) {
        dialog.say(SPEAKER,
                   "That's the spirit! I'll be right here. Guarding. Bravely.");
        stage = Stage::EXPLORING;
      } else {
        dialog.say(SPEAKER,
                   "Oh. Well... the stones aren't going anywhere, I suppose.");
        finish(Outcome::STOPPED);
        stage = Stage::IDLE;
      }
    } else if (stage == Stage::TALKING && hasCompleted()) {
      stage = Stage::IDLE;
    } else if (stage == Stage::TALKING) {
      if (choice == YES) {
        events.push_back(Event::ACCEPTED);
        dialog.say(SPEAKER,
                   "You will? The paths behind me lead uphill. Off you go, "
                   "and... be careful!");
        active = true;
        arrived = false;
        reporting = false;
        report = Report::NONE;
        outcome = Outcome::NONE;
        stage = Stage::EXPLORING;
      } else {
        events.push_back(Event::DECLINED);
        dialog.say(SPEAKER,
                   "Fine, fine. I'll just keep guarding these bushes, then.");
        stage = Stage::IDLE;
      }
    }
  }

  if (active && !arrived && situation.atStones) {
    arrived = true;
    events.push_back(Event::ARRIVED);
  }

  if (situation.interact && before == stage && stage != Stage::TALKING &&
      stage != Stage::INSPECTING) {
    if (canInspect()) {
      startInspection();
    } else if (canTalk()) {
      startConversation();
    }
  }
}

std::vector<StonesQuest::Event> StonesQuest::takeEvents() {
  std::vector<Event> result;
  result.swap(events);
  return result;
}

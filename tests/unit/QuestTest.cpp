#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <vector>

#include "Scene/Dialog.hpp"
#include "Scene/RaceQuest.hpp"

namespace rules = bibo::scene::rules;
using bibo::scene::Dialog;
using bibo::scene::DialogInput;
using bibo::scene::RaceQuest;

using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;

DialogInput Confirm() {
  DialogInput input;
  input.confirm = true;
  return input;
}

DialogInput Navigate(int direction) {
  DialogInput input;
  input.navigate = direction;
  return input;
}

/**
 * Updates until the page has appeared completely
 * @return The sounds the speaker made meanwhile
 */
int FinishPage(Dialog& dialog) {
  int sounds = 0;
  for (int i = 0; i < 60 * 30 && dialog.IsTyping(); ++i) {
    dialog.Update(kFrame, DialogInput());
    sounds += dialog.TakeSounds();
  }
  return sounds;
}

// Finish line across the x axis at the origin, the race goes along -z
rules::FinishLine TestFinishLine() {
  rules::FinishLine line;
  line.center = vec3(0.0f);
  line.across = vec3(1.0f, 0.0f, 0.0f);
  line.half_width = 8.0f;
  return line;
}

/**
 * The quest with the bear next to the beaver at the start
 */
struct Quest {
  RaceQuest quest{TestFinishLine(), vec3(0.0f, 0.0f, -1.0f)};
  RaceQuest::Situation situation;
  std::vector<RaceQuest::Event> events;

  Quest() {
    situation.bear = vec3(5.0f, 1.0f, 4.0f);
    situation.near_beaver = true;
  }

  void Update(float seconds = kFrame) {
    for (float time = 0.0f; time < seconds - 1e-4f; time += kFrame) {
      quest.Update(kFrame, situation);
      situation.interact = false;
      situation.dialog = DialogInput();
      for (RaceQuest::Event event : quest.TakeEvents()) {
        events.push_back(event);
      }
    }
  }

  int Count(RaceQuest::Event event) const {
    int result = 0;
    for (RaceQuest::Event e : events) {
      result += e == event ? 1 : 0;
    }
    return result;
  }

  void Talk() {
    situation.interact = true;
    Update();
  }

  /**
   * Goes through the pages of the conversation and answers
   * @param interact The button of the answer is also the one that starts a
   * conversation (E)
   */
  void Answer(bool yes, bool interact = false) {
    const Dialog& dialog = quest.GetDialog();
    for (int page = 0; page < 10 && !dialog.IsChoosing(); ++page) {
      situation.dialog = Confirm();  // the whole page at once
      Update();
      if (!dialog.IsChoosing()) {
        situation.dialog = Confirm();  // next page
        Update();
      }
    }
    REQUIRE(dialog.IsChoosing());
    if (!yes) {
      situation.dialog = Navigate(1);
      Update();
    }
    situation.dialog = Confirm();
    situation.interact = interact;
    Update();
  }

  /**
   * Accepts the race, walks to the start and waits for the countdown
   */
  void StartRace() {
    Talk();
    Answer(true);
    REQUIRE(quest.GetStage() == RaceQuest::Stage::kWalkingToStart);
    situation.at_start = true;
    situation.bear = quest.GetStartPosition();
    Update();
    REQUIRE(quest.GetStage() == RaceQuest::Stage::kCountdown);
    situation.at_start = false;
    Update(RaceQuest::kCountdownSeconds + 0.05f);
    REQUIRE(quest.GetStage() == RaceQuest::Stage::kRacing);
  }

  /**
   * Runs through the checkpoints and over the finish line
   */
  void RunLap(float seconds) {
    for (std::size_t i = 0; i < rules::kCheckpointCount; ++i) {
      situation.bear =
          vec3(rules::kCheckpoints[i].x, 1.0f, rules::kCheckpoints[i].y);
      Update(seconds / static_cast<float>(rules::kCheckpointCount + 1));
    }
    situation.bear = vec3(0.0f, 1.0f, -0.5f);
    Update();
  }
};
}  // namespace

TEST_CASE(
    "The text of a page appears letter by letter, the speaker makes sounds "
    "meanwhile",
    "[quest][dialog]") {
  Dialog dialog;
  dialog.Open("Beaver", {"Hello there, bear! Nice day.", "Second page."});
  REQUIRE(dialog.IsOpen());
  CHECK(dialog.GetSpeaker() == "Beaver");
  CHECK(dialog.GetText().empty());
  dialog.Update(0.1f, DialogInput());
  CHECK(dialog.GetText() == "Hell");  // 40 letters per second
  CHECK(dialog.TakeSounds() == 1);    // right away
  CHECK(dialog.IsTyping());
  CHECK_FALSE(dialog.CanContinue());

  int sounds = FinishPage(dialog);
  CHECK(dialog.GetText() == "Hello there, bear! Nice day.");
  CHECK(sounds >= 3);
  CHECK(sounds <= 8);
  CHECK(dialog.CanContinue());
  CHECK_FALSE(dialog.IsChoosing());  // no answers

  // Silent while the page is complete
  dialog.Update(1.0f, DialogInput());
  CHECK(dialog.TakeSounds() == 0);

  dialog.Update(kFrame, Confirm());
  CHECK(dialog.GetPageIndex() == 1);
  CHECK(dialog.GetText().empty());

  SECTION(
      "Confirm shows the whole page at once, then closes the conversation "
      "after the last page") {
    dialog.Update(kFrame, Confirm());
    CHECK(dialog.GetText() == "Second page.");
    dialog.Update(kFrame, Confirm());
    CHECK_FALSE(dialog.IsOpen());
    int choice = 5;
    CHECK(dialog.TakeEnded(choice));
    CHECK(choice == -1);
    CHECK_FALSE(dialog.TakeEnded(choice));  // once
  }
}

TEST_CASE("The text pauses after the end of a sentence", "[quest][dialog]") {
  Dialog dialog;
  dialog.Open("Beaver", {"Hi. Ho"});
  dialog.Update(3.0f / Dialog::kCharactersPerSecond + 1e-4f, DialogInput());
  CHECK(dialog.GetText() == "Hi.");
  dialog.Update(0.5f * Dialog::kSentencePause, DialogInput());
  CHECK(dialog.GetText() == "Hi.");
  dialog.Update(Dialog::kSentencePause, DialogInput());
  CHECK(dialog.GetText().size() > 3);
}

TEST_CASE("After the last page the player chooses an answer",
          "[quest][dialog]") {
  Dialog dialog;
  dialog.Open("Beaver", {"Would you like to?"}, {"Yes", "No"});
  CHECK_FALSE(dialog.IsChoosing());  // not before the question appeared
  FinishPage(dialog);
  REQUIRE(dialog.IsChoosing());
  CHECK_FALSE(dialog.CanContinue());
  CHECK(dialog.GetSelectedChoice() == 0);

  int choice = -1;
  SECTION("Yes") {
    dialog.Update(kFrame, Confirm());
    REQUIRE(dialog.TakeEnded(choice));
    CHECK(choice == 0);
  }
  SECTION("The selection goes round") {
    dialog.Update(kFrame, Navigate(1));
    CHECK(dialog.GetSelectedChoice() == 1);
    dialog.Update(kFrame, Navigate(1));
    CHECK(dialog.GetSelectedChoice() == 0);
    dialog.Update(kFrame, Navigate(-1));
    CHECK(dialog.GetSelectedChoice() == 1);
    dialog.Update(kFrame, Confirm());
    REQUIRE(dialog.TakeEnded(choice));
    CHECK(choice == 1);
  }
  SECTION("Cancel answers with the last choice (No)") {
    DialogInput cancel;
    cancel.cancel = true;
    dialog.Update(kFrame, cancel);
    REQUIRE(dialog.TakeEnded(choice));
    CHECK(choice == 1);
  }
  CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("Cancel can answer with another choice than the last one",
          "[quest][dialog]") {
  Dialog dialog;
  dialog.Open("Bear", {"Keep on fishing?"}, {"Keep fishing", "Stop fishing"},
              0);
  FinishPage(dialog);
  REQUIRE(dialog.IsChoosing());
  DialogInput cancel;
  cancel.cancel = true;
  dialog.Update(kFrame, cancel);
  int choice = -1;
  REQUIRE(dialog.TakeEnded(choice));
  CHECK(choice == 0);
}

TEST_CASE("A line said in passing goes away on its own", "[quest][dialog]") {
  Dialog dialog;
  dialog.Say("Beaver", "Splendid!");
  CHECK(dialog.IsPassing());
  dialog.Update(kFrame, Confirm());
  CHECK(dialog.IsTyping());  // the player cannot skip it
  FinishPage(dialog);
  CHECK_FALSE(dialog.CanContinue());
  dialog.Update(Dialog::kLineHoldTime + 0.1f, DialogInput());
  CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("Times are shown as minutes, seconds and hundredths", "[quest]") {
  CHECK(RaceQuest::FormatTime(0.0f) == "0:00.00");
  CHECK(RaceQuest::FormatTime(83.456f) == "1:23.45");
  CHECK(RaceQuest::FormatTime(300.0f) == "5:00.00");
  CHECK(RaceQuest::FormatTime(-1.0f) == "0:00.00");
}

TEST_CASE(
    "The race is a quest of the beaver: talk, accept, walk to the start, "
    "countdown, race",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
  CHECK(quest.IsOffered());
  CHECK(quest.IsPlayerInControl());
  CHECK(quest.GetCountdownNumber() == -1);  // nothing on the screen

  SECTION("Only near the beaver") {
    q.situation.near_beaver = false;
    q.Talk();
    CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
    CHECK_FALSE(quest.CanTalk());
  }
  SECTION(
      "Yes: the bear walks up to the starting line, the countdown starts the "
      "timer") {
    q.Update();
    CHECK(quest.CanTalk());
    q.Talk();
    CHECK(quest.GetStage() == RaceQuest::Stage::kTalking);
    CHECK_FALSE(quest.IsPlayerInControl());
    CHECK_FALSE(quest.IsOffered());
    q.Update(1.0f);
    // it chatters while its words appear
    CHECK(q.Count(RaceQuest::Event::kBeaverSound) > 0);

    q.Answer(true);
    CHECK(q.Count(RaceQuest::Event::kAccepted) == 1);
    CHECK(quest.GetStage() == RaceQuest::Stage::kWalkingToStart);
    CHECK_FALSE(quest.IsPlayerInControl());
    CHECK(quest.GetDialog().IsPassing());  //"Off to the starting line!"
    // Behind the line, looking along the track
    CHECK_THAT(quest.GetStartPosition().z, WithinAbs(1.0f, 1e-4f));
    CHECK(quest.GetStartDirection() == vec3(0.0f, 0.0f, -1.0f));

    q.Update(2.0f);
    CHECK(quest.GetStage() == RaceQuest::Stage::kWalkingToStart);
    q.situation.at_start = true;
    q.Update();
    REQUIRE(quest.GetStage() == RaceQuest::Stage::kCountdown);
    CHECK(quest.GetCountdownNumber() == 3);
    q.Update(1.0f);
    CHECK(quest.GetCountdownNumber() == 2);
    q.Update(1.0f);
    CHECK(quest.GetCountdownNumber() == 1);
    CHECK(quest.GetStage() == RaceQuest::Stage::kCountdown);
    q.Update(1.0f);
    REQUIRE(quest.GetStage() == RaceQuest::Stage::kRacing);
    CHECK(q.Count(RaceQuest::Event::kCountdownTick) == 3);
    CHECK(q.Count(RaceQuest::Event::kStart) == 1);
    CHECK(quest.GetCountdownNumber() == 0);  // GO!
    CHECK(quest.IsPlayerInControl());
    q.Update(RaceQuest::kGoDuration + 0.05f);
    CHECK(quest.GetCountdownNumber() == -1);
    CHECK(quest.GetRaceTime() > 0.9f);
  }
  SECTION("No ends the conversation") {
    q.Talk();
    q.Answer(false);
    CHECK(q.Count(RaceQuest::Event::kDeclined) == 1);
    CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
    CHECK(quest.IsOffered());
    CHECK(quest.IsPlayerInControl());
    CHECK(quest.GetDialog().IsPassing());  //"Suit yourself."
    // The player can talk again
    q.Update(0.5f);
    q.Talk();
    CHECK(quest.GetStage() == RaceQuest::Stage::kTalking);
  }
  SECTION("The button that answered does not start the conversation again") {
    q.Talk();
    q.Answer(false, true);  // E answers and talks
    CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
    q.Update(0.5f);
    CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
  }
  SECTION(
      "The cutscene ends after a while even if the bear does not get to the "
      "start") {
    q.Talk();
    q.Answer(true);
    q.Update(RaceQuest::kWalkTimeout + 0.1f);
    CHECK(quest.GetStage() == RaceQuest::Stage::kCountdown);
  }
}

TEST_CASE(
    "The lap is complete at the finish line after all checkpoints, the beaver "
    "offers to improve the best time",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.StartRace();
  CHECK(quest.GetCheckpointsPassed() == 0);
  CHECK(quest.GetCheckpointCount() == rules::kCheckpointCount);

  // The finish line does not count before the checkpoints
  q.situation.bear = vec3(0.0f, 1.0f, -0.5f);
  q.Update(1.0f);
  CHECK(quest.GetStage() == RaceQuest::Stage::kRacing);

  q.RunLap(90.0f);
  CHECK(q.Count(RaceQuest::Event::kCheckpoint) ==
        static_cast<int>(rules::kCheckpointCount));
  REQUIRE(q.Count(RaceQuest::Event::kCompleted) == 1);
  CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
  CHECK(quest.GetOutcome() == RaceQuest::Outcome::kCompleted);
  CHECK(quest.HasCompleted());
  CHECK(quest.IsNewBest());
  float first = quest.GetRaceTime();
  CHECK_THAT(
      first,
      WithinAbs(
          81.0f,
          1.0f));  // 8 times 10 seconds, the second at the finish line before
  CHECK(quest.GetBestTime() == first);
  CHECK(quest.IsOffered());  // again

  SECTION("The beaver mentions the best time and offers to improve it") {
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.Talk();
    std::string first_page = quest.GetDialog().GetPage();
    CHECK(first_page.find(RaceQuest::FormatTime(first)) != std::string::npos);
    bool offers_to_improve = false;
    q.situation.dialog = Confirm();
    q.Update();
    q.situation.dialog = Confirm();
    q.Update();
    offers_to_improve = quest.GetDialog().GetPage().find("improve your time") !=
                        std::string::npos;
    CHECK(offers_to_improve);
  }
  SECTION("A faster lap is the new best time, a slower one is not") {
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.StartRace();
    q.RunLap(60.0f);
    CHECK(quest.IsNewBest());
    float second = quest.GetBestTime();
    CHECK(second < first);
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.StartRace();
    q.RunLap(120.0f);
    CHECK_FALSE(quest.IsNewBest());
    CHECK(quest.GetBestTime() == second);
    CHECK(quest.GetCompletions() == 3);
  }
  SECTION("Restarting the game keeps the best time") {
    quest.Abort();
    CHECK(quest.GetBestTime() == first);
    CHECK(quest.GetOutcome() == RaceQuest::Outcome::kNone);
  }
}

TEST_CASE(
    "The race fails in the water, off the track and after the time limit, then "
    "the beaver offers it again",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.StartRace();
  q.situation.bear =
      vec3(rules::kCheckpoints[0].x, 1.0f, rules::kCheckpoints[0].y);
  q.Update(5.0f);

  RaceQuest::Outcome expected = RaceQuest::Outcome::kNone;
  SECTION("Water") {
    q.situation.swimming = true;
    expected = RaceQuest::Outcome::kFellIntoWater;
  }
  SECTION("Off the track") {
    q.situation.on_track = false;
    expected = RaceQuest::Outcome::kLeftTrack;
  }
  SECTION("Too slow") {
    q.Update(RaceQuest::kTimeLimit - 5.5f);
    CHECK(quest.GetStage() == RaceQuest::Stage::kRacing);
    expected = RaceQuest::Outcome::kTooSlow;
    q.Update(1.0f);
  }
  q.Update();
  CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
  CHECK(quest.GetOutcome() == expected);
  CHECK(q.Count(RaceQuest::Event::kFailed) == 1);
  CHECK_FALSE(quest.HasCompleted());
  CHECK(quest.IsOffered());
  CHECK(quest.GetOutcomeAge() < 1.1f);  // the result is shown for a while

  // It can be started again at the beaver
  q.situation = Quest().situation;
  q.Talk();
  CHECK(quest.GetStage() == RaceQuest::Stage::kTalking);
  CHECK(quest.GetDialog().GetPageCount() == 2);  // a shorter conversation
  q.Answer(true);
  CHECK(quest.GetStage() == RaceQuest::Stage::kWalkingToStart);
}

TEST_CASE("Swimming before the race does not matter", "[quest][rules]") {
  Quest q;
  q.situation.swimming = true;
  q.situation.on_track = false;
  q.Update(10.0f);
  CHECK(q.quest.GetOutcome() == RaceQuest::Outcome::kNone);
  CHECK(q.Count(RaceQuest::Event::kFailed) == 0);
}

TEST_CASE("The race takes at most two minutes", "[quest][rules]") {
  CHECK(RaceQuest::kTimeLimit == 120.0f);
}

TEST_CASE(
    "While the bear is on another quest the beaver does not offer the race",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.situation.busy = true;
  q.Update();
  CHECK_FALSE(quest.IsOffered());
  CHECK(quest.CanTalk());  // it still answers
  q.Talk();
  CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
  CHECK(quest.GetDialog().IsPassing());  // only a line in passing
  q.Update(5.0f);
  CHECK_FALSE(quest.GetDialog().IsOpen());

  q.situation.busy = false;
  q.Update();
  CHECK(quest.IsOffered());
  q.Talk();
  CHECK(quest.GetStage() == RaceQuest::Stage::kTalking);
}

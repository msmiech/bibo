#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <vector>

#include "Scene/Dialog.hpp"
#include "Scene/RaceQuest.hpp"

using namespace bibo::scene;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace {
const float FRAME = 1.0f / 60.0f;

DialogInput confirm() {
  DialogInput input;
  input.confirm = true;
  return input;
}

DialogInput navigate(int direction) {
  DialogInput input;
  input.navigate = direction;
  return input;
}

/**
 * Updates until the page has appeared completely
 * @return The sounds the speaker made meanwhile
 */
int finishPage(Dialog& dialog) {
  int sounds = 0;
  for (int i = 0; i < 60 * 30 && dialog.isTyping(); ++i) {
    dialog.update(FRAME, DialogInput());
    sounds += dialog.takeSounds();
  }
  return sounds;
}

// Finish line across the x axis at the origin, the race goes along -z
rules::FinishLine finishLine() {
  rules::FinishLine line;
  line.center = vec3(0.0f);
  line.across = vec3(1.0f, 0.0f, 0.0f);
  line.halfWidth = 8.0f;
  return line;
}

/**
 * The quest with the bear next to the beaver at the start
 */
struct Quest {
  RaceQuest quest{finishLine(), vec3(0.0f, 0.0f, -1.0f)};
  RaceQuest::Situation situation;
  std::vector<RaceQuest::Event> events;

  Quest() {
    situation.bear = vec3(5.0f, 1.0f, 4.0f);
    situation.nearBeaver = true;
  }

  void update(float seconds = FRAME) {
    for (float time = 0.0f; time < seconds - 1e-4f; time += FRAME) {
      quest.update(FRAME, situation);
      situation.interact = false;
      situation.dialog = DialogInput();
      for (RaceQuest::Event event : quest.takeEvents()) {
        events.push_back(event);
      }
    }
  }

  int count(RaceQuest::Event event) const {
    int result = 0;
    for (RaceQuest::Event e : events) {
      result += e == event ? 1 : 0;
    }
    return result;
  }

  void talk() {
    situation.interact = true;
    update();
  }

  /**
   * Goes through the pages of the conversation and answers
   * @param interact The button of the answer is also the one that starts a
   * conversation (E)
   */
  void answer(bool yes, bool interact = false) {
    const Dialog& dialog = quest.getDialog();
    for (int page = 0; page < 10 && !dialog.isChoosing(); ++page) {
      situation.dialog = confirm();  // the whole page at once
      update();
      if (!dialog.isChoosing()) {
        situation.dialog = confirm();  // next page
        update();
      }
    }
    REQUIRE(dialog.isChoosing());
    if (!yes) {
      situation.dialog = navigate(1);
      update();
    }
    situation.dialog = confirm();
    situation.interact = interact;
    update();
  }

  /**
   * Accepts the race, walks to the start and waits for the countdown
   */
  void startRace() {
    talk();
    answer(true);
    REQUIRE(quest.getStage() == RaceQuest::Stage::WALKING_TO_START);
    situation.atStart = true;
    situation.bear = quest.getStartPosition();
    update();
    REQUIRE(quest.getStage() == RaceQuest::Stage::COUNTDOWN);
    situation.atStart = false;
    update(RaceQuest::COUNTDOWN_SECONDS + 0.05f);
    REQUIRE(quest.getStage() == RaceQuest::Stage::RACING);
  }

  /**
   * Runs through the checkpoints and over the finish line
   */
  void runLap(float seconds) {
    for (std::size_t i = 0; i < rules::CHECKPOINT_COUNT; ++i) {
      situation.bear =
          vec3(rules::CHECKPOINTS[i].x, 1.0f, rules::CHECKPOINTS[i].y);
      update(seconds / static_cast<float>(rules::CHECKPOINT_COUNT + 1));
    }
    situation.bear = vec3(0.0f, 1.0f, -0.5f);
    update();
  }
};
}  // namespace

TEST_CASE(
    "The text of a page appears letter by letter, the speaker makes sounds "
    "meanwhile",
    "[quest][dialog]") {
  Dialog dialog;
  dialog.open("Beaver", {"Hello there, bear! Nice day.", "Second page."});
  REQUIRE(dialog.isOpen());
  CHECK(dialog.getSpeaker() == "Beaver");
  CHECK(dialog.getText().empty());
  dialog.update(0.1f, DialogInput());
  CHECK(dialog.getText() == "Hell");  // 40 letters per second
  CHECK(dialog.takeSounds() == 1);    // right away
  CHECK(dialog.isTyping());
  CHECK_FALSE(dialog.canContinue());

  int sounds = finishPage(dialog);
  CHECK(dialog.getText() == "Hello there, bear! Nice day.");
  CHECK(sounds >= 3);
  CHECK(sounds <= 8);
  CHECK(dialog.canContinue());
  CHECK_FALSE(dialog.isChoosing());  // no answers

  // Silent while the page is complete
  dialog.update(1.0f, DialogInput());
  CHECK(dialog.takeSounds() == 0);

  dialog.update(FRAME, confirm());
  CHECK(dialog.getPageIndex() == 1);
  CHECK(dialog.getText().empty());

  SECTION(
      "Confirm shows the whole page at once, then closes the conversation "
      "after the last page") {
    dialog.update(FRAME, confirm());
    CHECK(dialog.getText() == "Second page.");
    dialog.update(FRAME, confirm());
    CHECK_FALSE(dialog.isOpen());
    int choice = 5;
    CHECK(dialog.takeEnded(choice));
    CHECK(choice == -1);
    CHECK_FALSE(dialog.takeEnded(choice));  // once
  }
}

TEST_CASE("The text pauses after the end of a sentence", "[quest][dialog]") {
  Dialog dialog;
  dialog.open("Beaver", {"Hi. Ho"});
  dialog.update(3.0f / Dialog::CHARACTERS_PER_SECOND + 1e-4f, DialogInput());
  CHECK(dialog.getText() == "Hi.");
  dialog.update(0.5f * Dialog::SENTENCE_PAUSE, DialogInput());
  CHECK(dialog.getText() == "Hi.");
  dialog.update(Dialog::SENTENCE_PAUSE, DialogInput());
  CHECK(dialog.getText().size() > 3);
}

TEST_CASE("After the last page the player chooses an answer",
          "[quest][dialog]") {
  Dialog dialog;
  dialog.open("Beaver", {"Would you like to?"}, {"Yes", "No"});
  CHECK_FALSE(dialog.isChoosing());  // not before the question appeared
  finishPage(dialog);
  REQUIRE(dialog.isChoosing());
  CHECK_FALSE(dialog.canContinue());
  CHECK(dialog.getSelectedChoice() == 0);

  int choice = -1;
  SECTION("Yes") {
    dialog.update(FRAME, confirm());
    REQUIRE(dialog.takeEnded(choice));
    CHECK(choice == 0);
  }
  SECTION("The selection goes round") {
    dialog.update(FRAME, navigate(1));
    CHECK(dialog.getSelectedChoice() == 1);
    dialog.update(FRAME, navigate(1));
    CHECK(dialog.getSelectedChoice() == 0);
    dialog.update(FRAME, navigate(-1));
    CHECK(dialog.getSelectedChoice() == 1);
    dialog.update(FRAME, confirm());
    REQUIRE(dialog.takeEnded(choice));
    CHECK(choice == 1);
  }
  SECTION("Cancel answers with the last choice (No)") {
    DialogInput cancel;
    cancel.cancel = true;
    dialog.update(FRAME, cancel);
    REQUIRE(dialog.takeEnded(choice));
    CHECK(choice == 1);
  }
  CHECK_FALSE(dialog.isOpen());
}

TEST_CASE("Cancel can answer with another choice than the last one",
          "[quest][dialog]") {
  Dialog dialog;
  dialog.open("Bear", {"Keep on fishing?"}, {"Keep fishing", "Stop fishing"},
              0);
  finishPage(dialog);
  REQUIRE(dialog.isChoosing());
  DialogInput cancel;
  cancel.cancel = true;
  dialog.update(FRAME, cancel);
  int choice = -1;
  REQUIRE(dialog.takeEnded(choice));
  CHECK(choice == 0);
}

TEST_CASE("A line said in passing goes away on its own", "[quest][dialog]") {
  Dialog dialog;
  dialog.say("Beaver", "Splendid!");
  CHECK(dialog.isPassing());
  dialog.update(FRAME, confirm());
  CHECK(dialog.isTyping());  // the player cannot skip it
  finishPage(dialog);
  CHECK_FALSE(dialog.canContinue());
  dialog.update(Dialog::LINE_HOLD_TIME + 0.1f, DialogInput());
  CHECK_FALSE(dialog.isOpen());
}

TEST_CASE("Times are shown as minutes, seconds and hundredths", "[quest]") {
  CHECK(RaceQuest::formatTime(0.0f) == "0:00.00");
  CHECK(RaceQuest::formatTime(83.456f) == "1:23.45");
  CHECK(RaceQuest::formatTime(300.0f) == "5:00.00");
  CHECK(RaceQuest::formatTime(-1.0f) == "0:00.00");
}

TEST_CASE(
    "The race is a quest of the beaver: talk, accept, walk to the start, "
    "countdown, race",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
  CHECK(quest.isOffered());
  CHECK(quest.isPlayerInControl());
  CHECK(quest.getCountdownNumber() == -1);  // nothing on the screen

  SECTION("Only near the beaver") {
    q.situation.nearBeaver = false;
    q.talk();
    CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
    CHECK_FALSE(quest.canTalk());
  }
  SECTION(
      "Yes: the bear walks up to the starting line, the countdown starts the "
      "timer") {
    q.update();
    CHECK(quest.canTalk());
    q.talk();
    CHECK(quest.getStage() == RaceQuest::Stage::TALKING);
    CHECK_FALSE(quest.isPlayerInControl());
    CHECK_FALSE(quest.isOffered());
    q.update(1.0f);
    // it chatters while its words appear
    CHECK(q.count(RaceQuest::Event::BEAVER_SOUND) > 0);

    q.answer(true);
    CHECK(q.count(RaceQuest::Event::ACCEPTED) == 1);
    CHECK(quest.getStage() == RaceQuest::Stage::WALKING_TO_START);
    CHECK_FALSE(quest.isPlayerInControl());
    CHECK(quest.getDialog().isPassing());  //"Off to the starting line!"
    // Behind the line, looking along the track
    CHECK_THAT(quest.getStartPosition().z, WithinAbs(1.0f, 1e-4f));
    CHECK(quest.getStartDirection() == vec3(0.0f, 0.0f, -1.0f));

    q.update(2.0f);
    CHECK(quest.getStage() == RaceQuest::Stage::WALKING_TO_START);
    q.situation.atStart = true;
    q.update();
    REQUIRE(quest.getStage() == RaceQuest::Stage::COUNTDOWN);
    CHECK(quest.getCountdownNumber() == 3);
    q.update(1.0f);
    CHECK(quest.getCountdownNumber() == 2);
    q.update(1.0f);
    CHECK(quest.getCountdownNumber() == 1);
    CHECK(quest.getStage() == RaceQuest::Stage::COUNTDOWN);
    q.update(1.0f);
    REQUIRE(quest.getStage() == RaceQuest::Stage::RACING);
    CHECK(q.count(RaceQuest::Event::COUNTDOWN_TICK) == 3);
    CHECK(q.count(RaceQuest::Event::START) == 1);
    CHECK(quest.getCountdownNumber() == 0);  // GO!
    CHECK(quest.isPlayerInControl());
    q.update(RaceQuest::GO_DURATION + 0.05f);
    CHECK(quest.getCountdownNumber() == -1);
    CHECK(quest.getRaceTime() > 0.9f);
  }
  SECTION("No ends the conversation") {
    q.talk();
    q.answer(false);
    CHECK(q.count(RaceQuest::Event::DECLINED) == 1);
    CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
    CHECK(quest.isOffered());
    CHECK(quest.isPlayerInControl());
    CHECK(quest.getDialog().isPassing());  //"Suit yourself."
    // The player can talk again
    q.update(0.5f);
    q.talk();
    CHECK(quest.getStage() == RaceQuest::Stage::TALKING);
  }
  SECTION("The button that answered does not start the conversation again") {
    q.talk();
    q.answer(false, true);  // E answers and talks
    CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
    q.update(0.5f);
    CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
  }
  SECTION(
      "The cutscene ends after a while even if the bear does not get to the "
      "start") {
    q.talk();
    q.answer(true);
    q.update(RaceQuest::WALK_TIMEOUT + 0.1f);
    CHECK(quest.getStage() == RaceQuest::Stage::COUNTDOWN);
  }
}

TEST_CASE(
    "The lap is complete at the finish line after all checkpoints, the beaver "
    "offers to improve the best time",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.startRace();
  CHECK(quest.getCheckpointsPassed() == 0);
  CHECK(quest.getCheckpointCount() == rules::CHECKPOINT_COUNT);

  // The finish line does not count before the checkpoints
  q.situation.bear = vec3(0.0f, 1.0f, -0.5f);
  q.update(1.0f);
  CHECK(quest.getStage() == RaceQuest::Stage::RACING);

  q.runLap(90.0f);
  CHECK(q.count(RaceQuest::Event::CHECKPOINT) ==
        static_cast<int>(rules::CHECKPOINT_COUNT));
  REQUIRE(q.count(RaceQuest::Event::COMPLETED) == 1);
  CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
  CHECK(quest.getOutcome() == RaceQuest::Outcome::COMPLETED);
  CHECK(quest.hasCompleted());
  CHECK(quest.isNewBest());
  float first = quest.getRaceTime();
  CHECK_THAT(
      first,
      WithinAbs(
          81.0f,
          1.0f));  // 8 times 10 seconds, the second at the finish line before
  CHECK(quest.getBestTime() == first);
  CHECK(quest.isOffered());  // again

  SECTION("The beaver mentions the best time and offers to improve it") {
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.talk();
    std::string firstPage = quest.getDialog().getPage();
    CHECK(firstPage.find(RaceQuest::formatTime(first)) != std::string::npos);
    bool offersToImprove = false;
    q.situation.dialog = confirm();
    q.update();
    q.situation.dialog = confirm();
    q.update();
    offersToImprove = quest.getDialog().getPage().find("improve your time") !=
                      std::string::npos;
    CHECK(offersToImprove);
  }
  SECTION("A faster lap is the new best time, a slower one is not") {
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.startRace();
    q.runLap(60.0f);
    CHECK(quest.isNewBest());
    float second = quest.getBestTime();
    CHECK(second < first);
    q.situation.bear = vec3(5.0f, 1.0f, 4.0f);
    q.startRace();
    q.runLap(120.0f);
    CHECK_FALSE(quest.isNewBest());
    CHECK(quest.getBestTime() == second);
    CHECK(quest.getCompletions() == 3);
  }
  SECTION("Restarting the game keeps the best time") {
    quest.abort();
    CHECK(quest.getBestTime() == first);
    CHECK(quest.getOutcome() == RaceQuest::Outcome::NONE);
  }
}

TEST_CASE(
    "The race fails in the water, off the track and after the time limit, then "
    "the beaver offers it again",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.startRace();
  q.situation.bear =
      vec3(rules::CHECKPOINTS[0].x, 1.0f, rules::CHECKPOINTS[0].y);
  q.update(5.0f);

  RaceQuest::Outcome expected = RaceQuest::Outcome::NONE;
  SECTION("Water") {
    q.situation.swimming = true;
    expected = RaceQuest::Outcome::FELL_INTO_WATER;
  }
  SECTION("Off the track") {
    q.situation.onTrack = false;
    expected = RaceQuest::Outcome::LEFT_TRACK;
  }
  SECTION("Too slow") {
    q.update(RaceQuest::TIME_LIMIT - 5.5f);
    CHECK(quest.getStage() == RaceQuest::Stage::RACING);
    expected = RaceQuest::Outcome::TOO_SLOW;
    q.update(1.0f);
  }
  q.update();
  CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
  CHECK(quest.getOutcome() == expected);
  CHECK(q.count(RaceQuest::Event::FAILED) == 1);
  CHECK_FALSE(quest.hasCompleted());
  CHECK(quest.isOffered());
  CHECK(quest.getOutcomeAge() < 1.1f);  // the result is shown for a while

  // It can be started again at the beaver
  q.situation = Quest().situation;
  q.talk();
  CHECK(quest.getStage() == RaceQuest::Stage::TALKING);
  CHECK(quest.getDialog().getPageCount() == 2);  // a shorter conversation
  q.answer(true);
  CHECK(quest.getStage() == RaceQuest::Stage::WALKING_TO_START);
}

TEST_CASE("Swimming before the race does not matter", "[quest][rules]") {
  Quest q;
  q.situation.swimming = true;
  q.situation.onTrack = false;
  q.update(10.0f);
  CHECK(q.quest.getOutcome() == RaceQuest::Outcome::NONE);
  CHECK(q.count(RaceQuest::Event::FAILED) == 0);
}

TEST_CASE("The race takes at most two minutes", "[quest][rules]") {
  CHECK(RaceQuest::TIME_LIMIT == 120.0f);
}

TEST_CASE(
    "While the bear is on another quest the beaver does not offer the race",
    "[quest][rules]") {
  Quest q;
  RaceQuest& quest = q.quest;
  q.situation.busy = true;
  q.update();
  CHECK_FALSE(quest.isOffered());
  CHECK(quest.canTalk());  // it still answers
  q.talk();
  CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
  CHECK(quest.getDialog().isPassing());  // only a line in passing
  q.update(5.0f);
  CHECK_FALSE(quest.getDialog().isOpen());

  q.situation.busy = false;
  q.update();
  CHECK(quest.isOffered());
  q.talk();
  CHECK(quest.getStage() == RaceQuest::Stage::TALKING);
}

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Scene/StonesQuest.hpp"

using namespace bibo::scene;

namespace {
const float FRAME = 1.0f / 60.0f;

DialogInput confirm() {
  DialogInput input;
  input.confirm = true;
  return input;
}

/**
 * The quest with the bear next to Chonk
 */
struct Quest {
  StonesQuest quest;
  StonesQuest::Situation situation;
  std::vector<StonesQuest::Event> events;

  Quest() { situation.nearChonk = true; }

  void update(float seconds = FRAME) {
    for (float time = 0.0f; time < seconds - 1e-4f; time += FRAME) {
      quest.update(FRAME, situation);
      situation.interact = false;
      situation.dialog = DialogInput();
      for (StonesQuest::Event event : quest.takeEvents()) {
        events.push_back(event);
      }
    }
  }

  int count(StonesQuest::Event event) const {
    int result = 0;
    for (StonesQuest::Event e : events) {
      result += e == event ? 1 : 0;
    }
    return result;
  }

  void talk() {
    situation.interact = true;
    update();
  }

  /**
   * Goes through the pages of the conversation and chooses an answer (if there
   * are any)
   */
  void answer(int choice) {
    const Dialog& dialog = quest.getDialog();
    for (int page = 0; page < 12 && dialog.isOpen() && !dialog.isPassing() &&
                       !dialog.isChoosing();
         ++page) {
      situation.dialog = confirm();  // the whole page at once
      update();
      if (dialog.isOpen() && !dialog.isChoosing()) {
        situation.dialog = confirm();  // next page (or closes the last one)
        update();
      }
    }
    if (dialog.isChoosing()) {
      for (int i = 0; i < choice; ++i) {
        situation.dialog.navigate = 1;
        update();
      }
      situation.dialog = confirm();
      update();
    }
  }
};
}  // namespace

TEST_CASE(
    "Chonk sends the bear to the standing stones and wants a report of what is "
    "happening there",
    "[quest][stones]") {
  Quest q;
  StonesQuest& quest = q.quest;
  q.update();
  CHECK(quest.isOffered());
  CHECK(quest.canTalk());
  CHECK(quest.getObjective().empty());

  q.talk();
  REQUIRE(quest.getStage() == StonesQuest::Stage::TALKING);
  CHECK(quest.getDialog().getSpeaker() == StonesQuest::SPEAKER);
  CHECK_FALSE(quest.isPlayerInControl());
  q.update(2.0f);  // the first words appear
  q.answer(0);
  REQUIRE(quest.getStage() == StonesQuest::Stage::EXPLORING);
  CHECK(quest.isActive());
  CHECK(quest.isPlayerInControl());
  CHECK(q.count(StonesQuest::Event::ACCEPTED) == 1);
  CHECK(q.count(StonesQuest::Event::CHONK_SOUND) >
        3);  // it chitters while its words appear
  CHECK_FALSE(quest.isOffered());
  CHECK(quest.getObjective() == "Explore the stone circle on the hill");

  // At the stones: it arrives, then inspects the flat stone in the middle
  q.situation.nearChonk = false;
  q.update(3.0f);  // the line Chonk says in passing goes away
  CHECK_FALSE(quest.canInspect());
  q.situation.atStones = true;
  q.update();
  CHECK(quest.hasArrived());
  CHECK(q.count(StonesQuest::Event::ARRIVED) == 1);
  CHECK(quest.getObjective() ==
        "Inspect the flat stone in the middle of the circle");
  q.situation.nearStone = true;
  q.update();
  REQUIRE(quest.canInspect());
  q.talk();
  REQUIRE(quest.isInspecting());
  CHECK(quest.getDialog().getSpeaker() == StonesQuest::STONES);
  CHECK_FALSE(quest.isPlayerInControl());
  q.answer(0);
  REQUIRE(quest.getStage() == StonesQuest::Stage::RETURNING);
  CHECK(q.count(StonesQuest::Event::INSPECTED) == 1);
  CHECK(quest.hasReport());
  CHECK(quest.getObjective() == "Report back to Chonk");
  CHECK_FALSE(quest.canInspect());  // only once

  SECTION(
      "The truth: Chonk finds its snack stash again, the quest is complete") {
    q.situation = StonesQuest::Situation();
    q.situation.nearChonk = true;
    q.update();
    q.talk();
    REQUIRE(quest.getStage() == StonesQuest::Stage::TALKING);
    q.answer(0);  // what the bear found
    CHECK(quest.getStage() == StonesQuest::Stage::TALKING);  // Chonk answers
    q.answer(0);
    CHECK(quest.getStage() == StonesQuest::Stage::IDLE);
    CHECK(quest.hasCompleted());
    CHECK(quest.getReport() == StonesQuest::Report::TRUTH);
    CHECK(quest.getOutcome() == StonesQuest::Outcome::COMPLETED);
    CHECK(q.count(StonesQuest::Event::COMPLETED) == 1);
    CHECK_FALSE(quest.isActive());
    CHECK_FALSE(quest.isOffered());  // no more quests
    CHECK(quest.getObjective().empty());

    // Chonk still talks (thanks again), without choices
    q.update(0.5f);
    q.talk();
    REQUIRE(quest.getStage() == StonesQuest::Stage::TALKING);
    q.answer(0);
    CHECK(quest.getStage() == StonesQuest::Stage::IDLE);
    CHECK(q.count(StonesQuest::Event::COMPLETED) == 1);
  }
  SECTION("A tall tale also completes it") {
    q.situation = StonesQuest::Situation();
    q.situation.nearChonk = true;
    q.update();
    q.talk();
    q.answer(1);
    q.answer(0);
    CHECK(quest.hasCompleted());
    CHECK(quest.getReport() == StonesQuest::Report::HAUNTED);
  }
}

TEST_CASE(
    "Talking to Chonk on the way tells where to go, the player can give up",
    "[quest][stones]") {
  Quest q;
  StonesQuest& quest = q.quest;
  q.update();
  q.talk();
  q.answer(0);
  REQUIRE(quest.getStage() == StonesQuest::Stage::EXPLORING);
  q.update(3.0f);

  SECTION("On my way") {
    q.talk();
    REQUIRE(quest.getStage() == StonesQuest::Stage::TALKING);
    q.answer(0);
    CHECK(quest.getStage() == StonesQuest::Stage::EXPLORING);
    CHECK(quest.isActive());
  }
  SECTION("Giving up") {
    q.talk();
    q.answer(1);
    CHECK(quest.getStage() == StonesQuest::Stage::IDLE);
    CHECK_FALSE(quest.isActive());
    CHECK(quest.getOutcome() == StonesQuest::Outcome::STOPPED);
    CHECK(q.count(StonesQuest::Event::STOPPED) == 1);
    CHECK(quest.isOffered());  // it can be tried again
  }
}

TEST_CASE(
    "Chonk does not offer its quest while the bear is on another one, "
    "restarting ends it",
    "[quest][stones]") {
  Quest q;
  StonesQuest& quest = q.quest;
  q.situation.busy = true;
  q.update();
  CHECK_FALSE(quest.isOffered());
  CHECK_FALSE(quest.canTalk());
  q.talk();
  CHECK(quest.getStage() == StonesQuest::Stage::IDLE);

  q.situation.busy = false;
  q.update();
  q.talk();
  q.answer(0);
  REQUIRE(quest.isActive());
  quest.abort();
  CHECK_FALSE(quest.isActive());
  CHECK(quest.getStage() == StonesQuest::Stage::IDLE);
  CHECK_FALSE(quest.getDialog().isOpen());
}

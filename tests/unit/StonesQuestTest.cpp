#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Scene/StonesQuest.hpp"

using bibo::scene::Dialog;
using bibo::scene::DialogInput;
using bibo::scene::StonesQuest;

namespace {
const float kFrame = 1.0f / 60.0f;

DialogInput Confirm() {
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

  Quest() { situation.near_chonk = true; }

  void Update(float seconds = kFrame) {
    for (float time = 0.0f; time < seconds - 1e-4f; time += kFrame) {
      quest.Update(kFrame, situation);
      situation.interact = false;
      situation.dialog = DialogInput();
      for (StonesQuest::Event event : quest.TakeEvents()) {
        events.push_back(event);
      }
    }
  }

  int Count(StonesQuest::Event event) const {
    int result = 0;
    for (StonesQuest::Event e : events) {
      result += e == event ? 1 : 0;
    }
    return result;
  }

  void Talk() {
    situation.interact = true;
    Update();
  }

  /**
   * Goes through the pages of the conversation and chooses an answer (if there
   * are any)
   */
  void Answer(int choice) {
    const Dialog& dialog = quest.GetDialog();
    for (int page = 0; page < 12 && dialog.IsOpen() && !dialog.IsPassing() &&
                       !dialog.IsChoosing();
         ++page) {
      situation.dialog = Confirm();  // the whole page at once
      Update();
      if (dialog.IsOpen() && !dialog.IsChoosing()) {
        situation.dialog = Confirm();  // next page (or closes the last one)
        Update();
      }
    }
    if (dialog.IsChoosing()) {
      for (int i = 0; i < choice; ++i) {
        situation.dialog.navigate = 1;
        Update();
      }
      situation.dialog = Confirm();
      Update();
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
  q.Update();
  CHECK(quest.IsOffered());
  CHECK(quest.CanTalk());
  CHECK(quest.GetObjective().empty());

  q.Talk();
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kTalking);
  CHECK(quest.GetDialog().GetSpeaker() == StonesQuest::kSpeaker);
  CHECK_FALSE(quest.IsPlayerInControl());
  q.Update(2.0f);  // the first words appear
  q.Answer(0);
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kExploring);
  CHECK(quest.IsActive());
  CHECK(quest.IsPlayerInControl());
  CHECK(q.Count(StonesQuest::Event::kAccepted) == 1);
  // it chitters while its words appear
  CHECK(q.Count(StonesQuest::Event::kChonkSound) > 3);
  CHECK_FALSE(quest.IsOffered());
  CHECK(quest.GetObjective() == "Explore the stone circle on the hill");

  // At the stones: it arrives, then inspects the flat stone in the middle
  q.situation.near_chonk = false;
  q.Update(3.0f);  // the line Chonk says in passing goes away
  CHECK_FALSE(quest.CanInspect());
  q.situation.at_stones = true;
  q.Update();
  CHECK(quest.HasArrived());
  CHECK(q.Count(StonesQuest::Event::kArrived) == 1);
  CHECK(quest.GetObjective() ==
        "Inspect the flat stone in the middle of the circle");
  q.situation.near_stone = true;
  q.Update();
  REQUIRE(quest.CanInspect());
  q.Talk();
  REQUIRE(quest.IsInspecting());
  CHECK(quest.GetDialog().GetSpeaker() == StonesQuest::kStones);
  CHECK_FALSE(quest.IsPlayerInControl());
  q.Answer(0);
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kReturning);
  CHECK(q.Count(StonesQuest::Event::kInspected) == 1);
  CHECK(quest.HasReport());
  CHECK(quest.GetObjective() == "Report back to Chonk");
  CHECK_FALSE(quest.CanInspect());  // only once

  SECTION(
      "The truth: Chonk finds its snack stash again, the quest is complete") {
    q.situation = StonesQuest::Situation();
    q.situation.near_chonk = true;
    q.Update();
    q.Talk();
    REQUIRE(quest.GetStage() == StonesQuest::Stage::kTalking);
    q.Answer(0);  // what the bear found
    CHECK(quest.GetStage() == StonesQuest::Stage::kTalking);  // Chonk answers
    q.Answer(0);
    CHECK(quest.GetStage() == StonesQuest::Stage::kIdle);
    CHECK(quest.HasCompleted());
    CHECK(quest.GetReport() == StonesQuest::Report::kTruth);
    CHECK(quest.GetOutcome() == StonesQuest::Outcome::kCompleted);
    CHECK(q.Count(StonesQuest::Event::kCompleted) == 1);
    CHECK_FALSE(quest.IsActive());
    CHECK_FALSE(quest.IsOffered());  // no more quests
    CHECK(quest.GetObjective().empty());

    // Chonk still talks (thanks again), without choices
    q.Update(0.5f);
    q.Talk();
    REQUIRE(quest.GetStage() == StonesQuest::Stage::kTalking);
    q.Answer(0);
    CHECK(quest.GetStage() == StonesQuest::Stage::kIdle);
    CHECK(q.Count(StonesQuest::Event::kCompleted) == 1);
  }
  SECTION("A tall tale also completes it") {
    q.situation = StonesQuest::Situation();
    q.situation.near_chonk = true;
    q.Update();
    q.Talk();
    q.Answer(1);
    q.Answer(0);
    CHECK(quest.HasCompleted());
    CHECK(quest.GetReport() == StonesQuest::Report::kHaunted);
  }
}

TEST_CASE(
    "Talking to Chonk on the way tells where to go, the player can give up",
    "[quest][stones]") {
  Quest q;
  StonesQuest& quest = q.quest;
  q.Update();
  q.Talk();
  q.Answer(0);
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kExploring);
  q.Update(3.0f);

  SECTION("On my way") {
    q.Talk();
    REQUIRE(quest.GetStage() == StonesQuest::Stage::kTalking);
    q.Answer(0);
    CHECK(quest.GetStage() == StonesQuest::Stage::kExploring);
    CHECK(quest.IsActive());
  }
  SECTION("Giving up") {
    q.Talk();
    q.Answer(1);
    CHECK(quest.GetStage() == StonesQuest::Stage::kIdle);
    CHECK_FALSE(quest.IsActive());
    CHECK(quest.GetOutcome() == StonesQuest::Outcome::kStopped);
    CHECK(q.Count(StonesQuest::Event::kStopped) == 1);
    CHECK(quest.IsOffered());  // it can be tried again
  }
}

TEST_CASE(
    "Chonk does not offer its quest while the bear is on another one, "
    "restarting ends it",
    "[quest][stones]") {
  Quest q;
  StonesQuest& quest = q.quest;
  q.situation.busy = true;
  q.Update();
  CHECK_FALSE(quest.IsOffered());
  CHECK_FALSE(quest.CanTalk());
  q.Talk();
  CHECK(quest.GetStage() == StonesQuest::Stage::kIdle);

  q.situation.busy = false;
  q.Update();
  q.Talk();
  q.Answer(0);
  REQUIRE(quest.IsActive());
  quest.Abort();
  CHECK_FALSE(quest.IsActive());
  CHECK(quest.GetStage() == StonesQuest::Stage::kIdle);
  CHECK_FALSE(quest.GetDialog().IsOpen());
}

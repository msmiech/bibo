#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

#include "Scene/SalmonQuest.hpp"

using bibo::scene::Dialog;
using bibo::scene::DialogInput;
using bibo::scene::SalmonQuest;

using Catch::Matchers::WithinAbs;

namespace {
const float kFrame = 1.0f / 60.0f;
const int kPools = 6;

DialogInput Confirm() {
  DialogInput input;
  input.confirm = true;
  return input;
}

/**
 * The quest with the bear next to Mishka
 */
struct Quest {
  SalmonQuest quest{kPools};
  SalmonQuest::Situation situation;
  std::vector<SalmonQuest::Event> events;

  Quest() { situation.near_mishka = true; }

  void Update(float seconds = kFrame) {
    for (float time = 0.0f; time < seconds - 1e-4f; time += kFrame) {
      quest.Update(kFrame, situation);
      situation.interact = false;
      situation.eaten_pool = -1;
      situation.dialog = DialogInput();
      for (SalmonQuest::Event event : quest.TakeEvents()) {
        events.push_back(event);
      }
    }
  }

  int Count(SalmonQuest::Event event) const {
    int result = 0;
    for (SalmonQuest::Event e : events) {
      result += e == event ? 1 : 0;
    }
    return result;
  }

  void Talk() {
    situation.interact = true;
    Update();
  }

  /**
   * Goes through the pages of the conversation and chooses an answer
   */
  void Answer(int choice) {
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
    for (int i = 0; i < choice; ++i) {
      situation.dialog.navigate = 1;
      Update();
    }
    situation.dialog = Confirm();
    Update();
  }

  void Eat(int pool) {
    situation.eaten_pool = pool;
    Update();
  }

  void Accept() {
    Talk();
    REQUIRE(quest.GetStage() == SalmonQuest::Stage::kTalking);
    Answer(0);
    REQUIRE(quest.GetStage() == SalmonQuest::Stage::kFishing);
  }
};
}  // namespace

TEST_CASE(
    "Fishing is a quest of Mishka: talk, accept, eat a salmon from every pool",
    "[quest][salmon]") {
  Quest q;
  SalmonQuest& quest = q.quest;
  CHECK(quest.GetStage() == SalmonQuest::Stage::kIdle);
  CHECK(quest.IsOffered());
  CHECK_FALSE(quest.IsActive());
  CHECK(quest.GetPoolCount() == kPools);

  SECTION("Only near Mishka") {
    q.situation.near_mishka = false;
    q.Talk();
    CHECK(quest.GetStage() == SalmonQuest::Stage::kIdle);
    CHECK_FALSE(quest.CanTalk());
  }
  SECTION("Salmon eaten outside of the quest give no points") {
    q.Eat(0);
    q.Eat(1);
    CHECK(quest.GetEatenCount() == 0);
    CHECK(quest.GetScore() == 0);
    CHECK(q.Count(SalmonQuest::Event::kEaten) == 0);
  }
  SECTION("No: nothing happens") {
    q.Talk();
    CHECK_FALSE(quest.IsPlayerInControl());
    q.Answer(1);
    CHECK(quest.GetStage() == SalmonQuest::Stage::kIdle);
    CHECK(q.Count(SalmonQuest::Event::kDeclined) == 1);
    CHECK_FALSE(quest.IsActive());
    CHECK(quest.GetDialog().IsPassing());  // Mishka says something in passing
    int before = q.Count(SalmonQuest::Event::kBearSound);
    q.Update(0.5f);
    // and grumbles meanwhile
    CHECK(q.Count(SalmonQuest::Event::kBearSound) > before);
  }
  SECTION("Yes: every salmon of a new pool counts, until all pools are done") {
    q.Accept();
    CHECK(q.Count(SalmonQuest::Event::kAccepted) == 1);
    CHECK(quest.IsActive());
    CHECK(quest.IsPlayerInControl());
    CHECK_FALSE(quest.IsOffered());

    q.Eat(2);
    CHECK(quest.GetEatenCount() == 1);
    CHECK(quest.GetScore() == SalmonQuest::kSalmonPoints);
    CHECK(quest.GetDonePools()[2]);
    CHECK(q.Count(SalmonQuest::Event::kEaten) == 1);
    q.Eat(2);  // the same pool again does not count (no more salmon jump there)
    CHECK(quest.GetEatenCount() == 1);
    q.Eat(kPools);  // no such pool
    CHECK(quest.GetEatenCount() == 1);

    q.Update(10.0f);
    CHECK_THAT(quest.GetTime(), WithinAbs(10.0f + 3 * kFrame, 0.05f));
    for (int pool : {0, 1, 3, 4}) {
      q.Eat(pool);
    }
    CHECK(quest.IsActive());
    CHECK(q.Count(SalmonQuest::Event::kCompleted) == 0);
    q.Eat(5);
    CHECK(q.Count(SalmonQuest::Event::kCompleted) == 1);
    CHECK_FALSE(quest.IsActive());
    CHECK(quest.GetStage() == SalmonQuest::Stage::kIdle);
    CHECK(quest.GetOutcome() == SalmonQuest::Outcome::kCompleted);
    CHECK(quest.GetScore() == kPools * SalmonQuest::kSalmonPoints);
    CHECK(quest.HasCompleted());
    CHECK(quest.IsNewBest());
    float time = quest.GetTime();
    CHECK_THAT(quest.GetBestTime(), WithinAbs(time, 1e-4f));
    CHECK(quest.IsOffered());  // again

    // Again, slower: the best time stays
    q.Update(1.0f);
    q.Accept();
    CHECK(quest.GetEatenCount() == 0);  // all pools are open again
    CHECK(quest.GetScore() == 0);
    q.Update(time + 5.0f);
    for (int pool = 0; pool < kPools; ++pool) {
      q.Eat(pool);
    }
    CHECK(quest.GetCompletions() == 2);
    CHECK_FALSE(quest.IsNewBest());
    CHECK_THAT(quest.GetBestTime(), WithinAbs(time, 1e-4f));
  }
}

TEST_CASE(
    "During the quest Mishka tells how many salmon are left, the player can "
    "stop fishing",
    "[quest][salmon]") {
  Quest q;
  SalmonQuest& quest = q.quest;
  q.Accept();
  q.Eat(0);
  q.Update(2.0f);  // the line in passing goes away
  q.Talk();
  REQUIRE(quest.GetStage() == SalmonQuest::Stage::kTalking);
  CHECK(quest.IsActive());
  CHECK_FALSE(quest.IsPlayerInControl());
  float time = quest.GetTime();
  q.Update(3.0f);
  CHECK_THAT(quest.GetTime(),
             WithinAbs(time, 1e-4f));  // the time stands still while they talk

  SECTION("Keep fishing") {
    q.Answer(0);
    CHECK(quest.GetStage() == SalmonQuest::Stage::kFishing);
    CHECK(quest.IsActive());
    CHECK(quest.GetEatenCount() == 1);
  }
  SECTION("Cancel also keeps fishing") {
    q.situation.dialog.cancel = true;
    q.Update();
    CHECK(quest.GetStage() == SalmonQuest::Stage::kFishing);
    CHECK(quest.IsActive());
  }
  SECTION("Stop fishing") {
    q.Answer(1);
    CHECK(quest.GetStage() == SalmonQuest::Stage::kIdle);
    CHECK_FALSE(quest.IsActive());
    CHECK(quest.GetOutcome() == SalmonQuest::Outcome::kStopped);
    CHECK(q.Count(SalmonQuest::Event::kStopped) == 1);
    CHECK_FALSE(quest.HasCompleted());
    CHECK(quest.GetScore() == SalmonQuest::kSalmonPoints);  // what the bear got
    q.Eat(1);
    CHECK(quest.GetEatenCount() == 1);  // no more points
    CHECK(quest.IsOffered());
  }
}

TEST_CASE("Mishka does not offer fishing while the bear is on another quest",
          "[quest][salmon]") {
  Quest q;
  q.situation.busy = true;
  q.Update();
  CHECK_FALSE(q.quest.IsOffered());
  CHECK_FALSE(q.quest.CanTalk());
  q.Talk();
  CHECK(q.quest.GetStage() == SalmonQuest::Stage::kIdle);
  q.situation.busy = false;
  q.Update();
  CHECK(q.quest.IsOffered());
}

TEST_CASE("Restarting ends the quest, the best time stays", "[quest][salmon]") {
  Quest q;
  q.Accept();
  for (int pool = 0; pool < kPools; ++pool) {
    q.Eat(pool);
  }
  float best = q.quest.GetBestTime();
  q.Update(1.0f);
  q.Accept();
  q.Eat(3);
  q.quest.Abort();
  CHECK(q.quest.GetStage() == SalmonQuest::Stage::kIdle);
  CHECK_FALSE(q.quest.IsActive());
  CHECK(q.quest.GetEatenCount() == 0);
  CHECK(q.quest.GetOutcome() == SalmonQuest::Outcome::kNone);
  CHECK(q.quest.HasCompleted());
  CHECK_THAT(q.quest.GetBestTime(), WithinAbs(best, 1e-6f));
  CHECK_FALSE(q.quest.GetDialog().IsOpen());
}

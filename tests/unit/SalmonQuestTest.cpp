#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Scene/SalmonQuest.hpp"

using namespace bibo::scene;
using Catch::Matchers::WithinAbs;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	const int POOLS = 6;

	DialogInput confirm()
	{
		DialogInput input;
		input.confirm = true;
		return input;
	}

	/**
	* The quest with the bear next to Mishka
	*/
	struct Quest
	{
		SalmonQuest quest{ POOLS };
		SalmonQuest::Situation situation;
		std::vector<SalmonQuest::Event> events;

		Quest()
		{
			situation.nearMishka = true;
		}

		void update(float seconds = FRAME)
		{
			for (float time = 0.0f; time < seconds - 1e-4f; time += FRAME)
			{
				quest.update(FRAME, situation);
				situation.interact = false;
				situation.eatenPool = -1;
				situation.dialog = DialogInput();
				for (SalmonQuest::Event event : quest.takeEvents())
				{
					events.push_back(event);
				}
			}
		}

		int count(SalmonQuest::Event event) const
		{
			int result = 0;
			for (SalmonQuest::Event e : events)
			{
				result += e == event ? 1 : 0;
			}
			return result;
		}

		void talk()
		{
			situation.interact = true;
			update();
		}

		/**
		* Goes through the pages of the conversation and chooses an answer
		*/
		void answer(int choice)
		{
			const Dialog& dialog = quest.getDialog();
			for (int page = 0; page < 10 && !dialog.isChoosing(); ++page)
			{
				situation.dialog = confirm(); //the whole page at once
				update();
				if (!dialog.isChoosing())
				{
					situation.dialog = confirm(); //next page
					update();
				}
			}
			REQUIRE(dialog.isChoosing());
			for (int i = 0; i < choice; ++i)
			{
				situation.dialog.navigate = 1;
				update();
			}
			situation.dialog = confirm();
			update();
		}

		void eat(int pool)
		{
			situation.eatenPool = pool;
			update();
		}

		void accept()
		{
			talk();
			REQUIRE(quest.getStage() == SalmonQuest::Stage::TALKING);
			answer(0);
			REQUIRE(quest.getStage() == SalmonQuest::Stage::FISHING);
		}
	};
}

TEST_CASE("Fishing is a quest of Mishka: talk, accept, eat a salmon from every pool", "[quest][salmon]")
{
	Quest q;
	SalmonQuest& quest = q.quest;
	CHECK(quest.getStage() == SalmonQuest::Stage::IDLE);
	CHECK(quest.isOffered());
	CHECK_FALSE(quest.isActive());
	CHECK(quest.getPoolCount() == POOLS);

	SECTION("Only near Mishka")
	{
		q.situation.nearMishka = false;
		q.talk();
		CHECK(quest.getStage() == SalmonQuest::Stage::IDLE);
		CHECK_FALSE(quest.canTalk());
	}
	SECTION("Salmon eaten outside of the quest give no points")
	{
		q.eat(0);
		q.eat(1);
		CHECK(quest.getEatenCount() == 0);
		CHECK(quest.getScore() == 0);
		CHECK(q.count(SalmonQuest::Event::EATEN) == 0);
	}
	SECTION("No: nothing happens")
	{
		q.talk();
		CHECK_FALSE(quest.isPlayerInControl());
		q.answer(1);
		CHECK(quest.getStage() == SalmonQuest::Stage::IDLE);
		CHECK(q.count(SalmonQuest::Event::DECLINED) == 1);
		CHECK_FALSE(quest.isActive());
		CHECK(quest.getDialog().isPassing()); //Mishka says something in passing
		int before = q.count(SalmonQuest::Event::BEAR_SOUND);
		q.update(0.5f);
		CHECK(q.count(SalmonQuest::Event::BEAR_SOUND) > before); //and grumbles meanwhile
	}
	SECTION("Yes: every salmon of a new pool counts, until all pools are done")
	{
		q.accept();
		CHECK(q.count(SalmonQuest::Event::ACCEPTED) == 1);
		CHECK(quest.isActive());
		CHECK(quest.isPlayerInControl());
		CHECK_FALSE(quest.isOffered());

		q.eat(2);
		CHECK(quest.getEatenCount() == 1);
		CHECK(quest.getScore() == SalmonQuest::SALMON_POINTS);
		CHECK(quest.getDonePools()[2]);
		CHECK(q.count(SalmonQuest::Event::EATEN) == 1);
		q.eat(2); //the same pool again does not count (no more salmon jump there)
		CHECK(quest.getEatenCount() == 1);
		q.eat(POOLS); //no such pool
		CHECK(quest.getEatenCount() == 1);

		q.update(10.0f);
		CHECK_THAT(quest.getTime(), WithinAbs(10.0f + 3 * FRAME, 0.05f));
		for (int pool : { 0, 1, 3, 4 })
		{
			q.eat(pool);
		}
		CHECK(quest.isActive());
		CHECK(q.count(SalmonQuest::Event::COMPLETED) == 0);
		q.eat(5);
		CHECK(q.count(SalmonQuest::Event::COMPLETED) == 1);
		CHECK_FALSE(quest.isActive());
		CHECK(quest.getStage() == SalmonQuest::Stage::IDLE);
		CHECK(quest.getOutcome() == SalmonQuest::Outcome::COMPLETED);
		CHECK(quest.getScore() == POOLS * SalmonQuest::SALMON_POINTS);
		CHECK(quest.hasCompleted());
		CHECK(quest.isNewBest());
		float time = quest.getTime();
		CHECK_THAT(quest.getBestTime(), WithinAbs(time, 1e-4f));
		CHECK(quest.isOffered()); //again

		//Again, slower: the best time stays
		q.update(1.0f);
		q.accept();
		CHECK(quest.getEatenCount() == 0); //all pools are open again
		CHECK(quest.getScore() == 0);
		q.update(time + 5.0f);
		for (int pool = 0; pool < POOLS; ++pool)
		{
			q.eat(pool);
		}
		CHECK(quest.getCompletions() == 2);
		CHECK_FALSE(quest.isNewBest());
		CHECK_THAT(quest.getBestTime(), WithinAbs(time, 1e-4f));
	}
}

TEST_CASE("During the quest Mishka tells how many salmon are left, the player can stop fishing", "[quest][salmon]")
{
	Quest q;
	SalmonQuest& quest = q.quest;
	q.accept();
	q.eat(0);
	q.update(2.0f); //the line in passing goes away
	q.talk();
	REQUIRE(quest.getStage() == SalmonQuest::Stage::TALKING);
	CHECK(quest.isActive());
	CHECK_FALSE(quest.isPlayerInControl());
	float time = quest.getTime();
	q.update(3.0f);
	CHECK_THAT(quest.getTime(), WithinAbs(time, 1e-4f)); //the time stands still while they talk

	SECTION("Keep fishing")
	{
		q.answer(0);
		CHECK(quest.getStage() == SalmonQuest::Stage::FISHING);
		CHECK(quest.isActive());
		CHECK(quest.getEatenCount() == 1);
	}
	SECTION("Cancel also keeps fishing")
	{
		q.situation.dialog.cancel = true;
		q.update();
		CHECK(quest.getStage() == SalmonQuest::Stage::FISHING);
		CHECK(quest.isActive());
	}
	SECTION("Stop fishing")
	{
		q.answer(1);
		CHECK(quest.getStage() == SalmonQuest::Stage::IDLE);
		CHECK_FALSE(quest.isActive());
		CHECK(quest.getOutcome() == SalmonQuest::Outcome::STOPPED);
		CHECK(q.count(SalmonQuest::Event::STOPPED) == 1);
		CHECK_FALSE(quest.hasCompleted());
		CHECK(quest.getScore() == SalmonQuest::SALMON_POINTS); //what the bear got
		q.eat(1);
		CHECK(quest.getEatenCount() == 1); //no more points
		CHECK(quest.isOffered());
	}
}

TEST_CASE("Mishka does not offer fishing while the bear is on another quest", "[quest][salmon]")
{
	Quest q;
	q.situation.busy = true;
	q.update();
	CHECK_FALSE(q.quest.isOffered());
	CHECK_FALSE(q.quest.canTalk());
	q.talk();
	CHECK(q.quest.getStage() == SalmonQuest::Stage::IDLE);
	q.situation.busy = false;
	q.update();
	CHECK(q.quest.isOffered());
}

TEST_CASE("Restarting ends the quest, the best time stays", "[quest][salmon]")
{
	Quest q;
	q.accept();
	for (int pool = 0; pool < POOLS; ++pool)
	{
		q.eat(pool);
	}
	float best = q.quest.getBestTime();
	q.update(1.0f);
	q.accept();
	q.eat(3);
	q.quest.abort();
	CHECK(q.quest.getStage() == SalmonQuest::Stage::IDLE);
	CHECK_FALSE(q.quest.isActive());
	CHECK(q.quest.getEatenCount() == 0);
	CHECK(q.quest.getOutcome() == SalmonQuest::Outcome::NONE);
	CHECK(q.quest.hasCompleted());
	CHECK_THAT(q.quest.getBestTime(), WithinAbs(best, 1e-6f));
	CHECK_FALSE(q.quest.getDialog().isOpen());
}

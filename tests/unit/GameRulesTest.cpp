#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"

using namespace bibo::scene;
using glm::vec3;

TEST_CASE("The bear has fallen off the track below the fall-off height", "[rules]")
{
	CHECK(rules::hasFallenOff(vec3(0.0f, -1.2f, 0.0f)));
	CHECK(rules::hasFallenOff(vec3(50.0f, -5.0f, -20.0f)));

	CHECK_FALSE(rules::hasFallenOff(vec3(0.0f, rules::FALL_OFF_HEIGHT, 0.0f)));
	CHECK_FALSE(rules::hasFallenOff(vec3(0.0f, -1.0f, 0.0f)));
	CHECK_FALSE(rules::hasFallenOff(vec3(88.0f, 5.5f, -7.8f)));
}

TEST_CASE("The goal is reached inside the finish area around the goal", "[rules]")
{
	const vec3 goal(10.0f, 2.0f, -30.0f);

	SECTION("At the goal and anywhere inside the area")
	{
		CHECK(rules::hasReachedGoal(goal, goal));
		CHECK(rules::hasReachedGoal(goal + vec3(0.49f, -0.49f, 0.49f), goal));
		CHECK(rules::hasReachedGoal(goal + vec3(-0.49f, 3.49f, -0.49f), goal));
	}

	SECTION("The area reaches higher above the goal than below it (the bear stands on it)")
	{
		CHECK(rules::hasReachedGoal(goal + vec3(0.0f, 3.0f, 0.0f), goal));
		CHECK_FALSE(rules::hasReachedGoal(goal + vec3(0.0f, -3.0f, 0.0f), goal));
	}

	SECTION("Outside of the area in any direction")
	{
		const vec3 offset = GENERATE(
			vec3(0.5f, 0.0f, 0.0f), vec3(-0.5f, 0.0f, 0.0f),
			vec3(0.0f, 3.5f, 0.0f), vec3(0.0f, -0.5f, 0.0f),
			vec3(0.0f, 0.0f, 0.5f), vec3(0.0f, 0.0f, -0.5f),
			vec3(5.0f, 0.0f, 5.0f));
		CAPTURE(offset);
		CHECK_FALSE(rules::hasReachedGoal(goal + offset, goal));
	}
}

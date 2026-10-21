#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "Scene/Hedgehog.hpp"
#include "Scene/RaceQuest.hpp"
#include "Scene/Salmon.hpp"
#include "World/TrackTerrain.hpp"
#include "World/WorldGenerator.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
using bibo::scene::Hedgehog;
using bibo::scene::WaterPool;
using glm::vec2;
using glm::vec3;

TEST_CASE("There are pools of water on the track that are deep enough for salmon", "[assets][world][animals]")
{
	const bibo::world::WorldGenerator& generator = worldGenerator();
	const float sea = generator.getSeaLevel();
	std::vector<WaterPool> pools = bibo::scene::findWaterPools(generator);
	REQUIRE(pools.size() >= 4);
	float lastDistance = -1.0f;
	for (const WaterPool& pool : pools)
	{
		CAPTURE(pool.center, pool.trackDistance);
		CHECK(pool.center.y == sea);
		CHECK(pool.trackDistance > lastDistance); //in driving direction
		lastDistance = pool.trackDistance;
		CHECK(generator.getPath().nearest(vec2(pool.center.x, pool.center.z)).found);
		REQUIRE_FALSE(pool.spots.empty());
		for (vec2 spot : pool.spots)
		{
			CHECK(generator.height(spot) < sea - 0.6f);
			CHECK(glm::distance(spot, vec2(pool.center.x, pool.center.z)) <= pool.radius);
		}
	}
	//Apart from each other
	for (std::size_t i = 1; i < pools.size(); ++i)
	{
		CHECK(glm::distance(pools[i].center, pools[i - 1].center) > pools[i].radius + pools[i - 1].radius);
	}
}

TEST_CASE("The hedgehogs live where the rocks of the track were and stay on the floor of the track", "[assets][world][animals]")
{
	const int index = GENERATE(range(0, 12));
	vec3 rock;
	float radius;
	REQUIRE(bibo::world::TrackTerrain::nodeSphere(trackScene(), ("Rock" + std::to_string(index + 1)).c_str(), rock, radius));
	const vec2 home(rock.x, rock.z);
	Hedgehog::Ground ground = Hedgehog::trackFloor(worldGenerator());
	float height;
	CAPTURE(index, home);
	REQUIRE(ground(home, height));

	Hedgehog hedgehog(home, Hedgehog::startDirection(index), Hedgehog::seedOf(index), ground);
	CHECK(std::abs(hedgehog.getPosition().y - height) < 1e-4f); //on the ground, not floating like the rocks
	for (int i = 0; i < 60 * 60; ++i)
	{
		hedgehog.update(1.0f / 60.0f);
		vec3 position = hedgehog.getPosition();
		REQUIRE(ground(vec2(position.x, position.z), height));
		REQUIRE(std::abs(position.y - height) < 0.3f);
		REQUIRE(glm::distance(vec2(position.x, position.z), home) <= Hedgehog::HOME_RADIUS + 0.5f);
	}
}

TEST_CASE("The race track counts as the track for the race quest, the forest and the sea do not", "[assets][world][quest]")
{
	const bibo::world::WorldGenerator& generator = worldGenerator();
	const bibo::world::TrackPath& path = generator.getPath();
	int checked = 0;
	for (std::size_t i = 0; i < path.getSamples().size(); i += 3)
	{
		const bibo::world::TrackPath::Sample& sample = path.getSamples()[i];
		vec2 right(-sample.tangent.y, sample.tangent.x);
		vec2 center(sample.position.x, sample.position.z);
		CAPTURE(sample.distance);
		//The whole floor, also a bit up the walls
		for (float lateral : { -sample.floorLeft - 1.0f, 0.0f, sample.floorRight + 1.0f })
		{
			vec2 point = center + right * lateral;
			REQUIRE(bibo::scene::RaceQuest::isOnTrack(path, vec3(point.x, sample.position.y, point.y)));
		}
		++checked;
	}
	CHECK(checked > 100);
	//The start, the forest next to it and the sea
	const vec3 start(nodeModelMatrix(trackNode("BearBody"))[3]);
	CHECK(bibo::scene::RaceQuest::isOnTrack(path, start));
	CHECK_FALSE(bibo::scene::RaceQuest::isOnTrack(path, start + vec3(40.0f, 0.0f, 0.0f)));
	CHECK_FALSE(bibo::scene::RaceQuest::isOnTrack(path, vec3(600.0f, 0.0f, 600.0f)));
}

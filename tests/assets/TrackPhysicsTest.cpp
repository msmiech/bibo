#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <assimp/scene.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "Physics/Physics.hpp"
#include "Scene/Bear.hpp"
#include "Scene/GameRules.hpp"
#include "Scene/Hedgehog.hpp"
#include "World/WorldGenerator.hpp"
#include "World/WorldStreamer.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using namespace bibo::test;
using bibo::scene::Bear;
using bibo::scene::Hedgehog;
using bibo::world::TrackPath;
using bibo::world::WorldStreamer;
namespace rules = bibo::scene::rules;
using glm::vec3;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	const vec3 DOWN(0.0f, -5.0f, 0.0f);
	const float BACKWARD_SPEED = 7.5f; //Bear.cpp

	vec3 startPosition(const std::string& nodeName)
	{
		return vec3(nodeModelMatrix(trackNode(nodeName))[3]);
	}

	/**
	* The bodies of the track as the game creates them: the ground under the sea (Scene::initPhysicsBodies), the hedgehogs
	* at their homes where the rocks of the track are (Scene::initAnimals), the terrain and the trees around the bear streamed
	* by the world (WorldStreamer)
	*/
	struct TrackPhysics
	{
		PhysicsWorld world;
		std::vector<BodyHandle> hedgehogs;
		const vec3 bearStart = startPosition("BearBody");
		std::unique_ptr<WorldStreamer> streamer;

		explicit TrackPhysics(bool withHedgehogs = true)
		{
			world.createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f)); //ground
			for (int i = 0; withHedgehogs && i < 12; ++i)
			{
				vec3 home;
				float radius;
				REQUIRE(bibo::world::TrackTerrain::nodeSphere(trackScene(), ("Rock" + std::to_string(i + 1)).c_str(), home, radius));
				Hedgehog hedgehog(glm::vec2(home.x, home.z), Hedgehog::startDirection(i), Hedgehog::seedOf(i), Hedgehog::trackFloor(worldGenerator()));
				hedgehogs.push_back(world.createKinematicSphere(hedgehog.getColliderCenter(), hedgehog.getColliderRadius()));
			}
			streamer = std::make_unique<WorldStreamer>(worldGenerator(), &world, WorldStreamer::Settings(), 2);
			streamer->ensureCollision(bearStart, 12.0f);
		}

		~TrackPhysics()
		{
			streamer.reset(); //removes its bodies from the world
		}

		struct Run
		{
			std::vector<vec3> path; //position after every frame
			std::vector<BodyHandle> touchedBodies;
			bool supported = false;

			vec3 end() const { return path.back(); }
			bool touched(BodyHandle body) const { return std::find(touchedBodies.begin(), touchedBodies.end(), body) != touchedBodies.end(); }
		};

		/**
		* Places the bear and moves it every frame (like Bear::update, DOWN is the gravity), the world is streamed around it
		* @param velocity Velocity in a frame, from the position of the bear
		*/
		template <typename Velocity>
		Run moveBear(vec3 start, float seconds, Velocity velocity)
		{
			Run run;
			CharacterController bear(world, Bear::characterControllerDesc(start), [&](const CharacterController::Hit& hit) { run.touchedBodies.push_back(hit.body); });
			for (int i = 0; i < static_cast<int>(seconds / FRAME); ++i)
			{
				streamer->ensureCollision(bear.getPosition(), 8.0f);
				world.update(FRAME);
				run.supported = bear.move(velocity(bear.getPosition()) * FRAME, FRAME);
				run.path.push_back(bear.getPosition());
			}
			return run;
		}

		Run moveBear(vec3 start, float seconds, vec3 velocity)
		{
			return moveBear(start, seconds, [velocity](vec3) { return velocity; });
		}
	};
}

TEST_CASE("The bear starts standing on the terrain without touching a hedgehog", "[assets][physics][bear][world]")
{
	TrackPhysics track;

	TrackPhysics::Run run = track.moveBear(track.bearStart, 3.0f, DOWN);

	vec3 position = run.end();
	CAPTURE(track.bearStart, position);
	CHECK(run.supported);
	CHECK_FALSE(rules::hasFallenOff(position)); //did not fall through the terrain onto the ground box
	CHECK(std::abs(position.y - track.bearStart.y) < 2.0f); //starts close to the surface
	CHECK(glm::distance(vec3(position.x, 0.0f, position.z), vec3(track.bearStart.x, 0.0f, track.bearStart.z)) < 0.5f); //does not slide away
	for (BodyHandle hedgehog : track.hedgehogs)
	{
		CHECK_FALSE(run.touched(hedgehog)); //would knock it over
	}
}

TEST_CASE("Going backwards from the start neither passes a checkpoint nor completes the lap", "[assets][physics][bear][rules][world]")
{
	//There is no wall behind the start: the finish line is a trigger and the checkpoints have to be passed in their order
	TrackPhysics track;
	const rules::FinishLine finishLine = trackFinishLine();
	const vec3 backwards(0.0f, 0.0f, 1.0f); //the bear starts looking along -z (Bear::Bear)

	TrackPhysics::Run run = track.moveBear(track.bearStart, 1.5f, BACKWARD_SPEED * backwards + DOWN);

	CAPTURE(track.bearStart, run.end());
	CHECK(run.end().z > track.bearStart.z + 5.0f); //walks past where the wall was (3 units behind the start)
	CHECK_FALSE(rules::isOnFinishLine(run.end(), finishLine));
	std::size_t nextCheckpoint = 0;
	for (const vec3& position : run.path)
	{
		nextCheckpoint = rules::passCheckpoint(position, nextCheckpoint);
		REQUIRE_FALSE(rules::hasCompletedLap(nextCheckpoint, position, finishLine));
	}
	CHECK(nextCheckpoint == 0);
}

TEST_CASE("In the sea next to the island the bear falls onto the ground box and the game is lost", "[assets][physics][bear][rules][world]")
{
	TrackPhysics track;
	const vec3 sea(600.0f, 10.0f, 600.0f);
	REQUIRE(worldGenerator().height(glm::vec2(sea.x, sea.z)) < worldGenerator().getSeaLevel() - 5.0f);

	TrackPhysics::Run run = track.moveBear(sea, 5.0f, DOWN);

	CAPTURE(run.end());
	CHECK(run.supported);
	CHECK(rules::hasFallenOff(run.end()));
}

TEST_CASE("The ground of the physics world is the terrain of the track", "[assets][physics][world]")
{
	//The collision meshes of the chunks are their surfaces, which are the track terrain on the floor of the track
	TrackPhysics track(false);
	const TrackPath& path = worldGenerator().getPath();
	const bibo::world::TrackTerrain& terrain = worldGenerator().getTrack();
	float largestDifference = 0.0f;
	glm::vec3 worst(0.0f);
	for (std::size_t i = 0; i < path.getSamples().size(); i += 12)
	{
		const TrackPath::Sample& sample = path.getSamples()[i];
		glm::vec2 right(-sample.tangent.y, sample.tangent.x);
		for (float lateral : { -0.8f, 0.0f, 0.8f })
		{
			glm::vec2 point = glm::vec2(sample.position.x, sample.position.z) + right * (lateral * std::min(sample.floorLeft, sample.floorRight));
			if (terrain.height(point) > sample.position.y + 0.5f)
			{
				continue; //the floor is narrower here (on the walls the voxels round the steep surface a bit)
			}
			vec3 above(point.x, sample.position.y + 4.0f, point.y);
			track.streamer->ensureCollision(above, 6.0f);
			RayHit hit;
			CAPTURE(i, point, above, worldGenerator().isInCave(above));
			REQUIRE(track.world.castRay(above, vec3(0.0f, -10.0f, 0.0f), hit, true));
			float difference = std::abs(hit.position.y - terrain.height(point));
			if (difference > largestDifference)
			{
				largestDifference = difference;
				worst = hit.position;
			}
		}
	}
	CAPTURE(worst, terrain.height(glm::vec2(worst.x, worst.z)));
	CHECK(largestDifference < 0.25f); //a voxel is 1 unit, steep edges (of the puddles) are a bit rounded
}

TEST_CASE("The caves leave room for the bear to run and jump", "[assets][physics][bear][world]")
{
	//The roof of a cave is not part of the track terrain: along the trail through the cave nothing is in the way of the
	//bear, also not when it jumps (over the water on the track)
	TrackPhysics track(false);
	const TrackPath& path = worldGenerator().getPath();
	const std::vector<bibo::world::Cave>& caves = worldGenerator().getCaves();
	REQUIRE(caves.size() >= 2);
	const std::size_t index = GENERATE_COPY(range(std::size_t(0), caves.size()));
	const bibo::world::Cave& cave = caves[index];
	CAPTURE(index, cave.start, cave.end);
	const float RADIUS = 0.5f; //of the capsule of the bear
	const float JUMP = 2.5f; //above the floor, with the capsule

	for (float distance = cave.start - 10.0f; distance < cave.end + 10.0f; distance += 0.5f)
	{
		TrackPath::Sample sample = path.at(distance);
		TrackPath::Sample next = path.at(distance + 0.5f);
		//Over the water the bear jumps from the floor before it
		float floor = std::max({ sample.position.y, next.position.y, worldGenerator().getSeaLevel() });
		vec3 from(sample.position.x, floor + RADIUS + 0.4f, sample.position.z);
		vec3 to(next.position.x, std::max(next.position.y, worldGenerator().getSeaLevel()) + RADIUS + 0.4f, next.position.z);
		track.streamer->ensureCollision(from, 4.0f);
		RayHit hit;
		CAPTURE(distance, from);
		//Forwards (above the steps of the floor, the bear climbs or jumps them)
		CHECK_FALSE(track.world.castSphere(from + vec3(0.0f, 1.2f, 0.0f), to - from, RADIUS, hit, true));
		//Up (a jump)
		CHECK_FALSE(track.world.castSphere(from, vec3(0.0f, JUMP, 0.0f), RADIUS, hit, true));
	}
}

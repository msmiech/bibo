#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Physics/Physics.hpp"
#include "Scene/Bear.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using bibo::scene::Bear;
using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	const float GROUND_TOP = -5.0f;
	//Pushes the character onto the ground every frame (like the gravity in Bear::update)
	const vec3 DOWN(0.0f, -5.0f, 0.0f);
	//Speeds of the bear (Bear.cpp)
	const float FORWARD_SPEED = 10.0f;
	const float BACKWARD_SPEED = 7.5f;

	bool contains(const std::vector<BodyHandle>& bodies, BodyHandle body)
	{
		return std::find(bodies.begin(), bodies.end(), body) != bodies.end();
	}

	/**
	* Physics world with the ground of the game and the bear standing on it at the origin
	*/
	struct BearOnGround
	{
		PhysicsWorld world;
		BodyHandle ground;
		CharacterControllerDesc desc = Bear::characterControllerDesc(vec3(0.0f, GROUND_TOP + 3.0f, 0.0f));
		std::vector<BodyHandle> touchedBodies;
		CharacterController bear{ world, desc, [this](BodyHandle body) { touchedBodies.push_back(body); } };
		//Height of the center of the capsule when the bear stands on the ground
		const float standingHeight = GROUND_TOP + 0.5f * desc.height + desc.radius;

		BearOnGround() : ground(world.createStaticBox(vec3(0.0f, GROUND_TOP - 1.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f)))
		{
		}

		/**
		* Moves the bear every frame with the given velocity, like the game loop (Scene::update, Bear::update)
		* @return true if the bear stands on the ground afterwards
		*/
		bool moveFor(float seconds, vec3 velocity)
		{
			bool supported = false;
			int frames = static_cast<int>(std::lround(seconds / FRAME));
			for (int i = 0; i < frames; ++i)
			{
				world.update(FRAME);
				supported = bear.move(velocity * FRAME, FRAME);
			}
			return supported;
		}

		void land()
		{
			REQUIRE(moveFor(1.0f, DOWN));
		}
	};
}

TEST_CASE_METHOD(BearOnGround, "The bear lands on the ground", "[physics][bear]")
{
	CHECK(moveFor(2.0f, DOWN));
	CHECK_THAT(bear.getPosition(), IsNear(vec3(0.0f, standingHeight, 0.0f), desc.contactOffset + 0.01f));

	SECTION("The ground is reported as touched body (Scene::onBearHit only reacts to rocks)")
	{
		CHECK(contains(touchedBodies, ground));
	}

	SECTION("Pushing down on flat ground neither moves the bear nor lets it sink in")
	{
		vec3 standing = bear.getPosition();
		CHECK(moveFor(2.0f, DOWN));
		CHECK_THAT(bear.getPosition(), IsNear(standing, 1e-3f));
	}
}

TEST_CASE_METHOD(BearOnGround, "The bear walks with its speed on flat ground", "[physics][bear]")
{
	land();
	vec3 start = bear.getPosition();

	CHECK(moveFor(1.0f, vec3(0.0f, 0.0f, -FORWARD_SPEED) + DOWN));

	vec3 walked = bear.getPosition() - start;
	CHECK_THAT(walked.z, WithinAbs(-FORWARD_SPEED, 0.3f));
	CHECK_THAT(walked.x, WithinAbs(0.0f, 1e-3f));
	CHECK_THAT(walked.y, WithinAbs(0.0f, 0.01f));
}

TEST_CASE_METHOD(BearOnGround, "A frame without duration does not move the bear", "[physics][bear]")
{
	land();
	vec3 start = bear.getPosition();

	CHECK(bear.move(vec3(5.0f, 0.0f, 5.0f), 0.0f)); //still reports the ground
	CHECK(bear.getPosition() == start);
}

TEST_CASE_METHOD(BearOnGround, "The bear cannot walk through the thin wall behind it", "[physics][bear]")
{
	//Scene::initPhysicsBodies: 14 x 14 wall, 2 cm thick, 3 units behind the start of the bear
	const float wallZ = 3.0f;
	world.createStaticBox(vec3(0.0f, standingHeight, wallZ), vec3(7.0f, 7.0f, 0.01f));
	land();

	moveFor(2.0f, vec3(0.0f, 0.0f, BACKWARD_SPEED) + DOWN); //would walk 15 units without the wall

	float frontOfBear = bear.getPosition().z + desc.radius;
	CHECK(frontOfBear < wallZ);
	CHECK(frontOfBear > wallZ - 0.2f);
}

TEST_CASE_METHOD(BearOnGround, "Touching a rock is reported with the body of the rock", "[physics][bear]")
{
	//Scene::initPhysicsBodies: rocks are static spheres with a radius of 0.7
	const float rockRadius = 0.7f;

	SECTION("Rock in the way")
	{
		BodyHandle rock = world.createStaticSphere(vec3(0.0f, GROUND_TOP + rockRadius, -5.0f), rockRadius);
		land();
		REQUIRE_FALSE(contains(touchedBodies, rock));

		moveFor(1.0f, vec3(0.0f, 0.0f, -FORWARD_SPEED) + DOWN);

		CHECK(contains(touchedBodies, rock));
	}

	SECTION("Rock next to the way")
	{
		BodyHandle rock = world.createStaticSphere(vec3(3.0f, GROUND_TOP + rockRadius, -5.0f), rockRadius);
		land();

		moveFor(1.0f, vec3(0.0f, 0.0f, -FORWARD_SPEED) + DOWN);

		CHECK_FALSE(contains(touchedBodies, rock));
		CHECK(bear.getPosition().z < -9.0f);
	}
}

TEST_CASE("The bear can be created without a hit callback", "[physics][bear]")
{
	PhysicsWorld world;
	world.createStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(10.0f, 1.0f, 10.0f));
	CharacterController bear(world, Bear::characterControllerDesc(vec3(0.0f, 2.0f, 0.0f)), nullptr);

	bool supported = false;
	for (int i = 0; i < 60; ++i)
	{
		world.update(FRAME);
		supported = bear.move(DOWN * FRAME, FRAME);
	}
	CHECK(supported);
}

TEST_CASE_METHOD(BearOnGround, "The bear climbs obstacles up to its step offset", "[physics][bear]")
{
	land();

	SECTION("Step lower than the step offset")
	{
		const float stepHeight = 0.5f * desc.stepOffset;
		world.createStaticBox(vec3(0.0f, GROUND_TOP + 0.5f * stepHeight, -13.0f), vec3(5.0f, 0.5f * stepHeight, 10.0f));

		moveFor(1.5f, vec3(0.0f, 0.0f, -FORWARD_SPEED) + DOWN);

		CHECK(bear.getPosition().z < -5.0f);
		CHECK_THAT(bear.getPosition().y, WithinAbs(standingHeight + stepHeight, 0.1f));
	}

	SECTION("Obstacle higher than the step offset")
	{
		const float obstacleHeight = 1.5f * desc.stepOffset;
		const float obstacleFront = -3.0f;
		world.createStaticBox(vec3(0.0f, GROUND_TOP + 0.5f * obstacleHeight, obstacleFront - 1.0f), vec3(5.0f, 0.5f * obstacleHeight, 1.0f));

		moveFor(1.5f, vec3(0.0f, 0.0f, -FORWARD_SPEED) + DOWN);

		CHECK(bear.getPosition().z - desc.radius > obstacleFront - 0.2f);
		CHECK_THAT(bear.getPosition().y, WithinAbs(standingHeight, 0.1f));
	}
}

TEST_CASE("The bear stands on gentle slopes and slides down steep ones", "[physics][bear]")
{
	PhysicsWorld world;
	//Square of 40 x 40 with the front side facing up, tilted around the x axis
	const float vertices[] = { -20.0f, 0.0f, -20.0f, -20.0f, 0.0f, 20.0f, 20.0f, 0.0f, 20.0f, 20.0f, 0.0f, -20.0f };
	const unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };
	const CharacterControllerDesc desc = Bear::characterControllerDesc(vec3(0.0f, 3.0f, 0.0f));

	auto slideOnSlope = [&](float slopeDegrees) {
		world.createStaticMesh(vertices, 4, indices, 2, 1.0f, vec3(0.0f), glm::angleAxis(glm::radians(slopeDegrees), vec3(1.0f, 0.0f, 0.0f)));
		CharacterController bear(world, desc, nullptr);
		for (int i = 0; i < 60; ++i) //land
		{
			world.update(FRAME);
			bear.move(DOWN * FRAME, FRAME);
		}
		vec3 landed = bear.getPosition();
		for (int i = 0; i < 120; ++i)
		{
			world.update(FRAME);
			bear.move(DOWN * FRAME, FRAME);
		}
		return glm::distance(landed, bear.getPosition());
	};

	SECTION("Slope below the slope limit")
	{
		REQUIRE(glm::radians(30.0f) < desc.slopeLimit);
		CHECK(slideOnSlope(30.0f) < 0.05f);
	}

	SECTION("Slope above the slope limit")
	{
		REQUIRE(glm::radians(60.0f) > desc.slopeLimit);
		CHECK(slideOnSlope(60.0f) > 2.0f);
	}
}

TEST_CASE_METHOD(BearOnGround, "Jumping lifts the bear off the ground", "[physics][bear]")
{
	land();
	const float jumpSpeed = 10.0f, maxJumpTime = 0.5f; //Bear.cpp

	bool supported = moveFor(maxJumpTime, vec3(0.0f, jumpSpeed, 0.0f));

	CHECK_FALSE(supported);
	CHECK_THAT(bear.getPosition().y - standingHeight, WithinAbs(jumpSpeed * maxJumpTime, 0.3f));
}

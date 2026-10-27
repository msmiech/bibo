#include <cmath>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Physics/Physics.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using bibo::test::IsNear;
using Catch::Matchers::WithinAbs;
using glm::mat3;
using glm::mat4;
using glm::vec3;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	const float GRAVITY = 9.81f;
	//The ground of the game is a large box whose top face is at y = -5 (Scene::initPhysicsBodies)
	const float GROUND_TOP = -5.0f;

	void simulate(PhysicsWorld& world, float seconds, float timeDelta = FRAME)
	{
		int frames = static_cast<int>(std::lround(seconds / timeDelta));
		for (int i = 0; i < frames; ++i)
		{
			world.update(timeDelta);
		}
	}

	vec3 positionOf(const PhysicsWorld& world, BodyHandle body)
	{
		return vec3(world.getTransform(body)[3]);
	}

	BodyHandle createGround(PhysicsWorld& world)
	{
		return world.createStaticBox(vec3(0.0f, GROUND_TOP - 1.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f));
	}

	//Square of 2 triangles in the XY plane from -1 to 1, the front side faces +Z (up in Blender/Collada)
	const float SQUARE_VERTICES[] = { -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f };
	const unsigned int SQUARE_INDICES[] = { 0, 1, 2, 0, 2, 3 };
}

TEST_CASE("A dynamic box falls with the gravity of the earth", "[physics]")
{
	PhysicsWorld world;
	const vec3 start(1.0f, 10.0f, -2.0f);
	BodyHandle box = world.createDynamicBox(start, vec3(0.4f), 1.0f);

	simulate(world, 1.0f);

	vec3 position = positionOf(world, box);
	CHECK_THAT(start.y - position.y, WithinAbs(0.5f * GRAVITY, 0.25f)); //s = g * t^2 / 2
	CHECK_THAT(position.x, WithinAbs(start.x, 1e-4f));
	CHECK_THAT(position.z, WithinAbs(start.z, 1e-4f));
}

TEST_CASE("A dynamic box comes to rest on the ground", "[physics]")
{
	PhysicsWorld world;
	createGround(world);
	const float halfExtent = 0.4f;
	BodyHandle box = world.createDynamicBox(vec3(0.0f), vec3(halfExtent), 1.0f);

	simulate(world, 3.0f);
	mat4 resting = world.getTransform(box);
	CHECK_THAT(resting[3].y, WithinAbs(GROUND_TOP + halfExtent, 0.05f));
	CHECK_THAT(mat3(resting), IsNear(mat3(1.0f), 0.01f)); //still upright

	simulate(world, 1.0f);
	CHECK_THAT(positionOf(world, box), IsNear(vec3(resting[3]), 1e-3f));
}

TEST_CASE("Long frames are split into several steps and stay stable", "[physics]")
{
	//e.g. while the window is dragged the game loop stalls
	PhysicsWorld world;
	createGround(world);
	BodyHandle box = world.createDynamicBox(vec3(0.0f, 5.0f, 0.0f), vec3(0.4f), 1.0f);

	simulate(world, 4.0f, 0.5f);

	CHECK_THAT(positionOf(world, box).y, WithinAbs(GROUND_TOP + 0.4f, 0.1f));
}

TEST_CASE("Time does not advance for frames without duration", "[physics]")
{
	PhysicsWorld world;
	const vec3 start(0.0f, 10.0f, 0.0f);
	BodyHandle box = world.createDynamicBox(start, vec3(0.4f), 1.0f);

	world.update(0.0f);
	world.update(-1.0f);

	CHECK(positionOf(world, box) == start);
}

TEST_CASE("Static bodies stay where they were created", "[physics]")
{
	PhysicsWorld world;
	const vec3 boxPosition(0.0f, -1.0f, 0.0f);
	const vec3 spherePosition(5.0f, 0.0f, 0.0f);
	BodyHandle staticBox = world.createStaticBox(boxPosition, vec3(2.0f, 1.0f, 2.0f));
	BodyHandle staticSphere = world.createStaticSphere(spherePosition, 0.7f);
	//Falls onto both
	world.createDynamicBox(vec3(0.0f, 3.0f, 0.0f), vec3(0.5f), 1.0f);
	world.createDynamicBox(vec3(5.0f, 3.0f, 0.0f), vec3(0.5f), 1.0f);

	simulate(world, 2.0f);

	CHECK(world.getTransform(staticBox) == glm::translate(mat4(1.0f), boxPosition));
	CHECK(world.getTransform(staticSphere) == glm::translate(mat4(1.0f), spherePosition));
}

TEST_CASE("Every body gets its own valid handle", "[physics]")
{
	PhysicsWorld world;
	std::set<BodyHandle> handles;
	for (int i = 0; i < 5; ++i)
	{
		handles.insert(world.createDynamicBox(vec3(3.0f * i, 0.0f, 0.0f), vec3(0.4f), 1.0f));
		handles.insert(world.createStaticSphere(vec3(3.0f * i, 5.0f, 0.0f), 0.7f));
	}
	//The wall of the scene is only 2 cm thick (thinner than the default convex radius of Jolt)
	handles.insert(world.createStaticBox(vec3(0.0f, 0.0f, 3.0f), vec3(7.0f, 7.0f, 0.01f)));

	CHECK(handles.size() == 11);
	CHECK_FALSE(handles.contains(INVALID_BODY));
}

TEST_CASE("A triangle mesh is scaled, rotated and placed", "[physics]")
{
	//Modelled with Z-up (like in Blender), rotated to Y-up and scaled
	PhysicsWorld world;
	const vec3 position(0.0f, 2.0f, 0.0f);
	const glm::quat rotation = glm::angleAxis(glm::radians(-90.0f), vec3(1.0f, 0.0f, 0.0f));
	BodyHandle mesh = world.createStaticMesh(SQUARE_VERTICES, 4, SQUARE_INDICES, 2, 10.0f, position, rotation);
	REQUIRE(mesh != INVALID_BODY);

	SECTION("The transformation of the body is the rotation and the position")
	{
		mat4 expected = glm::translate(mat4(1.0f), position) * glm::mat4_cast(rotation);
		CHECK_THAT(world.getTransform(mesh), IsNear(expected, 1e-5f));
	}

	SECTION("A box falling onto the scaled square lands on it")
	{
		//Outside of the unscaled square, inside of the scaled one
		BodyHandle box = world.createDynamicBox(vec3(5.0f, 6.0f, 5.0f), vec3(0.4f), 1.0f);
		simulate(world, 3.0f);
		CHECK_THAT(positionOf(world, box).y, WithinAbs(position.y + 0.4f, 0.05f));
	}

	SECTION("A box next to the scaled square falls past it")
	{
		BodyHandle box = world.createDynamicBox(vec3(15.0f, 6.0f, 15.0f), vec3(0.4f), 1.0f);
		simulate(world, 2.0f);
		CHECK(positionOf(world, box).y < position.y - 5.0f);
	}
}

TEST_CASE("Shapes built in other threads become static bodies that can be removed again", "[physics]")
{
	//Like the streamed world: the worker threads build the shapes, the game adds and removes the bodies
	PhysicsWorld world;
	std::shared_ptr<CollisionShape> ground, trunks;
	std::thread builder([&] {
		std::vector<vec3> vertices = { { -10.0f, 0.0f, -10.0f }, { 10.0f, 0.0f, -10.0f }, { 10.0f, 0.0f, 10.0f }, { -10.0f, 0.0f, 10.0f } };
		ground = PhysicsWorld::buildMeshShape(vertices, { 0, 2, 1, 0, 3, 2 });
		ShapePart trunk;
		trunk.type = ShapePart::CAPSULE;
		trunk.position = vec3(3.0f, 2.0f, 0.0f);
		trunk.halfHeight = 1.5f;
		trunk.radius = 0.3f;
		ShapePart stone;
		stone.position = vec3(-3.0f, 0.5f, 0.0f);
		stone.radius = 0.8f;
		trunks = PhysicsWorld::buildCompoundShape({ trunk, stone });
	});
	builder.join();
	REQUIRE(ground);
	REQUIRE(trunks);
	CHECK(ground->isValid());
	CHECK_FALSE(PhysicsWorld::buildMeshShape({}, {}));
	CHECK_FALSE(PhysicsWorld::buildCompoundShape({}));

	std::size_t before = world.getBodyCount();
	BodyHandle groundBody = world.addStaticBody(*ground);
	BodyHandle trunksBody = world.addStaticBody(*trunks);
	REQUIRE(groundBody != INVALID_BODY);
	REQUIRE(trunksBody != INVALID_BODY);
	CHECK(world.getBodyCount() == before + 2);

	//The shapes are in world space (the bodies are at the origin)
	RayHit hit;
	REQUIRE(world.castRay(vec3(1.0f, 5.0f, 1.0f), vec3(0.0f, -10.0f, 0.0f), hit, true));
	CHECK(hit.body == groundBody);
	CHECK_THAT(hit.position.y, WithinAbs(0.0f, 1e-3f));
	REQUIRE(world.castRay(vec3(3.0f, 2.0f, -5.0f), vec3(0.0f, 0.0f, 10.0f), hit, true));
	CHECK(hit.body == trunksBody);
	CHECK_THAT(hit.position.z, WithinAbs(-0.3f, 1e-3f));
	REQUIRE(world.castRay(vec3(-3.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), hit, true));
	CHECK_THAT(hit.position.y, WithinAbs(1.3f, 1e-3f));

	world.removeBody(trunksBody);
	world.removeBody(INVALID_BODY); //nothing happens
	CHECK(world.getBodyCount() == before + 1);
	REQUIRE(world.castRay(vec3(3.0f, 2.0f, -5.0f), vec3(0.0f, 0.0f, 10.0f), hit, true) == false);
	world.removeBody(groundBody);
	CHECK(world.getBodyCount() == before);
	CHECK_FALSE(world.castRay(vec3(1.0f, 5.0f, 1.0f), vec3(0.0f, -10.0f, 0.0f), hit, true));
}

TEST_CASE("Physics worlds can be created again and simulate the same way", "[physics]")
{
	//PhysicsWorld registers and unregisters the global Jolt factory
	std::vector<mat4> results;
	for (int i = 0; i < 3; ++i)
	{
		PhysicsWorld world;
		createGround(world);
		BodyHandle box = world.createDynamicBox(vec3(0.0f), vec3(0.4f), 1.0f);
		world.createDynamicBox(vec3(0.3f, 2.0f, 0.0f), vec3(0.4f), 1.0f); //falls onto the first one
		simulate(world, 1.0f);
		results.push_back(world.getTransform(box));
	}

	CHECK(results[1] == results[0]);
	CHECK(results[2] == results[0]);
}

TEST_CASE("A sphere cast stops where the sphere touches the first body", "[physics]")
{
	PhysicsWorld world;
	world.createStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(50.0f, 1.0f, 50.0f)); //top at y = 0
	world.optimizeBroadPhase();
	const float radius = 0.5f;
	RayHit hit;

	SECTION("Down onto the ground")
	{
		REQUIRE(world.castSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), radius, hit));
		CHECK_THAT(hit.fraction, WithinAbs((5.0f - radius) / 10.0f, 1e-3f));
		CHECK_THAT(hit.position, IsNear(vec3(0.0f), 1e-3f));
		CHECK_THAT(hit.normal, IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-3f));
	}

	SECTION("Along the ground without touching it")
	{
		CHECK_FALSE(world.castSphere(vec3(0.0f, 1.0f, 0.0f), vec3(10.0f, 0.0f, 0.0f), radius, hit));
	}

	SECTION("A ray would pass a body that the sphere touches")
	{
		world.createStaticSphere(vec3(5.0f, 1.0f, 0.7f), 0.4f);
		RayHit rayHit;
		CHECK_FALSE(world.castRay(vec3(0.0f, 1.0f, 0.0f), vec3(10.0f, 0.0f, 0.0f), rayHit));
		REQUIRE(world.castSphere(vec3(0.0f, 1.0f, 0.0f), vec3(10.0f, 0.0f, 0.0f), radius, hit));
		CHECK(hit.fraction > 0.3f);
		CHECK(hit.fraction < 0.5f);
	}

	SECTION("Only static bodies")
	{
		world.createDynamicBox(vec3(0.0f, 3.0f, 0.0f), vec3(0.4f), 1.0f);
		REQUIRE(world.castSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), radius, hit));
		CHECK(hit.fraction < 0.2f); //the box
		REQUIRE(world.castSphere(vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -10.0f, 0.0f), radius, hit, true));
		CHECK_THAT(hit.fraction, WithinAbs((5.0f - radius) / 10.0f, 1e-3f)); //the ground
	}
}

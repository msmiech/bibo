#include <cmath>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using bibo::scene::MeshTransform;
using bibo::scene::terrainTransform;
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
	CHECK(handles.count(INVALID_BODY) == 0);
}

TEST_CASE("A triangle mesh is placed like its model matrix", "[physics]")
{
	//Like the terrain (Scene::initPhysicsBodies): modelled with Z-up in Blender, rotated to Y-up and scaled
	PhysicsWorld world;
	const vec3 position(0.0f, 2.0f, 0.0f);
	const mat4 modelMatrix = glm::scale(glm::rotate(glm::translate(mat4(1.0f), position), glm::radians(-90.0f), vec3(1.0f, 0.0f, 0.0f)), vec3(10.0f));
	const MeshTransform transform = terrainTransform(modelMatrix);
	BodyHandle mesh = world.createStaticMesh(SQUARE_VERTICES, 4, SQUARE_INDICES, 2, transform.scale, transform.position, transform.rotation);
	REQUIRE(mesh != INVALID_BODY);

	SECTION("Scale, rotation and position are taken from the model matrix")
	{
		CHECK_THAT(transform.scale, WithinAbs(10.0f, 1e-5f));
		CHECK_THAT(transform.position, IsNear(position, 1e-6f));
		CHECK_THAT(transform.rotation * vec3(0.0f, 0.0f, 1.0f), IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-6f)); //up axis
	}

	SECTION("The transformation of the body is the model matrix without the scale")
	{
		CHECK_THAT(world.getTransform(mesh), IsNear(glm::scale(modelMatrix, vec3(0.1f)), 1e-5f));
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

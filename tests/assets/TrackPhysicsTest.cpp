#include <algorithm>
#include <cmath>
#include <vector>

#include <assimp/scene.h>
#include <catch2/catch_test_macros.hpp>

#include "Physics/Physics.hpp"
#include "Scene/Bear.hpp"
#include "Scene/GameRules.hpp"
#include "Scene/Scene.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using namespace bibo::test;
using bibo::scene::Bear;
using bibo::scene::MeshTransform;
using bibo::scene::terrainTransform;
using bibo::test::IsNear;
namespace rules = bibo::scene::rules;
using glm::vec3;
using glm::vec4;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	const vec3 DOWN(0.0f, -5.0f, 0.0f);
	const float ROCK_RADIUS = 0.7f;
	const float BACKWARD_SPEED = 7.5f; //Bear.cpp

	vec3 startPosition(const std::string& nodeName)
	{
		return vec3(nodeModelMatrix(trackNode(nodeName))[3]);
	}

	/**
	* The first mesh of the terrain node, as the game loads it (Model, Mesh): Mesh::vertices, Mesh::indices
	*/
	struct TerrainMesh
	{
		std::vector<float> vertices;
		std::vector<unsigned int> indices;

		TerrainMesh()
		{
			aiNode& node = trackNode("Boden");
			REQUIRE(node.mNumMeshes > 0);
			const aiMesh* mesh = trackScene().mMeshes[node.mMeshes[0]];
			for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
			{
				vertices.insert(vertices.end(), { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z });
			}
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
			{
				//Points and lines become degenerate triangles
				const aiFace& face = mesh->mFaces[i];
				bool isTriangle = face.mNumIndices == 3;
				indices.insert(indices.end(), { face.mIndices[0], isTriangle ? face.mIndices[1] : face.mIndices[0], isTriangle ? face.mIndices[2] : face.mIndices[0] });
			}
		}

		unsigned int numVertices() const { return static_cast<unsigned int>(vertices.size() / 3); }
		unsigned int numTriangles() const { return static_cast<unsigned int>(indices.size() / 3); }
		vec3 vertex(unsigned int i) const { return vec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]); }
	};

	/**
	* The static bodies of the track as Scene::initPhysicsBodies creates them
	*/
	struct TrackPhysics
	{
		PhysicsWorld world;
		std::vector<BodyHandle> rocks;
		const vec3 bearStart = startPosition("BearBody");

		TrackPhysics()
		{
			world.createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f)); //ground
			for (int i = 1; i <= 12; ++i)
			{
				rocks.push_back(world.createStaticSphere(startPosition("Rock" + std::to_string(i)), ROCK_RADIUS));
			}
			TerrainMesh terrain;
			MeshTransform transform = terrainTransform(nodeModelMatrix(trackNode("Boden")));
			BodyHandle terrainBody = world.createStaticMesh(terrain.vertices.data(), terrain.numVertices(), terrain.indices.data(), terrain.numTriangles(),
				transform.scale, transform.position, transform.rotation);
			REQUIRE(terrainBody != INVALID_BODY);
			world.optimizeBroadPhase();
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
		* Places the bear and moves it every frame with the given velocity (like Bear::update, DOWN is the gravity)
		*/
		Run moveBear(vec3 start, float seconds, vec3 velocity)
		{
			Run run;
			CharacterController bear(world, Bear::characterControllerDesc(start), [&](const CharacterController::Hit& hit) { run.touchedBodies.push_back(hit.body); });
			for (int i = 0; i < static_cast<int>(seconds / FRAME); ++i)
			{
				world.update(FRAME);
				run.supported = bear.move(velocity * FRAME, FRAME);
				run.path.push_back(bear.getPosition());
			}
			return run;
		}
	};
}

TEST_CASE("The bear starts standing on the terrain without touching a rock", "[assets][physics][bear]")
{
	TrackPhysics track;

	TrackPhysics::Run run = track.moveBear(track.bearStart, 3.0f, DOWN);

	vec3 position = run.end();
	CAPTURE(track.bearStart, position);
	CHECK(run.supported);
	CHECK_FALSE(rules::hasFallenOff(position)); //did not fall through the terrain onto the ground box
	CHECK(std::abs(position.y - track.bearStart.y) < 2.0f); //starts close to the surface
	CHECK(glm::distance(vec3(position.x, 0.0f, position.z), vec3(track.bearStart.x, 0.0f, track.bearStart.z)) < 0.5f); //does not slide away
	for (BodyHandle rock : track.rocks)
	{
		CHECK_FALSE(run.touched(rock)); //would be game over
	}
}

TEST_CASE("Going backwards from the start neither passes a checkpoint nor completes the lap", "[assets][physics][bear][rules]")
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

TEST_CASE("Next to the track the bear falls onto the ground box and the game is lost", "[assets][physics][bear][rules]")
{
	TrackPhysics track;

	TrackPhysics::Run run = track.moveBear(vec3(600.0f, 10.0f, 600.0f), 5.0f, DOWN);

	CAPTURE(run.end());
	CHECK(run.supported);
	CHECK(rules::hasFallenOff(run.end()));
}

TEST_CASE("The collision mesh of the terrain matches the rendered terrain", "[assets][physics]")
{
	//Rendered: model matrix of the node (Geometry) * vertex. Physics: rotation * (scale * vertex) + position (Scene::initPhysicsBodies)
	const glm::mat4 modelMatrix = nodeModelMatrix(trackNode("Boden"));
	const MeshTransform transform = terrainTransform(modelMatrix);
	TerrainMesh terrain;
	REQUIRE(terrain.numVertices() > 0);

	float largestDifference = 0.0f;
	unsigned int worstVertex = 0;
	for (unsigned int i = 0; i < terrain.numVertices(); ++i)
	{
		vec3 rendered = vec3(modelMatrix * vec4(terrain.vertex(i), 1.0f));
		vec3 collision = transform.rotation * (transform.scale * terrain.vertex(i)) + transform.position;
		float difference = glm::distance(rendered, collision);
		if (difference > largestDifference)
		{
			largestDifference = difference;
			worstVertex = i;
		}
	}

	CAPTURE(worstVertex, vec3(modelMatrix * vec4(terrain.vertex(worstVertex), 1.0f)));
	CHECK(largestDifference < 0.01f);
}

TEST_CASE("The terrain is turned from the Z-up axis of Blender to the Y-up axis of the game", "[assets][physics]")
{
	const MeshTransform transform = terrainTransform(nodeModelMatrix(trackNode("Boden")));

	CHECK_THAT(transform.rotation * vec3(0.0f, 0.0f, 1.0f), IsNear(vec3(0.0f, 1.0f, 0.0f), 1e-5f));
}

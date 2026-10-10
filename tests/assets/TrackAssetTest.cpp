#include <assimp/scene.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
namespace rules = bibo::scene::rules;
using glm::vec3;

namespace
{
	/**
	* Meshes the game creates for a node (Model::createMeshes): the meshes of the node, or of its children if it has any
	*/
	std::vector<const aiMesh*> meshesOf(const aiNode& node)
	{
		const aiScene& scene = trackScene();
		std::vector<const aiMesh*> meshes;
		if (node.mNumChildren == 0)
		{
			for (unsigned int i = 0; i < node.mNumMeshes; ++i)
			{
				meshes.push_back(scene.mMeshes[node.mMeshes[i]]);
			}
		}
		else
		{
			for (unsigned int i = 0; i < node.mNumChildren; ++i)
			{
				for (unsigned int j = 0; j < node.mChildren[i]->mNumMeshes; ++j)
				{
					meshes.push_back(scene.mMeshes[node.mChildren[i]->mMeshes[j]]);
				}
			}
		}
		return meshes;
	}

	vec3 startPosition(const std::string& nodeName)
	{
		return vec3(nodeModelMatrix(trackNode(nodeName))[3]);
	}
}

TEST_CASE("Every node the game loads is a triangle mesh in the track", "[assets]")
{
	const std::string name = GENERATE(from_range(gameNodeNames()));
	CAPTURE(name);

	//The game uses meshes[0] of several of these nodes without checking
	std::vector<const aiMesh*> meshes = meshesOf(trackNode(name));
	REQUIRE_FALSE(meshes.empty());

	for (const aiMesh* mesh : meshes)
	{
		CAPTURE(mesh->mName.C_Str());
		CHECK(mesh->HasPositions());
		CHECK(mesh->HasNormals());
		CHECK(mesh->mNumFaces > 0);
		CHECK((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0);
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
		{
			const aiFace& face = mesh->mFaces[f];
			for (unsigned int i = 0; i < face.mNumIndices; ++i)
			{
				if (face.mIndices[i] >= mesh->mNumVertices)
				{
					FAIL("Face " << f << " uses vertex " << face.mIndices[i] << " of " << mesh->mNumVertices);
				}
			}
		}
	}
}

TEST_CASE("Normal mapped nodes have texture coordinates and tangents", "[assets]")
{
	const std::string name = GENERATE(from_range(normalMappedNodeNames()));
	CAPTURE(name);

	for (const aiMesh* mesh : meshesOf(trackNode(name)))
	{
		CAPTURE(mesh->mName.C_Str());
		CHECK(mesh->HasTextureCoords(0));
		CHECK(mesh->HasTangentsAndBitangents());
	}
}

TEST_CASE("The track contains the sun light", "[assets]")
{
	//The scene uses the first light for lighting and shadows
	CHECK(trackScene().mNumLights >= 1);
}

TEST_CASE("The bear starts on the track, outside of the finish area", "[assets][rules]")
{
	const vec3 bear = startPosition("BearBody");
	const vec3 goal = startPosition("Cube"); //Scene::update: the goal is geometry 17

	CAPTURE(bear, goal);
	CHECK_FALSE(rules::hasFallenOff(bear));
	CHECK_FALSE(rules::hasReachedGoal(bear, goal));
	CHECK_FALSE(rules::hasFallenOff(goal)); //the goal can be reached without falling off
}

TEST_CASE("The limbs of the bear are next to its body", "[assets]")
{
	//Bear::Bear: the limbs are positioned relative to the body
	const vec3 body = startPosition("BearBody");
	const std::string limb = GENERATE(values<std::string>({ "LeftHand", "RightHand", "LeftLeg", "RightLeg" }));
	CAPTURE(limb);

	CHECK(glm::distance(startPosition(limb), body) < 3.0f);
}

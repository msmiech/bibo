#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "Scene/CreatureMeshes.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Scene/Hedgehog.hpp"
#include "Scene/HedgehogModel.hpp"
#include "Scene/Beaver.hpp"
#include "Scene/BeaverModel.hpp"
#include "Scene/Salmon.hpp"
#include "TestSupport.hpp"

using namespace bibo::scene;
using glm::vec2;
using glm::vec3;

namespace
{
	const float FRAME = 1.0f / 60.0f;

	/**
	* Flat ground at y = 2 on a strip along z (|x| < 5), like the floor of the track
	*/
	bool strip(vec2 position, float& height)
	{
		height = 2.0f;
		return std::abs(position.x) < 5.0f;
	}

	WaterPool pool(vec3 center)
	{
		WaterPool water;
		water.center = center;
		water.radius = 3.0f;
		for (float x = -2.0f; x <= 2.0f; x += 1.0f)
		{
			water.spots.push_back(vec2(center.x + x, center.z));
		}
		return water;
	}
}

TEST_CASE("A hedgehog strolls around its home on the ground it can walk on", "[animals]")
{
	const vec2 home(0.0f, 10.0f);
	Hedgehog hedgehog(home, vec2(0.0f, 1.0f), 7u, strip);
	CHECK(hedgehog.getPosition() == vec3(0.0f, 2.0f, 10.0f));
	CHECK_FALSE(hedgehog.isWalking()); //sniffs first

	float walked = 0.0f, farthest = 0.0f;
	vec3 last = hedgehog.getPosition();
	bool walkedOnce = false;
	for (int i = 0; i < 60 * 60; ++i)
	{
		hedgehog.update(FRAME);
		vec3 position = hedgehog.getPosition();
		walked += glm::distance(position, last);
		last = position;
		walkedOnce = walkedOnce || hedgehog.isWalking();
		farthest = std::max(farthest, glm::distance(vec2(position.x, position.z), home));
		REQUIRE(std::abs(position.x) < 5.0f); //stays on the strip
		REQUIRE(position.y == 2.0f);
		//The collision sphere is around the body, which stands on the ground
		REQUIRE(hedgehog.getColliderCenter().y - hedgehog.getColliderRadius() > 1.9f);
	}
	CAPTURE(walked, farthest);
	CHECK(walkedOnce);
	CHECK(walked > 15.0f); //it walks, but slowly (and pauses)
	CHECK(walked < Hedgehog::WALK_SPEED * 60.0f);
	CHECK(farthest <= Hedgehog::HOME_RADIUS + 0.5f);

	SECTION("Its feet are under its body and step while it walks")
	{
		Hedgehog::Pose pose = hedgehog.getPose();
		for (const vec3& foot : pose.feet)
		{
			CHECK(glm::distance(vec2(foot.x, foot.z), vec2(last.x, last.z)) < 0.4f * Hedgehog::SCALE);
			CHECK(foot.y >= 2.0f - 1e-4f);
		}
	}
	SECTION("Reset: back home")
	{
		hedgehog.reset();
		CHECK(hedgehog.getPosition() == vec3(0.0f, 2.0f, 10.0f));
	}
	SECTION("Startled it hops up and lands again")
	{
		float before = hedgehog.getColliderCenter().y;
		hedgehog.startle();
		hedgehog.update(0.2f);
		CHECK(hedgehog.getColliderCenter().y > before + 0.2f);
		hedgehog.update(0.5f);
		CHECK(std::abs(hedgehog.getColliderCenter().y - before) < 0.1f); //it may bob with its steps
	}
}

TEST_CASE("Salmon only jump while the bear is close to their pool, out of the water and back into it", "[animals]")
{
	SalmonPools salmon({ pool(vec3(0.0f, -0.5f, 0.0f)) }, 3u);
	const vec3 far(0.0f, 1.0f, 100.0f);
	for (int i = 0; i < 5 * 60; ++i)
	{
		salmon.update(FRAME, far, far, true);
	}
	CHECK(salmon.getJumps().empty());

	const vec3 near(0.0f, 1.0f, 15.0f); //not close enough to catch one
	float highest = -1e9f;
	int jumps = 0;
	for (int i = 0; i < 10 * 60; ++i)
	{
		std::size_t before = salmon.getJumps().size();
		REQUIRE(salmon.update(FRAME, near, near, true) == -1);
		jumps += salmon.getJumps().size() > before ? 1 : 0;
		for (const SalmonPools::Jump& jump : salmon.getJumps())
		{
			vec3 position = jump.position();
			highest = std::max(highest, position.y);
			REQUIRE(position.y >= -0.5f - 0.31f); //from just below the surface
			CHECK(glm::distance(vec2(position.x, position.z), vec2(0.0f)) < 3.0f + 4.0f); //not far from its pool
		}
		REQUIRE(salmon.getJumps().size() <= 2);
	}
	CAPTURE(jumps, highest);
	CHECK(jumps >= 4); //every now and then
	CHECK(highest > -0.5f + 2.0f); //high enough for the bear to catch it in a jump
	CHECK_FALSE(salmon.getDroplets().empty()); //splashes
}

TEST_CASE("The bear catches salmon in the air, none jump out of closed pools", "[animals]")
{
	SalmonPools salmon({ pool(vec3(0.0f, -0.5f, 0.0f)), pool(vec3(0.0f, -0.5f, 40.0f)) }, 5u);
	const vec3 near(0.0f, 1.0f, 8.0f);
	//Waits for a salmon of the first pool, then puts its mouth where the salmon is
	auto catchOne = [&](const std::vector<bool>& closed) {
		int caught = -1;
		for (int i = 0; i < 10 * 60 && caught < 0; ++i)
		{
			vec3 mouth = near;
			if (!salmon.getJumps().empty() && salmon.getJumps().front().time > 0.3f)
			{
				mouth = salmon.getJumps().front().position();
			}
			caught = salmon.update(FRAME, near, mouth, true, closed);
		}
		return caught;
	};
	REQUIRE(catchOne({}) == 0);

	SECTION("Open pools stay open: the bear can catch another one there")
	{
		CHECK(catchOne({}) == 0);
		CHECK(catchOne({ false, false }) == 0);
	}
	SECTION("No more salmon jump out of a closed pool, and none can be caught there")
	{
		const std::vector<bool> closed = { true, false };
		for (int i = 0; i < 3 * 60; ++i)
		{
			salmon.update(FRAME, near, near, true, closed);
		}
		for (int i = 0; i < 5 * 60; ++i)
		{
			for (const SalmonPools::Jump& jump : salmon.getJumps())
			{
				REQUIRE(jump.pool != 0);
			}
			vec3 mouth = salmon.getJumps().empty() ? near : salmon.getJumps().front().position();
			REQUIRE(salmon.update(FRAME, near, mouth, true, closed) == -1);
		}
	}
	SECTION("The salmon of a pool that is closed while they are in the air cannot be caught")
	{
		for (int i = 0; i < 10 * 60 && (salmon.getJumps().empty() || salmon.getJumps().front().time < 0.3f); ++i)
		{
			salmon.update(FRAME, near, near, false);
		}
		REQUIRE_FALSE(salmon.getJumps().empty());
		CHECK(salmon.update(FRAME, near, salmon.getJumps().front().position(), true, { true, true }) == -1);
	}
	SECTION("Not while the bear cannot catch one (it holds one already)")
	{
		const vec3 second(0.0f, 1.0f, 48.0f);
		for (int i = 0; i < 10 * 60; ++i)
		{
			vec3 mouth = salmon.getJumps().empty() ? second : salmon.getJumps().front().position();
			REQUIRE(salmon.update(FRAME, second, mouth, false) == -1);
		}
	}
	SECTION("Reset: no salmon in the air")
	{
		salmon.reset();
		CHECK(salmon.getJumps().empty());
		CHECK(salmon.getDroplets().empty());
	}
}

TEST_CASE("The largest pool is the one with the most water", "[animals]")
{
	CHECK(largestPool({}) == -1);
	std::vector<WaterPool> pools = { pool(vec3(0.0f)), pool(vec3(0.0f, 0.0f, 40.0f)), pool(vec3(0.0f, 0.0f, 80.0f)) };
	pools[0].area = 30.0f;
	pools[1].area = 75.0f;
	pools[2].area = 50.0f;
	CHECK(largestPool(pools) == 1);
}

TEST_CASE("The models of the animals are closed meshes of the right size", "[animals]")
{
	const int index = GENERATE(range(0, CREATURE_MODEL_COUNT));
	CAPTURE(index);
	bibo::world::VegetationMesh mesh = buildCreatureModel(static_cast<CreatureModel>(index));
	REQUIRE_FALSE(mesh.vertices.empty());
	REQUIRE(mesh.indices.size() % 3 == 0);
	for (std::uint32_t i : mesh.indices)
	{
		REQUIRE(i < mesh.vertices.size());
	}
	for (const bibo::world::VegetationVertex& vertex : mesh.vertices)
	{
		REQUIRE(std::isfinite(vertex.position.x + vertex.position.y + vertex.position.z));
		REQUIRE(std::abs(glm::length(vertex.normal) - 1.0f) < 1e-3f);
		CHECK(vertex.parameters[1] == 0); //no wind
		CHECK(vertex.parameters[2] == 0); //no snow on them
	}
	CHECK(mesh.radius > 0.0f);
	CHECK(mesh.radius <= 1.0001f); //the hedgehog and the salmon are about 1 long, a droplet has a radius of 1
}

TEST_CASE("The hedgehog has spines on its back, fur on its face and belly, eyes, a nose and claws", "[animals]")
{
	using bibo::resources::SkinnedModel;
	HedgehogModel model = HedgehogModel::build();
	const std::size_t vertices = model.positions.size();
	REQUIRE(vertices > 0);
	REQUIRE(model.normals.size() == vertices);
	REQUIRE(model.colors.size() == vertices);
	REQUIRE(model.surfaces.size() == vertices);
	REQUIRE(model.parts.size() == vertices);
	REQUIRE(model.combs.size() == vertices);
	REQUIRE(model.spines.size() == vertices);
	REQUIRE(model.indices.size() % 3 == 0);
	for (unsigned int index : model.indices)
	{
		REQUIRE(index < vertices);
	}
	CAPTURE(model.spineTriangles.size(), model.furTriangles.size());
	CHECK(model.spineTriangles.size() > model.furTriangles.size()); //mostly spines
	CHECK_FALSE(model.furTriangles.empty());
	for (std::size_t i = 0; i < model.spineTriangles.size(); i += 3)
	{
		//Down to the belly (they hide the legs) and behind the face
		vec3 center = (model.positions[model.spineTriangles[i]] + model.positions[model.spineTriangles[i + 1]] + model.positions[model.spineTriangles[i + 2]]) / 3.0f;
		REQUIRE(center.y > 0.03f);
		REQUIRE(center.z < 0.45f);
	}
	int surfaces[7] = {};
	for (std::size_t v = 0; v < vertices; ++v)
	{
		surfaces[static_cast<int>(model.surfaces[v])]++;
		REQUIRE(std::abs(glm::length(model.normals[v]) - 1.0f) < 1e-3f);
		if (model.parts[v] == HedgehogModel::BODY && model.surfaces[v] == SkinnedModel::FUR)
		{
			REQUIRE(model.positions[v].y >= 0.0f); //not in the ground
		}
	}
	CHECK(surfaces[SkinnedModel::EYE] > 0);
	CHECK(surfaces[SkinnedModel::NOSE] > 0);
	CHECK(surfaces[SkinnedModel::CLAW] > 0);
	//The snout points forward, it is the front of the hedgehog
	float front = -1e9f;
	for (std::size_t v = 0; v < vertices; ++v)
	{
		if (model.parts[v] == HedgehogModel::BODY)
		{
			front = std::max(front, model.positions[v].z);
		}
	}
	CHECK(front > 0.5f);
}

TEST_CASE("The beaver sits upright: fur, orange front teeth, eyes, a nose, a mouth, claws and a flat tail", "[animals][beaver]")
{
	using bibo::resources::SkinnedModel;
	BeaverModel model = BeaverModel::build();
	const std::size_t vertices = model.positions.size();
	REQUIRE(vertices > 0);
	REQUIRE(model.normals.size() == vertices);
	REQUIRE(model.colors.size() == vertices);
	REQUIRE(model.surfaces.size() == vertices);
	REQUIRE(model.parts.size() == vertices);
	REQUIRE(model.combs.size() == vertices);
	REQUIRE(model.indices.size() % 3 == 0);
	for (unsigned int index : model.indices)
	{
		REQUIRE(index < vertices);
	}
	CHECK(model.spineTriangles.empty()); //no spines
	CHECK(model.furTriangles.size() > model.indices.size() / 3); //fur on most of it

	int surfaces[7] = {};
	int parts[BeaverModel::PART_COUNT] = {};
	for (std::size_t v = 0; v < vertices; ++v)
	{
		surfaces[static_cast<int>(model.surfaces[v])]++;
		REQUIRE(model.parts[v] >= 0);
		REQUIRE(model.parts[v] < BeaverModel::PART_COUNT);
		parts[model.parts[v]]++;
		REQUIRE(std::abs(glm::length(model.normals[v]) - 1.0f) < 1e-3f);
		REQUIRE(std::abs(glm::length(model.combs[v]) - 1.0f) < 1e-3f);
	}
	CHECK(surfaces[SkinnedModel::EYE] > 0);
	CHECK(surfaces[SkinnedModel::NOSE] > 0);
	CHECK(surfaces[SkinnedModel::TEETH] > 0);
	CHECK(surfaces[SkinnedModel::MOUTH] > 0);
	CHECK(surfaces[SkinnedModel::TONGUE] > 0);
	CHECK(surfaces[SkinnedModel::CLAW] > 0);
	for (int part = 0; part < BeaverModel::PART_COUNT; ++part)
	{
		CAPTURE(part);
		CHECK(parts[part] > 0);
	}
	//The teeth are orange
	for (std::size_t v = 0; v < vertices; ++v)
	{
		if (model.surfaces[v] == SkinnedModel::TEETH)
		{
			REQUIRE(model.colors[v].r > 2.0f * model.colors[v].g);
			REQUIRE(model.colors[v].g > model.colors[v].b);
		}
	}
	//The body sits on the ground and is about 1 high with its head (the head is in the space of the neck)
	float bottom = 1e9f, top = -1e9f;
	for (std::size_t v = 0; v < vertices; ++v)
	{
		if (model.parts[v] == BeaverModel::BODY)
		{
			bottom = std::min(bottom, model.positions[v].y);
		}
		if (model.parts[v] == BeaverModel::HEAD)
		{
			top = std::max(top, model.positions[v].y + BEAVER_NECK.y);
		}
	}
	CHECK(bottom >= -0.01f);
	CHECK(bottom < 0.05f);
	CHECK(top > 0.9f * BEAVER_HEIGHT);
	CHECK(top < 1.1f * BEAVER_HEIGHT);
}

TEST_CASE("The beaver turns to the bear when it comes close, talks with its mouth and cheers", "[animals][beaver]")
{
	const vec3 home(10.0f, 2.0f, -5.0f);
	Beaver beaver(home, vec3(1.0f, 0.0f, 0.0f));
	std::vector<glm::mat4> pose = beaver.getPose();
	REQUIRE(pose.size() == static_cast<std::size_t>(BeaverModel::PART_COUNT));
	auto forward = [&]() {
		glm::mat4 body = beaver.getPose()[BeaverModel::BODY];
		return glm::normalize(vec3(body[2].x, 0.0f, body[2].z));
	};
	CHECK(glm::dot(forward(), vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
	//Sitting on the ground, the marker above its head
	CHECK(std::abs(vec3(pose[BeaverModel::BODY][3]).y - home.y) < 0.05f);
	CHECK(beaver.getMarkerPosition().y > home.y + BEAVER_HEIGHT * Beaver::SCALE);
	CHECK(beaver.getColliderRadius() > 0.5f);

	SECTION("A bear far away is not noticed")
	{
		for (int i = 0; i < 120; ++i)
		{
			beaver.update(FRAME, home + vec3(0.0f, 1.0f, -40.0f));
		}
		CHECK(glm::dot(forward(), vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
	}
	SECTION("A bear close by: it turns to it")
	{
		for (int i = 0; i < 120; ++i)
		{
			beaver.update(FRAME, home + vec3(0.0f, 1.0f, -6.0f));
		}
		CHECK(glm::dot(forward(), vec3(0.0f, 0.0f, -1.0f)) > 0.95f);
	}
	SECTION("Its eyes look at the bear: up when the bear is above its face")
	{
		auto gazeY = [&](vec3 bear) {
			for (int i = 0; i < 120; ++i)
			{
				beaver.update(FRAME, bear);
			}
			glm::mat3 eye(beaver.getPose()[BeaverModel::EYE_LEFT]);
			return glm::normalize(eye * BeaverModel::eyeGaze(true)).y;
		};
		vec3 face = beaver.getFacePosition();
		float level = gazeY(face + vec3(4.0f, 0.0f, 0.0f));
		float above = gazeY(face + vec3(3.0f, 3.0f, 0.0f));
		CHECK(above > level + 0.2f);
		CHECK(beaver.getFacePosition().y > face.y - 0.5f);
	}
	SECTION("Every sound opens and closes the mouth")
	{
		beaver.setTalking(true);
		CHECK(beaver.isTalking());
		beaver.update(FRAME, home);
		CHECK(beaver.getMouthOpen() < 0.05f);
		beaver.speak();
		float widest = 0.0f;
		for (int i = 0; i < 12; ++i)
		{
			beaver.update(FRAME, home);
			widest = std::max(widest, beaver.getMouthOpen());
		}
		CHECK(widest > 0.8f);
		CHECK(beaver.getMouthOpen() < 0.1f);
	}
	SECTION("Cheering: it hops")
	{
		beaver.cheer();
		float highest = home.y;
		for (int i = 0; i < 60; ++i)
		{
			beaver.update(FRAME, home + vec3(0.0f, 1.0f, -6.0f));
			highest = std::max(highest, vec3(beaver.getPose()[BeaverModel::BODY][3]).y);
		}
		CHECK(highest > home.y + 0.1f);
		CHECK(std::abs(vec3(beaver.getPose()[BeaverModel::BODY][3]).y - home.y) < 0.05f); //back on the ground
	}
	for (const glm::mat4& part : beaver.getPose())
	{
		REQUIRE(std::isfinite(part[3].x + part[3].y + part[3].z));
	}
}

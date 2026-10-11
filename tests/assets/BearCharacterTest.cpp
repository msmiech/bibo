#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Physics/Physics.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Scene/BearCharacter.hpp"
#include "TestSupport.hpp"

using namespace bibo::physics;
using namespace bibo::test;
using bibo::scene::BearCharacter;
using bibo::scene::BearInput;
using bibo::scene::BearRig;
using Catch::Matchers::WithinAbs;
using glm::vec3;

namespace
{
	const float FRAME = 1.0f / 60.0f;
	using State = BearCharacter::State;

	/**
	* The bear on flat ground (top at y = 0), looking along +z, optionally with a rock in its way
	*/
	struct BearOnGround
	{
		PhysicsWorld world;
		BodyHandle rock = INVALID_BODY;
		std::unique_ptr<BearCharacter> bear;
		std::vector<State> states;

		explicit BearOnGround(bool withRock = false)
		{
			world.createStaticBox(vec3(0.0f, -1.0f, 0.0f), vec3(200.0f, 1.0f, 200.0f));
			if (withRock)
			{
				rock = world.createStaticSphere(vec3(0.0f, 0.7f, 9.0f), 0.7f); //like the rocks of the track
			}
			world.optimizeBroadPhase();
			bear = std::make_unique<BearCharacter>(world, bearModel(), vec3(0.0f, 1.4f, 0.0f), vec3(0.0f, 0.0f, 1.0f),
				[this](const CharacterController::Hit& hit) {
					if (hit.body == rock)
					{
						bear->knockDown(hit);
					}
				});
		}

		void run(float seconds, BearInput input = {})
		{
			int frames = static_cast<int>(std::lround(seconds / FRAME));
			for (int i = 0; i < frames; ++i)
			{
				world.update(FRAME);
				bear->update(FRAME, input);
				if (states.empty() || states.back() != bear->getState())
				{
					states.push_back(bear->getState());
				}
			}
		}

		/**
		* @return How far the paw of a leg is from where it should be on the ground
		*/
		float pawError(BearRig::Leg leg) const
		{
			const BearRig& rig = bear->getRig();
			const char* const paws[] = { "hand.L", "hand.R", "foot.L", "foot.R" };
			vec3 paw = rig.getBonePosition(rig.getBone(paws[leg]));
			return glm::distance(paw, rig.getFootTarget(leg) + vec3(0.0f, rig.getPawHeight(leg), 0.0f));
		}
	};

	BearInput forward()
	{
		BearInput input;
		input.forward = 1.0f;
		return input;
	}
}

TEST_CASE("The bear stands with its paws on the ground", "[assets][bear][animation]")
{
	BearOnGround scene;
	scene.run(1.0f);

	CHECK(scene.bear->getState() == State::CONTROL);
	for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg)
	{
		CAPTURE(leg);
		CHECK(scene.bear->getRig().isFootPlanted(static_cast<BearRig::Leg>(leg)));
		CHECK_THAT(scene.bear->getRig().getFootTarget(static_cast<BearRig::Leg>(leg)).y, WithinAbs(0.0f, 0.01f));
		CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.02f);
	}
	SECTION("The skinned bear is where the character is")
	{
		vec3 low(1e9f), high(-1e9f);
		for (const vec3& p : scene.bear->getPositions())
		{
			low = glm::min(low, p);
			high = glm::max(high, p);
		}
		vec3 center = 0.5f * (low + high);
		vec3 position = scene.bear->getPosition();
		CHECK(glm::distance(vec3(center.x, 0.0f, center.z), vec3(position.x, 0.0f, position.z)) < 0.4f);
		CHECK(center.y > 0.6f);
		CHECK(center.y < 1.5f);
	}
}

TEST_CASE("Running: the paws stay where they were planted and the bear speeds up smoothly", "[assets][bear][animation]")
{
	BearOnGround scene;
	scene.run(0.5f);
	vec3 start = scene.bear->getPosition();

	scene.run(0.1f, forward());
	float afterTenthSecond = scene.bear->getPosition().z - start.z;
	CHECK(afterTenthSecond > 0.05f);
	CHECK(afterTenthSecond < 1.0f); //not at full speed at once

	std::array<vec3, BearRig::LEG_COUNT> planted;
	std::array<bool, BearRig::LEG_COUNT> wasPlanted{};
	int plantedFrames = 0;
	for (int frame = 0; frame < 120; ++frame)
	{
		scene.run(FRAME, forward());
		for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg)
		{
			BearRig::Leg l = static_cast<BearRig::Leg>(leg);
			bool isPlanted = scene.bear->getRig().isFootPlanted(l);
			if (isPlanted && wasPlanted[leg])
			{
				//A planted paw does not slide
				CHECK(glm::distance(scene.bear->getRig().getFootTarget(l), planted[leg]) < 1e-4f);
				CHECK(scene.pawError(l) < 0.12f);
				++plantedFrames;
			}
			planted[leg] = scene.bear->getRig().getFootTarget(l);
			wasPlanted[leg] = isPlanted;
		}
	}
	CHECK(plantedFrames > 50);
	CHECK_THAT(scene.bear->getPosition().z - start.z, WithinAbs(0.1f * 3.0f + 2.0f * 10.0f, 2.0f));
}

TEST_CASE("Running into a rock knocks the bear down, it tumbles, gets up and can run again", "[assets][bear][ragdoll]")
{
	BearOnGround scene(true);
	scene.run(0.5f);
	scene.run(2.0f, forward());
	REQUIRE(scene.bear->getState() == State::RAGDOLL);

	SECTION("The fur is ruffled")
	{
		float maxBend = 0.0f;
		for (int i = 0; i < 20; ++i)
		{
			scene.run(FRAME);
			for (const vec3& offset : scene.bear->getFurOffsets())
			{
				maxBend = std::max(maxBend, glm::length(offset));
			}
		}
		CHECK(maxBend > 0.3f);
	}
	SECTION("Gets up after it lies still")
	{
		for (int i = 0; i < 8 * 60 && !(scene.states.size() > 2 && scene.bear->getState() == State::CONTROL); ++i)
		{
			scene.run(FRAME);
		}
		REQUIRE(scene.bear->getState() == State::CONTROL);
		CHECK(scene.states == std::vector<State>({ State::CONTROL, State::RAGDOLL, State::GETTING_UP, State::CONTROL }));
		//On its feet again, not inside of the rock
		vec3 position = scene.bear->getPosition();
		CHECK(glm::distance(position, vec3(0.0f, 0.7f, 9.0f)) > 0.7f + 0.9f);
		CHECK_THAT(position.y, WithinAbs(1.35f, 0.1f));
		for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg)
		{
			CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.05f);
		}
		CHECK(scene.bear->isProtected());

		SECTION("Right after getting up a hit does not knock it down again")
		{
			CharacterController::Hit hit;
			hit.normal = vec3(0.0f, 0.0f, -1.0f);
			scene.bear->knockDown(hit);
			scene.run(FRAME);
			CHECK(scene.bear->getState() == State::CONTROL);
		}
		SECTION("It runs again (away from the rock)")
		{
			BearInput turnAround;
			turnAround.turn = 1.0f;
			scene.run(2.0f, turnAround);
			vec3 before = scene.bear->getPosition();
			scene.run(1.0f, forward());
			CHECK(glm::distance(before, scene.bear->getPosition()) > 5.0f);
		}
	}
	SECTION("A restart brings it back to the start, standing")
	{
		scene.bear->reset();
		CHECK(scene.bear->getState() == State::CONTROL);
		CHECK_THAT(scene.bear->getPosition(), IsNear(vec3(0.0f, 1.4f, 0.0f), 1e-3f));
		scene.run(0.5f);
		CHECK(scene.bear->getState() == State::CONTROL);
	}
}

TEST_CASE("The bones follow the ragdoll while the bear tumbles", "[assets][bear][ragdoll]")
{
	BearOnGround scene(true);
	scene.run(0.5f);
	scene.run(2.0f, forward());
	REQUIRE(scene.bear->getState() == State::RAGDOLL);
	scene.run(0.5f);

	const BearRig& rig = scene.bear->getRig();
	CHECK(rig.getFace().lids > 0.99f); //eyes shut
	std::vector<glm::mat4> parts;
	rig.ragdollTransforms(parts);
	//The bones with a ragdoll part are exactly where the part is (positions come from the joints, so they match closely)
	CHECK(glm::distance(vec3(parts[BearRig::PART_PELVIS][3]), rig.getBonePosition(rig.getBone("pelvis"))) < 1e-4f);
	CHECK(glm::distance(rig.getCenter(), vec3(parts[BearRig::PART_PELVIS][3])) < 0.5f);
}

TEST_CASE("A bear lying on its back rolls over before it gets up", "[assets][bear][animation]")
{
	const bibo::resources::SkinnedModel& model = bearModel();
	BearRig rig(model.skeleton);
	REQUIRE(rig.isValid());
	auto ground = [](vec3 from, vec3 to, vec3& point, vec3& normal) {
		if (from.y < 0.0f || to.y > 0.0f)
		{
			return false;
		}
		point = glm::mix(from, to, from.y / (from.y - to.y));
		normal = vec3(0.0f, 1.0f, 0.0f);
		return true;
	};
	bibo::scene::LocomotionInput input;
	rig.reset(input, ground);
	std::vector<glm::mat4> parts;
	rig.ragdollTransforms(parts);
	const float standingDuration = [&] {
		BearRig upright(model.skeleton);
		upright.reset(input, ground);
		upright.updateFromRagdoll(FRAME, parts);
		upright.beginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));
		return upright.getGetUpDuration();
	}();

	//Upside down: turned half around the length axis of the body, lying on the ground
	glm::mat4 onBack = glm::translate(glm::mat4(1.0f), vec3(0.0f, 1.0f, 0.0f)) * glm::rotate(glm::mat4(1.0f), glm::pi<float>(), vec3(0.0f, 0.0f, 1.0f))
		* glm::translate(glm::mat4(1.0f), vec3(0.0f, -1.0f, 0.0f));
	for (glm::mat4& part : parts)
	{
		part = onBack * part;
	}
	rig.updateFromRagdoll(FRAME, parts);
	BearRig::GetUpStart start = rig.beginGetUp(ground, vec3(0.0f, 0.0f, 1.0f));

	CHECK(rig.getGetUpDuration() > standingDuration + 0.5f); //the roll takes extra time
	CHECK_THAT(start.direction, IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-3f));
	CHECK_THAT(start.rootPosition.y, WithinAbs(0.0f, 1e-3f));
	bool done = false;
	for (int i = 0; i < 600 && !done; ++i)
	{
		done = rig.updateGetUp(FRAME, ground, start.rootPosition);
	}
	CHECK(done);
	//Standing upright at the end: the head is above the pelvis
	CHECK(rig.getBonePosition(rig.getBone("head")).y > rig.getBonePosition(rig.getBone("pelvis")).y + 0.3f);
}

TEST_CASE("After standing still for a while the bear plays its idle animations, moving interrupts them", "[assets][bear][animation]")
{
	BearOnGround scene;
	scene.run(5.0f);
	CHECK_FALSE(scene.bear->getRig().isIdling());
	scene.run(2.0f);
	REQUIRE(scene.bear->getRig().isIdling());
	CHECK(scene.bear->getRig().getIdle() == BearRig::IDLE_STRETCH);

	SECTION("It sits down: the pelvis is low and the chest is above it")
	{
		scene.run(1.5f);
		const BearRig& rig = scene.bear->getRig();
		CHECK(rig.getBonePosition(rig.getBone("pelvis")).y < 0.6f);
		CHECK(rig.getBonePosition(rig.getBone("chest")).y > rig.getBonePosition(rig.getBone("pelvis")).y + 0.4f);
	}
	SECTION("The stretch ends standing, the yawn comes next")
	{
		scene.run(scene.bear->getRig().getIdleDuration(BearRig::IDLE_STRETCH));
		CHECK_FALSE(scene.bear->getRig().isIdling());
		scene.run(6.5f);
		REQUIRE(scene.bear->getRig().isIdling());
		CHECK(scene.bear->getRig().getIdle() == BearRig::IDLE_YAWN);
	}
	SECTION("Moving ends it at once and the bear runs off")
	{
		scene.run(2.0f);
		vec3 before = scene.bear->getPosition();
		scene.run(FRAME, forward());
		CHECK_FALSE(scene.bear->getRig().isIdling());
		scene.run(1.0f, forward());
		CHECK(scene.bear->getPosition().z - before.z > 5.0f);
		for (int leg = 0; leg < BearRig::LEG_COUNT; ++leg)
		{
			CHECK(scene.pawError(static_cast<BearRig::Leg>(leg)) < 0.15f);
		}
	}
}

TEST_CASE("The eyes blink and look around", "[assets][bear][animation]")
{
	BearOnGround scene;
	float maxLids = 0.0f;
	float minGaze = 1.0f, maxGaze = -1.0f;
	for (int i = 0; i < 5 * 60; ++i)
	{
		scene.run(FRAME);
		const bibo::scene::FaceControls& face = scene.bear->getRig().getFace();
		maxLids = std::max(maxLids, face.lids);
		minGaze = std::min(minGaze, face.gazeYaw);
		maxGaze = std::max(maxGaze, face.gazeYaw);
	}
	CHECK(maxLids > 0.9f); //blinked
	CHECK(maxGaze - minGaze > 0.05f); //saccades
}

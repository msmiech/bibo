#pragma once
#include <limits>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "BearRig.hpp"
#include "Fur.hpp"
#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"

namespace bibo
{
	namespace scene
	{
		/**
		* What the player wants the bear to do
		*/
		struct BearInput
		{
			/**
			* Where the bear should go in world space (y is ignored), length 0 to 1 (how fast): the bear turns towards it and runs.
			* Used instead of forward and turn if it is not zero (e.g. the stick of a gamepad relative to the camera).
			*/
			glm::vec3 move = glm::vec3(0.0f);
			float forward = 0.0f; //relative to the bear: 1 forward, -1 backward
			float turn = 0.0f; //1: left, -1: right
			bool jump = false; //held: the longer, the higher the jump (a new jump needs a new press)
		};

		/**
		* The player character without graphics: the character controller, the animation (BearRig), the ragdoll and the fur
		* dynamics. After a knock down (e.g. running into an obstacle) the bear tumbles as a ragdoll until it lies still,
		* then it gets up and the player controls it again.
		* A fish that the bear catches stays in its mouth until it stands on the ground, then the bear sits down and eats it
		* (the player can only move it again after the last bite).
		* In deep water the bear swims: it floats with its head and back out of the water and paddles (slower than it runs),
		* a jump at the surface helps it out onto the shore.
		*/
		class BearCharacter
		{
		public:
			enum class State
			{
				CONTROL, //the player moves the bear
				RAGDOLL, //tumbling after a hit
				GETTING_UP
			};
			enum class Fish
			{
				NONE,
				CARRIED, //in the mouth (running, jumping)
				EATING
			};
			/**
			* Where the fish is that the bear holds (in the frame of the fish model: +z towards its head)
			*/
			struct HeldFish
			{
				glm::vec3 center = glm::vec3(0.0f);
				glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				int bites = 0; //taken so far (3: eaten)
				float wiggle = 0.0f; //how far the tail is turned (radians)
			};

			/**
			* @param model Must stay alive as long as the character
			* @param startPosition Center of the character capsule
			* @param onHit Called when the bear touches a body (e.g. to knock it down with knockDown)
			*/
			BearCharacter(physics::PhysicsWorld& world, const resources::SkinnedModel& model, glm::vec3 startPosition, glm::vec3 startDirection,
				physics::CharacterController::HitCallback onHit);
			~BearCharacter();
			BearCharacter(const BearCharacter&) = delete;
			BearCharacter& operator=(const BearCharacter&) = delete;

			/**
			* Shape and movement limits of the bear's physics capsule
			*/
			static physics::CharacterControllerDesc characterControllerDesc(glm::vec3 position);

			//Running speed (m/s)
			static constexpr float FORWARD_SPEED = 10.0f;
			//Jump: how high the bear gets when the button is held until the highest point, and how long it rises (a shorter press jumps lower)
			static constexpr float JUMP_HEIGHT = 3.0f;
			static constexpr float JUMP_RISE_TIME = 0.4f;
			//Swimming speed (m/s)
			static constexpr float SWIM_SPEED = 4.0f;
			//The bear swims when the ground under it is this deep under the water surface (it stops in shallower water),
			//it floats with its paws this deep in the water
			static constexpr float SWIM_DEPTH = 0.75f, WADE_DEPTH = 0.55f, FLOAT_DEPTH = 0.85f;

			/**
			* Call after the physics world was updated
			*/
			void update(float timeDelta, const BearInput& input);
			/**
			* The bear falls over and tumbles (at the end of the next update). Ignored while it is down or getting up,
			* and for a moment after it got up.
			*/
			void knockDown(const physics::CharacterController::Hit& hit);
			/**
			* Back to the start, standing (e.g. when the game is restarted)
			*/
			void reset();
			/**
			* Where reset puts the bear
			* @param position Center of the character capsule
			*/
			void setStart(glm::vec3 position, glm::vec3 direction);
			glm::vec3 getStartPosition() const { return startPosition; }
			/**
			* From the center of the character capsule down to the ground it stands on
			*/
			static float groundOffset();
			/**
			* Puts the bear somewhere else, standing and looking in a direction (e.g. at the start of a race)
			* @param position Center of the character capsule
			*/
			void placeAt(glm::vec3 position, glm::vec3 direction);
			/**
			* Height of the water surface (the sea and the pools of the island), the bear swims where it is deep enough.
			* Without water (the default) it never swims.
			*/
			void setWaterLevel(float level) { waterLevel = level; }
			float getWaterLevel() const { return waterLevel; }
			/**
			* The bear looks at a point with its head and eyes (e.g. the face of the animal it talks to); it does not start
			* idle animations meanwhile (it stops stretching or yawning)
			*/
			void lookAt(glm::vec3 target) { rig.lookAt(target); }
			void stopLooking() { rig.stopLooking(); }
			/**
			* Between the eyes (world space)
			*/
			glm::vec3 getHeadPosition() const { return rig.getHeadPosition(); }
			/**
			* Plays an idle animation now (they also start on their own when the player does not move the bear for a while)
			*/
			void startIdle(BearRig::Idle idle);
			/**
			* @return true if the bear can catch a fish now (it is not tumbling and does not hold one already)
			*/
			bool canCatchFish() const { return state == State::CONTROL && fish == Fish::NONE; }
			/**
			* The bear caught a fish: it holds it in its mouth and eats it as soon as it stands on the ground
			* @return false if it cannot catch one now (see canCatchFish)
			*/
			bool catchFish();
			Fish getFish() const { return fish; }
			/**
			* @return How many fish the bear ate up since the last call (it took the last bite; a fish it dropped does not count)
			*/
			int takeEatenFish();
			/**
			* @return false if the bear holds no fish
			*/
			bool getHeldFish(HeldFish& held) const;
			/**
			* Between the front teeth (world space)
			*/
			glm::vec3 getMouthPosition() const;

			State getState() const { return state; }
			/**
			* Center of the character capsule, or where it would be while the bear tumbles (smooth when the state changes)
			*/
			glm::vec3 getPosition() const;
			glm::vec3 getDirection() const { return direction; }
			/**
			* How fast the bear moves (m/s), zero while it tumbles or gets up
			*/
			glm::vec3 getVelocity() const { return state == State::CONTROL ? velocity : glm::vec3(0.0f); }
			/**
			* @return true while the bear is in the air (jumping or falling)
			*/
			bool isAirborne() const { return state == State::CONTROL && !standingOnGround && !swimming; }
			/**
			* @return true while the bear swims (floats in deep water)
			*/
			bool isSwimming() const { return state == State::CONTROL && swimming; }
			/**
			* @return How deep the paws are in the water (negative: above the water surface), also while the bear tumbles
			*/
			float getWaterDepth() const;
			/**
			* @return How often the bear went into deep water since the last call (e.g. for a splash)
			*/
			int takeSplashes();
			/**
			* @return true for a moment after getting up, when hits do not knock the bear down
			*/
			bool isProtected() const { return protectedTime > 0.0f; }
			/**
			* Obstacles that the fur lies on (e.g. rocks)
			*/
			void setObstacles(const std::vector<fur::SphereCollider>& obstacles) { this->obstacles = obstacles; }

			const BearRig& getRig() const { return rig; }
			const resources::SkinnedModel& getModel() const { return model; }
			//Skinned vertices of the model in world space (updated every frame)
			const std::vector<glm::vec3>& getPositions() const { return positions; }
			const std::vector<glm::vec3>& getNormals() const { return normals; }
			const std::vector<glm::vec3>& getCombDirections() const { return combDirections; }
			const std::vector<glm::vec3>& getFurOffsets() const { return guideHairs.getOffsets(); }
			/**
			* Sphere around the whole bear (with its fur)
			*/
			glm::vec3 getBoundsCenter() const { return rig.getCenter(); }
			float getBoundsRadius() const { return 2.2f; }
			/**
			* Counts the frames in which the skinned vertices changed
			*/
			unsigned int getVersion() const { return version; }

		private:
			glm::vec3 rawPosition() const;
			void setState(State newState);
			void updateControl(float timeDelta, const BearInput& input);
			/**
			* @return How deep the paws of the character capsule are in the water (negative: above the water surface)
			*/
			float capsuleWaterDepth() const;
			/**
			* Turns the bear and changes its speed (on the ground)
			* @return The speed the bear should run at (m/s)
			*/
			float steer(float timeDelta, const BearInput& input, bool& hasInput);
			void updateRagdoll(float timeDelta);
			void updateGettingUp(float timeDelta);
			void startRagdoll();
			void startGettingUp();
			void updateSkin(float timeDelta);
			LocomotionInput locomotionInput(bool jumped = false) const;
			HeldFish carriedFish() const;

			physics::PhysicsWorld& world;
			const resources::SkinnedModel& model;
			glm::vec3 startPosition;
			glm::vec3 startDirection;
			physics::CharacterController::HitCallback onHit;
			std::unique_ptr<physics::CharacterController> controller;
			BearRig rig;
			std::unique_ptr<physics::Ragdoll> ragdoll;
			GroundProbe probe;

			State state = State::CONTROL;
			glm::vec3 direction;
			float speed = 0.0f; //along the direction
			float yawRate = 0.0f;
			glm::vec3 velocity = glm::vec3(0.0f);
			//Jumping and falling
			glm::vec3 airVelocity = glm::vec3(0.0f); //horizontal velocity in the air (the bear keeps its momentum)
			float verticalSpeed = 0.0f;
			float airTime = 0.0f; //since the bear left the ground
			float jumpBuffer = 0.0f; //jump was pressed this long ago (it still jumps if it lands soon)
			bool jumping = false; //rising from a jump (not falling off an edge)
			bool jumpWasHeld = false;
			float fallingTime = 0.0f; //getting up
			bool standingOnGround = false;
			//Swimming
			float waterLevel = std::numeric_limits<float>::lowest();
			bool swimming = false;
			int splashes = 0;
			bool knockDownPending = false;
			physics::CharacterController::Hit pendingHit;
			float stateTime = 0.0f, restTime = 0.0f, protectedTime = 0.0f;
			float idleWaitTime = 0.0f; //standing without input
			int nextIdle = BearRig::IDLE_STRETCH;
			glm::vec3 positionOffset = glm::vec3(0.0f); //smooths getPosition when the state changes
			Fish fish = Fish::NONE;
			bool lastBiteTaken = false;
			int eatenFish = 0;
			int tongueRoot, tongueTip, leftPaw, rightPaw; //bones that hold the fish

			//Skinning and fur
			std::vector<glm::mat4> skinMatrices;
			std::vector<glm::vec3> positions, normals, combDirections, bindCombDirections;
			std::vector<float> furLengths;
			fur::GuideHairs guideHairs;
			std::vector<fur::SphereCollider> obstacles;
			float time = 0.0f;
			unsigned int version = 0;
		};
	}
}

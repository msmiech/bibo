#pragma once
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
			float forward = 0.0f; //1: forward, -1: backward
			float turn = 0.0f; //1: left, -1: right
			bool jump = false;
		};

		/**
		* The player character without graphics: the character controller, the animation (BearRig), the ragdoll and the fur
		* dynamics. After a knock down (e.g. running into a rock) the bear tumbles as a ragdoll until it lies still,
		* then it gets up and the player controls it again.
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
			* Plays an idle animation now (they also start on their own when the player does not move the bear for a while)
			*/
			void startIdle(BearRig::Idle idle);

			State getState() const { return state; }
			/**
			* Center of the character capsule, or where it would be while the bear tumbles (smooth when the state changes)
			*/
			glm::vec3 getPosition() const;
			glm::vec3 getDirection() const { return direction; }
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
			void updateRagdoll(float timeDelta);
			void updateGettingUp(float timeDelta);
			void startRagdoll();
			void startGettingUp();
			void updateSkin(float timeDelta);
			LocomotionInput locomotionInput() const;

			physics::PhysicsWorld& world;
			const resources::SkinnedModel& model;
			const glm::vec3 startPosition;
			const glm::vec3 startDirection;
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
			float jumpTimeLeft = 0.0f, fallingTime = 0.0f;
			bool standingOnGround = false;
			bool knockDownPending = false;
			physics::CharacterController::Hit pendingHit;
			float stateTime = 0.0f, restTime = 0.0f, protectedTime = 0.0f;
			float idleWaitTime = 0.0f; //standing without input
			int nextIdle = BearRig::IDLE_STRETCH;
			glm::vec3 positionOffset = glm::vec3(0.0f); //smooths getPosition when the state changes

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

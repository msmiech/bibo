#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bibo
{
	namespace world
	{
		class WorldGenerator;
	}

	namespace scene
	{
		/**
		* A large hedgehog that strolls around its home on the track: it walks to a spot nearby, sniffs around for a while
		* and walks on. The bear is knocked over when it runs into one (an obstacle). Only the behavior, the scene draws it
		* (HedgehogRenderer) and moves its collision sphere.
		*/
		class Hedgehog
		{
		public:
			/**
			* Can a hedgehog stand at a point of the ground plane?
			* @param height The height of the ground there
			*/
			using Ground = std::function<bool(glm::vec2 position, float& height)>;
			/**
			* The floor of the race track (not too close to its walls, not in the water)
			* @param generator Must stay alive as long as the function is used
			*/
			static Ground trackFloor(const world::WorldGenerator& generator);
			/**
			* How the hedgehogs of the track start (every one looks somewhere else and does something else)
			*/
			static glm::vec2 startDirection(int index) { return glm::vec2(std::sin(2.4f * index), std::cos(2.4f * index)); }
			static std::uint32_t seedOf(int index) { return 1000u + static_cast<std::uint32_t>(index); }

			//Size of the model (HedgehogModel) in the world
			static constexpr float SCALE = 2.3f;
			static constexpr float WALK_SPEED = 1.1f;
			//How far it goes from its home
			static constexpr float HOME_RADIUS = 4.5f;

			/**
			* @param home Where it lives (it starts there, looking along startDirection)
			*/
			Hedgehog(glm::vec2 home, glm::vec2 startDirection, std::uint32_t seed, Ground ground);

			void update(float timeDelta);
			/**
			* Back home, as at the start
			*/
			void reset();
			/**
			* The bear ran into it: it hops up in surprise
			*/
			void startle();

			/**
			* On the ground under the middle of the hedgehog
			*/
			glm::vec3 getPosition() const { return position; }
			glm::vec2 getHome() const { return home; }
			/**
			* Its collision sphere (around the round body, without the spines)
			*/
			glm::vec3 getColliderCenter() const;
			float getColliderRadius() const;
			bool isWalking() const { return walking; }

			/**
			* What is drawn: the body and the feet
			*/
			struct Pose
			{
				glm::vec3 bodyPosition; //origin of the model
				glm::quat bodyRotation;
				std::array<glm::vec3, 4> feet; //front left, front right, hind left, hind right (origins of the foot models)
				glm::quat footRotation;
				float bristle = 0.0f; //0 to 1: the spines stand up (startled)
			};
			Pose getPose() const;

		private:
			void chooseTarget();
			float uniform(float from, float to);

			glm::vec2 home, startHeading;
			Ground ground;
			std::uint32_t seed;
			std::mt19937 random;

			glm::vec3 position;
			float yaw = 0.0f; //0: looking along +z
			glm::vec2 target;
			bool walking = false;
			float waitTime = 0.0f; //sniffing before it walks on
			float gaitPhase = 0.0f; //steps (1 per stride)
			float stride = 0.0f; //0 standing, 1 walking (blends the steps in and out)
			float time = 0.0f;
			float hopTime = -1.0f; //since it was startled, -1 if it is not hopping
		};
	}
}

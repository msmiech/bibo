#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bibo
{
	namespace physics
	{
		/**
		* Identifies a rigid body inside a PhysicsWorld.
		* Wraps a Jolt BodyID so that the Jolt headers stay out of the game code.
		*/
		using BodyHandle = std::uint32_t;
		const BodyHandle INVALID_BODY = 0xffffffff;

		/**
		* Physics simulation based on Jolt Physics (https://github.com/jrouwe/JoltPhysics).
		* Replaces PxFoundation, PxPhysics, PxCooking and PxScene of the former Nvidia PhysX 3.3 implementation.
		*/
		class PhysicsWorld
		{
		public:
			PhysicsWorld();
			~PhysicsWorld();
			PhysicsWorld(const PhysicsWorld&) = delete;
			PhysicsWorld& operator=(const PhysicsWorld&) = delete;

			void update(float time_delta);

			BodyHandle createStaticBox(glm::vec3 position, glm::vec3 halfExtent);
			BodyHandle createDynamicBox(glm::vec3 position, glm::vec3 halfExtent, float density);
			BodyHandle createStaticSphere(glm::vec3 position, float radius);
			/**
			* Creates a static triangle mesh. The vertices are scaled first, then rotated and translated.
			* @param vertices 3 floats per vertex
			* @param indices 3 indices per triangle
			*/
			BodyHandle createStaticMesh(const float* vertices, unsigned int numVertices, const unsigned int* indices, unsigned int numTriangles,
				float scale, glm::vec3 position, glm::quat rotation);
			/**
			* Should be called once after all static bodies were added (speeds up collision detection)
			*/
			void optimizeBroadPhase();

			/**
			* @return Current position and rotation of the body as model matrix
			*/
			glm::mat4 getTransform(BodyHandle body) const;
			/**
			* Moves a dynamic body to the given position and rotation and stops it (e.g. when the game is restarted)
			*/
			void resetBody(BodyHandle body, glm::vec3 position, glm::quat rotation);

		private:
			friend class CharacterController;
			struct Impl;
			std::unique_ptr<Impl> impl;
		};

		struct CharacterControllerDesc
		{
			glm::vec3 position = glm::vec3(0.0f); //center of the capsule
			float radius = 0.5f; //radius of the capsule
			float height = 1.0f; //height of the cylinder between the two hemispheres of the capsule
			float stepOffset = 0.4f; //max obstacle height the character can climb
			float slopeLimit = 0.785f; //max slope the character can walk (radians)
			float contactOffset = 0.02f; //skin around the capsule within which contacts are generated
		};

		/**
		* Kinematic capsule character that is moved by the game logic and collides with the physics world.
		* Replaces the PxCapsuleController of the former PhysX implementation.
		*/
		class CharacterController
		{
		public:
			/**
			* Called whenever the character starts touching a body
			*/
			using HitCallback = std::function<void(BodyHandle)>;

			CharacterController(PhysicsWorld& world, const CharacterControllerDesc& desc, HitCallback onHit);
			~CharacterController();
			CharacterController(const CharacterController&) = delete;
			CharacterController& operator=(const CharacterController&) = delete;

			/**
			* Moves the character by the given displacement while colliding with the world (like PxController::move)
			* @return true if the character stands on the ground after the move
			*/
			bool move(glm::vec3 displacement, float time_delta);
			glm::vec3 getPosition() const;
			/**
			* Moves the character to the given position without colliding with the world on the way (e.g. when the game is restarted)
			*/
			void setPosition(glm::vec3 position);

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};
	}
}

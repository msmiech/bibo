#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
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
		* Where a ray hits the first body
		*/
		struct RayHit
		{
			BodyHandle body = INVALID_BODY;
			glm::vec3 position = glm::vec3(0.0f);
			glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f); //surface normal of the body at the hit
			float fraction = 1.0f; //position = origin + fraction * ray
		};

		/**
		* Collision shape of a static body, built before the body is added to the world (e.g. by a worker thread while the
		* world is streamed in). The vertices of the shapes are in world space, the body is placed at the origin.
		*/
		class CollisionShape
		{
		public:
			CollisionShape();
			~CollisionShape();
			CollisionShape(const CollisionShape&) = delete;
			CollisionShape& operator=(const CollisionShape&) = delete;
			bool isValid() const;
		private:
			friend class PhysicsWorld;
			struct Impl;
			std::unique_ptr<Impl> impl;
		};

		/**
		* A capsule or sphere of a compound collision shape (e.g. the trunks of the trees in a part of the world)
		*/
		struct ShapePart
		{
			enum Type { CAPSULE, SPHERE };
			Type type = SPHERE;
			glm::vec3 position = glm::vec3(0.0f); //center
			glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //the capsule goes along the y axis
			float halfHeight = 0.0f; //capsule: half the length of its cylinder
			float radius = 0.5f;
		};

		/**
		* Physics simulation based on Jolt Physics (https://github.com/jrouwe/JoltPhysics).
		* Replaces PxFoundation, PxPhysics, PxCooking and PxScene of the former Nvidia PhysX 3.3 implementation.
		*/
		class PhysicsWorld
		{
		public:
			/**
			* Triangle mesh for a static body (thread safe, but a PhysicsWorld must exist)
			* @param indices 3 per triangle
			* @return nullptr if the mesh has no triangles
			*/
			static std::shared_ptr<CollisionShape> buildMeshShape(const std::vector<glm::vec3>& vertices, const std::vector<std::uint32_t>& indices);
			/**
			* Capsules and spheres as one shape for a static body (thread safe, but a PhysicsWorld must exist)
			* @return nullptr if there are no parts
			*/
			static std::shared_ptr<CollisionShape> buildCompoundShape(const std::vector<ShapePart>& parts);

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
			* Adds a static body with a shape built before (at the origin, the shape is in world space)
			*/
			BodyHandle addStaticBody(const CollisionShape& shape);
			/**
			* Removes the body from the world and deletes it
			*/
			void removeBody(BodyHandle body);
			/**
			* @return Number of bodies in the world
			*/
			std::size_t getBodyCount() const;
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
			/**
			* Finds the first body on the way from origin to origin + ray (e.g. the ground under the feet of the bear)
			* @param staticOnly Only static bodies (terrain, rocks), no dynamic bodies (boxes, ragdolls)
			* @return false if the ray does not hit a body
			*/
			bool castRay(glm::vec3 origin, glm::vec3 ray, RayHit& hit, bool staticOnly = false) const;
			/**
			* Moves a sphere from origin to origin + ray and finds the first body it touches (e.g. so that the camera does not go into the terrain)
			* @param hit position: where the sphere touches the body, fraction: how far the sphere got (0 if it touches a body at the origin)
			* @param staticOnly Only static bodies (terrain, rocks), no dynamic bodies (boxes, ragdolls)
			* @return false if the sphere does not touch a body on the way
			*/
			bool castSphere(glm::vec3 origin, glm::vec3 ray, float radius, RayHit& hit, bool staticOnly = false) const;

		private:
			friend class CharacterController;
			friend class Ragdoll;
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
			* Contact of the character with a body
			*/
			struct Hit
			{
				BodyHandle body = INVALID_BODY;
				glm::vec3 position = glm::vec3(0.0f); //where the character touches the body
				glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f); //contact normal, pointing from the body towards the character
			};
			/**
			* Called whenever the character starts touching a body
			*/
			using HitCallback = std::function<void(const Hit&)>;

			CharacterController(PhysicsWorld& world, const CharacterControllerDesc& desc, HitCallback onHit);
			~CharacterController();
			CharacterController(const CharacterController&) = delete;
			CharacterController& operator=(const CharacterController&) = delete;

			/**
			* Moves the character by the given displacement while colliding with the world (like PxController::move)
			* @return true if the character stands on the ground after the move
			*/
			bool move(glm::vec3 displacement, float time_delta);
			/**
			* @return true if the character stands on ground that is not too steep (see CharacterControllerDesc::slopeLimit),
			* false in the air or on a slope that it slides down
			*/
			bool isOnWalkableGround() const;
			glm::vec3 getPosition() const;
			/**
			* Moves the character to the given position without colliding with the world on the way (e.g. when the game is restarted)
			*/
			void setPosition(glm::vec3 position);

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};

		/**
		* Joint between a ragdoll part and its parent part, in the space of the bind transforms of the parts
		*/
		struct RagdollJoint
		{
			enum Type
			{
				SWING_TWIST, //ball joint with a cone limit (hips, shoulders, spine, neck)
				HINGE //rotates only around one axis (knees, elbows)
			};
			Type type = SWING_TWIST;
			glm::vec3 position = glm::vec3(0.0f);
			glm::vec3 twistAxis = glm::vec3(1.0f, 0.0f, 0.0f); //swing twist: along the child part; hinge: rotation axis
			glm::vec3 planeAxis = glm::vec3(0.0f, 1.0f, 0.0f); //perpendicular to the twist axis
			float swingAngle = 0.5f; //swing twist: half angle of the cone (radians)
			float twistAngle = 0.3f; //swing twist: twist limit in both directions
			float minAngle = -1.0f, maxAngle = 1.0f; //hinge: limits around the axis
			float friction = 1.0f; //torque that slows down the joint when it moves (N m)
		};

		/**
		* Rigid body of a ragdoll: a capsule that moves like a bone of a skeleton
		*/
		struct RagdollPart
		{
			int parent = -1; //index of the parent part, -1 for the root part (parents come before their children)
			glm::mat4 bindTransform = glm::mat4(1.0f); //transform of the part (its bone) when the joints are relaxed
			glm::vec3 capsuleStart = glm::vec3(0.0f), capsuleEnd = glm::vec3(0.0f, 1.0f, 0.0f); //in the same space as bindTransform
			float radius = 0.1f;
			float mass = 1.0f;
			RagdollJoint joint; //to the parent part
		};

		/**
		* Bodies connected by joints that fall and tumble like a limp body (e.g. the bear after it ran into a rock).
		* The bodies are only part of the physics world while the ragdoll is active.
		*/
		class Ragdoll
		{
		public:
			Ragdoll(PhysicsWorld& world, const std::vector<RagdollPart>& parts);
			~Ragdoll();
			Ragdoll(const Ragdoll&) = delete;
			Ragdoll& operator=(const Ragdoll&) = delete;

			/**
			* Adds the bodies to the physics world
			* @param transforms World transform of every part (rotation and translation)
			* @param velocities Linear velocity of every part
			* @param angularVelocity Spin of the whole ragdoll
			*/
			void activate(const std::vector<glm::mat4>& transforms, const std::vector<glm::vec3>& velocities, glm::vec3 angularVelocity);
			/**
			* Removes the bodies from the physics world
			*/
			void deactivate();
			bool isActive() const;
			std::size_t getPartCount() const;
			/**
			* @param transforms Current world transform of every part
			*/
			void getTransforms(std::vector<glm::mat4>& transforms) const;
			/**
			* @return The highest linear speed of all parts (m/s), for detecting when the ragdoll comes to rest
			*/
			float getMaxLinearSpeed() const;
			/**
			* @return The highest angular speed of all parts (rad/s)
			*/
			float getMaxAngularSpeed() const;

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};
	}
}

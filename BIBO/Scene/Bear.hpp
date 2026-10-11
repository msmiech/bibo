#pragma once
#include "Geometry.hpp"
#include "../Physics/Physics.hpp"
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bibo
{
	namespace scene
	{
		class Scene;

		class Bear : public Geometry
		{
		public:
			Bear(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, Texture* texture, physics::PhysicsWorld* physicsWorld, physics::CharacterController::HitCallback onHit);
			virtual ~Bear();

			void update(float time_delta) override;
			void draw(Shader* shader) override;
			vec3 getDirection() override;
			bool isVisible(const ViewFrustum& frustum) override;

			/**
			* Moves the bear back to its start position and direction (e.g. when the game is restarted)
			*/
			void reset();
			/**
			* Shape and movement limits of the bear's physics capsule
			*/
			static physics::CharacterControllerDesc characterControllerDesc(vec3 position);
		private:
			mat4 initialTransform;
			vec3 direction;

			unique_ptr<physics::CharacterController> characterController;

			Geometry leftHand;
			vec3 leftHandOffSet;
			Geometry rightHand;
			vec3 rightHandOffSet;
			Geometry leftLeg;
			vec3 leftLegOffSet;
			Geometry rightLeg;
			vec3 rightLegOffSet;

		};
	}
}

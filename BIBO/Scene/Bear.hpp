#pragma once
#include "Geometry.hpp"
#include "BearCharacter.hpp"
#include "BearRenderer.hpp"
#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace bibo
{
	namespace scene
	{
		/**
		* The player character: a young brown bear (Model/BearCub/BearCub.glb) with a skeleton, procedural animation,
		* fur strands and a ragdoll (see BearCharacter, BearRig, BearRenderer). The scene sets what the player wants it to do (setInput).
		*/
		class Bear : public Geometry
		{
		public:
			static const char* const MODEL_FILE;
			static constexpr std::size_t STRAND_COUNT = 60000;

			/**
			* @param assimpScene The track: the bear starts where its node BearBody is
			* @param onHit Called when the bear touches a body (e.g. to knock it down)
			*/
			Bear(GLFWwindow& window, const aiScene* assimpScene, physics::PhysicsWorld* physicsWorld, physics::CharacterController::HitCallback onHit);
			~Bear() override;

			void update(float time_delta) override;
			/**
			* What the player wants the bear to do in the next updates
			*/
			void setInput(const BearInput& input) { this->input = input; }
			/**
			* Draws the surface of the bear with the given shader (e.g. into the shadow map)
			*/
			void draw(Shader* shader) override;
			/**
			* Draws the bear with its fur (in the main pass)
			*/
			void drawShaded(const BearRenderContext& context);
			vec3 getPosition() override;
			vec3 getDirection() override;
			bool isVisible(const ViewFrustum& frustum) override;

			/**
			* Moves the bear back to its start position and direction (e.g. when the game is restarted)
			*/
			void reset();
			/**
			* The bear tumbles as a ragdoll, then gets up again (e.g. after running into a rock)
			*/
			void knockDown(const physics::CharacterController::Hit& hit);
			/**
			* @return true while the bear tumbles or gets up (the player cannot control it)
			*/
			bool isKnockedDown() const;
			/**
			* Obstacles that the fur lies on
			*/
			void setObstacles(const std::vector<fur::SphereCollider>& obstacles);
			BearCharacter& getCharacter() { return *character; }
			/**
			* Shape and movement limits of the bear's physics capsule
			*/
			static physics::CharacterControllerDesc characterControllerDesc(vec3 position);

		private:
			void uploadIfChanged();

			std::unique_ptr<resources::SkinnedModel> model;
			std::unique_ptr<BearCharacter> character;
			std::unique_ptr<BearRenderer> renderer;
			unsigned int uploadedVersion = 0;
			BearInput input;
		};
	}
}

#pragma once
#include "Geometry.hpp"
#ifndef GLM_INCLUDED
#define GLM_INCLUDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#endif
#ifndef PX_INCLUDED
#define PX_INCLUDED
#include "PhysX/PxPhysicsAPI.h"
#include <PhysX/PxRigidActor.h>
#include <PhysX/extensions/PxDefaultCpuDispatcher.h>
#include <PhysX/PxVisualizationParameter.h>
#include <PhysX/characterkinematic/PxController.h>
#include <PhysX/foundation/PxVec3.h>
#include <PhysX/characterkinematic/PxExtended.h>
#endif

using namespace physx;

namespace bibo
{
	namespace scene
	{
		class Scene;

		class Bear : public Geometry
		{
		public:
			Bear(mat4 modelMatrix, Shader* shader, GLFWwindow& window, Texture* texture, PxPhysics* mPhysics, PxControllerManager* manager, PxUserControllerHitReport* scene);
			virtual ~Bear();

			void update(float time_delta) override;
			void draw(Shader* shader) override;
			vec3 getDirection() override;

			void reset();
		private:
			mat4 initialTransform;
			vec3 direction;
			
			PxPhysics* mPhysics;
			PxController* pxController;
			PxControllerFilters* pxControllerFilters;
			PxRigidDynamic* pxActor;

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

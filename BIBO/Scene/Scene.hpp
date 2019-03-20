#pragma once
#include <vector>
#include "SceneObject.hpp"
#include "Geometry.hpp"
#include "Light.hpp"
#include <memory>
#include "Bear.hpp"
#include "Cube.hpp"
#include <filesystem>
#include "../Util/Util.hpp"
#include <memory>
#ifndef PX_INCLUDED
#define PX_INCLUDED
#include "PhysX/PxPhysicsAPI.h"
#include "PhysX/PxRigidActor.h"
#include "PhysX/extensions/PxDefaultCpuDispatcher.h"
#include "PhysX/PxVisualizationParameter.h"
#endif


using namespace std;
using namespace physx;

namespace bibo
{
	namespace scene
	{
		class Scene : public PxUserControllerHitReport
		{
			void initPxScene();
			void initPxActors();
			void initAnimatedTextures();

			GLFWwindow* window;
			vector<unique_ptr<Geometry>> geometries;
			vector<unique_ptr<Light>> lights;
			unique_ptr<Camera> camera;
			vector<unique_ptr<Texture>> textures;
			vector<unique_ptr<Texture>> crashVideo;
			vector<unique_ptr<Texture>> waterVideo;
			vector<unique_ptr<Geometry>> arrows;

			//special pointers
			Bear* bear;
			Geometry* world;
			Geometry* finishLine;
			Geometry* monitor1;
			Geometry* water;
			unique_ptr<Cube> debugCube;

			//PX stuff
			PxControllerManager* pxManager;
			PxFoundation* mFoundation;
			PxPhysics* mPhysics;
			PxScene* pxScene;
			PxCooking* mCooking;

			bool shadowMappingEnabled = true;
			bool normalMappingEnabled = true;
			bool transparencyEnabled = true;

			int userScore = 0;
			bool gameOver = false;
			bool gameWon = false;

			vector<PxActor*> actorsFish;
			vector<PxActor*> actorsRock;

			float accumulatedVideoTime = 0;
			int crashFrame = 0;
			int waterFrame = 0;

		public:
			Scene(GLFWwindow* window, Shader* shader, PxPhysics* mPhysx, PxFoundation* mFoundation, PxCooking* mCooking);
			~Scene();

			void update(float time_delta);
			void draw(Shader* shader, mat4 view, mat4 projection);

			void renderFromCamera(Shader* shader);
			void renderFromLightSource(Shader* shader);
			void renderLightSourcesOnly(Shader* shader);
			void setTransparencyEnabled(bool transparency);
			void setNormalMappingEnabled(bool normalMappingEnabled);
			void setShadowMappingEnabled(bool shadowMappingEnabled);
			bool isTransparencyEnabled();
			bool isNormalMappingEnabled();
			bool isShadowMappingEnabled();
			int getUserScore();
			bool isGameOver();
			bool isGameWon();

			//PxUserControllerHitReport implementation
			void onShapeHit(const PxControllerShapeHit& hit) override;
			void onControllerHit(const PxControllersHit& hit) override;
			void onObstacleHit(const PxControllerObstacleHit& hit) override;
		};
	}
}

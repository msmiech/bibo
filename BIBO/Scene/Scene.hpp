#pragma once
#include <vector>
#include "SceneObject.hpp"
#include "Geometry.hpp"
#include "Light.hpp"
#include <memory>
#include "Bear.hpp"
#include "Cube.hpp"
#include "../Util/Util.hpp"
#include "../Physics/Physics.hpp"


using namespace std;

namespace bibo
{
	namespace scene
	{
		class Scene
		{
			void initPhysicsBodies();
			void initAnimatedTextures();
			//Called when the bear touches a physics body
			void onBearHit(physics::BodyHandle body);

			GLFWwindow* window;
			vector<unique_ptr<Geometry>> geometries;
			vector<unique_ptr<Light>> lights;
			unique_ptr<Camera> camera;
			vector<unique_ptr<Texture>> textures;
			vector<unique_ptr<Texture>> monitorVideo;
			vector<unique_ptr<Texture>> waterVideo;
			vector<unique_ptr<Geometry>> arrows;

			//special pointers
			Bear* bear;
			Geometry* world;
			Geometry* finishLine;
			Geometry* monitor1;
			Geometry* water;
			unique_ptr<Cube> debugCube;

			//Physics
			physics::PhysicsWorld* physicsWorld;

			bool shadowMappingEnabled = true;
			bool normalMappingEnabled = true;
			bool transparencyEnabled = true;

			int userScore = 0;
			bool gameOver = false;
			bool gameWon = false;

			vector<physics::BodyHandle> bodiesFish;
			vector<physics::BodyHandle> bodiesRock;

			float accumulatedVideoTime = 0;
			int monitorFrame = 0;
			int waterFrame = 0;

		public:
			Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld);
			~Scene();

			void update(float time_delta);
			void draw(Shader* shader, mat4 view, mat4 projection);

			void renderFromCamera(Shader* shader);
			void renderFromLightSource(Shader* shader);
			void renderLightSourcesOnly(Shader* shader);
			/**
			* Size of the rendered image in pixels (e.g. after the window was resized)
			*/
			void setViewportSize(int width, int height);
			void setTransparencyEnabled(bool transparency);
			void setNormalMappingEnabled(bool normalMappingEnabled);
			void setShadowMappingEnabled(bool shadowMappingEnabled);
			bool isTransparencyEnabled();
			bool isNormalMappingEnabled();
			bool isShadowMappingEnabled();
			int getUserScore();
			bool isGameOver();
			bool isGameWon();
		};
	}
}

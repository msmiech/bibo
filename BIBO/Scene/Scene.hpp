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
			void initFinishLine();
			//Called when the bear touches a physics body
			void onBearHit(physics::BodyHandle body);
			//Counts the checkpoints and the finish line passed by the bear
			void updateLap(vec3 playerPosition);
			bool isOnFinishLine(vec3 position);
			void drawGeometry(Shader* shader, Geometry* geometry, GLuint normalMappingEnabledLocation);

			GLFWwindow* window;
			vector<unique_ptr<Geometry>> geometries;
			vector<unique_ptr<Light>> lights;
			unique_ptr<Camera> camera;
			vector<unique_ptr<Texture>> textures;
			vector<unique_ptr<Texture>> monitorVideo;
			vector<unique_ptr<Texture>> waterVideo;
			vector<unique_ptr<Geometry>> arrows;
			unique_ptr<Geometry> rotatingRock;

			//special pointers
			Bear* bear;
			Geometry* world;
			Geometry* finishLine;
			Geometry* monitor1;
			Geometry* water; //transparent, drawn after the other geometries
			unique_ptr<Cube> debugCube;

			//Physics
			physics::PhysicsWorld* physicsWorld;

			bool shadowMappingEnabled = true;
			bool normalMappingEnabled = true;
			bool transparencyEnabled = true;

			int userScore = 0;
			bool gameOver = false;
			bool gameWon = false;

			//A lap is complete when the bear reaches the finish line after it passed all checkpoints in their order
			size_t nextCheckpoint = 0;
			//Invisible trigger at the finish line (in the xz plane, the bear starts on it)
			vec3 finishLineCenter;
			vec3 finishLineAcross; //along the finish line, across the track
			float finishLineHalfWidth = 0.0f;

			vector<physics::BodyHandle> bodiesFish;
			vector<physics::BodyHandle> bodiesRock;
			//Dynamic bodies and their start positions, which they are moved back to by restart()
			vector<pair<physics::BodyHandle, vec3>> dynamicBodies;

			float accumulatedVideoTime = 0;
			int monitorFrame = 0;
			int waterFrame = 0;

		public:
			Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld);
			~Scene();

			void update(float time_delta);
			void draw(Shader* shader, mat4 view, mat4 projection);
			/**
			* Starts the run again: the bear, the movable objects and the lap go back to the start (R key, game over menu)
			*/
			void restart();

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
			int getCheckpointsPassed();
			int getCheckpointCount();
			bool isGameOver();
			bool isGameWon();
		};
	}
}

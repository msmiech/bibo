#pragma once
#include <vector>
#include "SceneObject.hpp"
#include "Geometry.hpp"
#include "Light.hpp"
#include <memory>
#include "Bear.hpp"
#include "Cube.hpp"
#include "GameRules.hpp"
#include "../Util/Util.hpp"
#include "../Physics/Physics.hpp"
#include <glm/gtc/quaternion.hpp>


using namespace std;

namespace bibo
{
	namespace scene
	{
		/**
		* Scale, rotation and position of a static triangle mesh in the physics world
		*/
		struct MeshTransform
		{
			float scale;
			quat rotation;
			vec3 position;
		};

		/**
		* The collision mesh of the terrain must be placed like the rendered terrain.
		* @param modelMatrix Model matrix of the rendered terrain: uniform scale, rotation (Z-up of Blender/Collada to Y-up) and translation
		*/
		MeshTransform terrainTransform(const mat4& modelMatrix);

		class Scene
		{
			void initPhysicsBodies();
			void initAnimatedTextures();
			//Called when the bear touches a physics body
			void onBearHit(physics::BodyHandle body);
			/**
			* @param frustum Geometries outside of it are not drawn (nullptr: draw all)
			* @return Number of drawn geometries
			*/
			int draw(Shader* shader, mat4 view, mat4 projection, const ViewFrustum* frustum);

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
			Geometry* monitor1;
			Geometry* water;
			unique_ptr<Cube> debugCube;
			unique_ptr<Geometry> rotatingRock;

			//Physics
			physics::PhysicsWorld* physicsWorld;

			bool shadowMappingEnabled = true;
			bool normalMappingEnabled = true;
			bool transparencyEnabled = true;
			bool viewFrustumCullingEnabled = true;
			int drawnGeometryCount = 0;

			bool gameOver = false;
			bool gameWon = false;

			vector<physics::BodyHandle> bodiesRock;

			float accumulatedVideoTime = 0;
			int monitorFrame = 0;
			int waterFrame = 0;

		public:
			Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld);

			void update(float time_delta);

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
			/**
			* Only the geometries that are (partially) inside of the view frustum of the camera are drawn
			*/
			void setViewFrustumCullingEnabled(bool viewFrustumCullingEnabled);
			bool isTransparencyEnabled();
			bool isNormalMappingEnabled();
			bool isShadowMappingEnabled();
			bool isViewFrustumCullingEnabled();
			/**
			* @return Number of geometries drawn by the last renderFromCamera
			*/
			int getDrawnGeometryCount();
			/**
			* @return Number of geometries in the scene
			*/
			int getGeometryCount();
			bool isGameOver();
			bool isGameWon();
		};
	}
}

#pragma once
#include <vector>
#include "SceneObject.hpp"
#include "Geometry.hpp"
#include <memory>
#include "Bear.hpp"
#include "Cube.hpp"
#include "GameRules.hpp"
#include "../Render/Renderer.hpp"
#include "../Util/Input.hpp"
#include "../Util/Util.hpp"
#include "../Physics/Physics.hpp"
#include "../World/World.hpp"
#include <glm/gtc/quaternion.hpp>


using namespace std;

namespace bibo
{
	namespace scene
	{
		/**
		* The race: the bear, the objects of the track (finish line, rocks, signs, ...) and the world around it (World:
		* the island with its forest, caves, sky, water and snow)
		*/
		class Scene : public render::RenderableScene
		{
			void initPhysicsBodies();
			void initAnimatedTextures();
			void initWorld(const aiScene& track);
			/**
			* @param board The finish line (geometry), the trigger goes along it across the track
			*/
			void initFinishLine(Geometry* board);
			//Called when the bear touches a physics body: obstacles (rocks) knock it down
			void onBearHit(const physics::CharacterController::Hit& hit);
			//What the camera follows: the bear
			Camera::FollowTarget cameraTarget();
			/**
			* The objects of the track (not the world)
			* @param frustum Geometries outside of it are not drawn (nullptr: draw all)
			* @param withBear The bear is drawn with the given shader too (its surface), otherwise left out
			* @return Number of drawn geometries
			*/
			int drawGeometries(Shader* shader, const mat4& view, const mat4& projection, const ViewFrustum* frustum, bool withBear);
			/**
			* @return false if the geometry is outside of the frustum and was not drawn
			*/
			bool drawGeometry(Shader* shader, Geometry* geometry, GLint normalMappingEnabledLocation, const ViewFrustum* frustum);
			Geometry* addGeometry(const aiScene* track, const char* nodeName, Texture* texture, Texture* normalMap = nullptr);

			GLFWwindow* window;
			Shader* mainShader;
			unique_ptr<Shader> depthShader;
			unique_ptr<Shader> lightShader;
			vector<unique_ptr<Geometry>> geometries;
			unique_ptr<Camera> camera;
			vector<unique_ptr<Texture>> textures;
			vector<unique_ptr<Texture>> monitorVideo;
			vector<unique_ptr<Geometry>> arrows;
			unique_ptr<world::World> world;

			//special pointers
			Bear* bear;
			Geometry* finishLine;
			Geometry* monitor1;
			Geometry* crate;
			vector<Geometry*> rocks;
			unique_ptr<Cube> debugCube;
			unique_ptr<Geometry> rotatingRock;
			Texture* crateNormalMap;

			//Physics
			physics::PhysicsWorld* physicsWorld;

			bool viewFrustumCullingEnabled = true;
			bool normalMappingEnabled = true;
			int drawnGeometryCount = 0;

			bool gameOver = false;
			bool gameWon = false;

			//Index of the checkpoint the bear has to pass next, see rules::passCheckpoint
			size_t nextCheckpoint = 0;
			rules::FinishLine finishLineTrigger;

			vector<physics::BodyHandle> bodiesRock;
			//Dynamic bodies and their start positions, which they are moved back to by restart()
			vector<pair<physics::BodyHandle, vec3>> dynamicBodies;

			float accumulatedVideoTime = 0;
			int monitorFrame = 0;

		public:
			Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld);
			~Scene() override;

			/**
			* @param player What the player does: moves the bear (relative to the camera), jumps and turns the camera
			*/
			void update(float time_delta, const input::PlayerInput& player = {});
			/**
			* @return false while the camera flies freely (L), then WASD moves the camera instead of the bear
			*/
			bool isCameraLocked() { return camera->isLocked(); }
			Camera& getCamera() { return *camera; }
			Bear& getBear() { return *bear; }
			world::World& getWorld() { return *world; }
			/**
			* Starts the run again: the bear, the movable objects and the lap go back to the start (R key, game over menu)
			*/
			void restart();

			//render::RenderableScene (the frame is rendered by render::Renderer)
			render::CameraView cameraView() override;
			void fillFrameUniforms(render::FrameUniforms& uniforms) override;
			void prepareFrame() override;
			void drawShadowCasters(const render::ShadowCascade& cascade) override;
			void drawMain(const render::RenderSettings& settings) override;

			/**
			* Only the objects of the track with the given shader (e.g. their silhouettes), the camera's view and projection
			*/
			void renderFromCamera(Shader* shader);
			/**
			* Size of the rendered image in pixels (e.g. after the window was resized)
			*/
			void setViewportSize(int width, int height);
			/**
			* Only the geometries that are (partially) inside of the view frustum of the camera are drawn
			*/
			void setViewFrustumCullingEnabled(bool viewFrustumCullingEnabled);
			bool isViewFrustumCullingEnabled();
			/**
			* @return Number of geometries drawn by the last renderFromCamera or drawMain
			*/
			int getDrawnGeometryCount();
			/**
			* @return true while the bear tumbles after running into an obstacle or gets up again
			*/
			bool isBearKnockedDown();
			/**
			* @return Number of geometries in the scene
			*/
			int getGeometryCount();
			int getCheckpointsPassed();
			int getCheckpointCount();
			bool isGameOver();
			bool isGameWon();
		};
	}
}

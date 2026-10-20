#pragma once
#include <vector>
#include "SceneObject.hpp"
#include "Geometry.hpp"
#include <memory>
#include "Bear.hpp"
#include "Beaver.hpp"
#include "CreatureRenderer.hpp"
#include "FurryRenderer.hpp"
#include "GameRules.hpp"
#include "Hedgehog.hpp"
#include "HedgehogRenderer.hpp"
#include "RaceQuest.hpp"
#include "Salmon.hpp"
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
		* The island: the bear, the objects of the track (finish line, signs, ...), the animals (hedgehogs that stroll around
		* on the track, salmon that jump out of the water, the beaver at the start who offers the race as a quest) and the world
		* around it (World: the island with its forest, caves, sky, water and snow). The bear roams freely and swims in the water;
		* the race around the track is a quest (RaceQuest), during its conversation and cutscene the scene moves the bear and the camera.
		*/
		class Scene : public render::RenderableScene
		{
			void initPhysicsBodies();
			void initWorld(const aiScene& track);
			/**
			* The hedgehogs live where the rocks of the track were, the salmon in the water on the track
			*/
			void initAnimals();
			/**
			* @param board The finish line (geometry), the trigger goes along it across the track
			*/
			void initFinishLine(Geometry* board);
			/**
			* The beaver next to the starting line, the race quest, and the start of the bear behind the line
			*/
			void initQuest();
			/**
			* What the bear does for the quest in this frame (where it is, whether it talks to the beaver, ...)
			*/
			RaceQuest::Situation questSituation(const input::PlayerInput& player);
			/**
			* The input of the bear: from the player, or from the cutscene and the conversation
			*/
			BearInput bearInput(const input::PlayerInput& player);
			/**
			* Turns the bear on the spot towards a direction
			* @return true if it looks there
			*/
			bool turnTowards(vec3 direction, BearInput& input);
			/**
			* The camera of the conversation and the cutscene (or the camera that follows the bear)
			*/
			void updateCamera(float time_delta);
			/**
			* The ground under a point (the height field of the world)
			*/
			vec3 groundAt(vec3 position) const;
			//Called when the bear touches a physics body: the hedgehogs knock it down
			void onBearHit(const physics::CharacterController::Hit& hit);
			void updateAnimals(float time_delta);
			//Puts the parts of the animals where they are drawn in this frame
			void placeAnimals();
			void placeSalmon(vec3 center, const quat& rotation, int bites, float wiggle);
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
			vector<unique_ptr<Geometry>> arrows;
			unique_ptr<world::World> world;

			//special pointers
			Bear* bear;
			Geometry* finishLine;
			Geometry* crate;
			Texture* crateNormalMap;

			//Animals
			vector<vec2> hedgehogHomes; //where the rocks of the track are
			vector<Hedgehog> hedgehogs;
			vector<physics::BodyHandle> hedgehogBodies;
			unique_ptr<SalmonPools> salmon;
			unique_ptr<CreatureRenderer> creatures;
			unique_ptr<HedgehogRenderer> hedgehogRenderer;
			unique_ptr<Beaver> beaver;
			physics::BodyHandle beaverBody = physics::INVALID_BODY;
			unique_ptr<FurryRenderer> beaverRenderer;
			int score = 0;

			//Physics
			physics::PhysicsWorld* physicsWorld;

			bool viewFrustumCullingEnabled = true;
			bool normalMappingEnabled = true;
			int drawnGeometryCount = 0;

			//The race around the track
			rules::FinishLine finishLineTrigger;
			unique_ptr<RaceQuest> raceQuest;
			RaceQuest::Stage lastStage = RaceQuest::Stage::IDLE;
			bool atStart = false; //the bear reached the starting line in the cutscene
			bool jumpBlocked = false; //after a conversation until the jump button is released (it may have answered)
			vector<RaceQuest::Event> events; //for takeEvents
			int splashes = 0;

			//Dynamic bodies and their start poses, which they are moved back to by restart()
			struct DynamicBody
			{
				physics::BodyHandle body;
				vec3 position;
				quat rotation;
			};
			vector<DynamicBody> dynamicBodies;

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
			const vector<Hedgehog>& getHedgehogs() const { return hedgehogs; }
			const SalmonPools& getSalmon() const { return *salmon; }
			/**
			* Starts the run again: the bear, the movable objects, the animals, the lap and the score go back to the start
			* (R key, game over menu)
			*/
			void restart();

			const RaceQuest& getRaceQuest() const { return *raceQuest; }
			const Beaver& getBeaver() const { return *beaver; }
			/**
			* @return true if the bear is in the water (swimming or wading deep), the race is lost then
			*/
			bool isBearInWater();
			/**
			* @return What happened in the quest since the last call (e.g. for the sounds)
			*/
			vector<RaceQuest::Event> takeEvents();
			/**
			* @return How often the bear fell into the water since the last call
			*/
			int takeSplashes();

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
			/**
			* @return Bonus points of the run (the salmon the bear caught)
			*/
			int getScore() const { return score; }
			int getSalmonCaught() const { return salmon->getCaughtCount(); }
			int getPoolCount() const { return static_cast<int>(salmon->getPools().size()); }
		};
	}
}

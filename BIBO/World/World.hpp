#pragma once
#include <memory>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Chunk.hpp"
#include "WorldGenerator.hpp"
#include "WorldStreamer.hpp"
#include "../Physics/Physics.hpp"
#include "../Render/FrameUniforms.hpp"
#include "../Render/ShadowCascades.hpp"
#include "../Scene/ViewFrustum.hpp"

struct aiScene;

namespace bibo
{
	namespace world
	{
		class MaterialTextures;
		class SkyRenderer;
		class TerrainRenderer;
		class VegetationRenderer;
		class WaterRenderer;
		class SnowRenderer;

		/**
		* Sun, sky and weather of the island: a bright polar day with a low sun, polar lights and a bit of snow
		*/
		struct Atmosphere
		{
			glm::vec3 toSun; //direction towards the sun
			glm::vec3 sunColor; //radiance
			float auroraIntensity = 1.0f;
			float snowOnObjects = 0.3f; //how much of the upper sides of the trees and stones is covered with snow
			float fogDensity = 0.0011f; //at the sea level
			float fogFalloff = 0.012f; //the fog gets thinner with the height
			float fogMaximum = 0.93f;
			float fogSunScattering = 0.25f;
			glm::vec2 windDirection = glm::normalize(glm::vec2(0.8f, 0.6f));
			float windStrength = 1.0f;

			/**
			* @param elevation Of the sun above the horizon (degrees)
			* @param azimuth Of the sun, from +z towards +x (degrees)
			*/
			static Atmosphere polarDay(float elevation = 27.0f, float azimuth = 40.0f);
		};

		/**
		* The world around the race track: the generated island (WorldGenerator), streamed in chunks and columns with levels
		* of detail (WorldStreamer), in the physics world around the bear, and drawn with the terrain, vegetation, sky, water
		* and snow renderers. Needs an OpenGL context.
		*/
		class World
		{
		public:
			struct Statistics
			{
				WorldStreamer::Statistics streaming;
				int meshes = 0; //chunks with a mesh on the graphics card
				int drawnChunks = 0;
				std::size_t drawnTriangles = 0;
				int drawnInstances = 0;
				std::size_t instances = 0;
			};

			/**
			* @param track The track file: the terrain (node "Boden"), the water level (node "Water")
			* @param start Start and finish of the lap, startDirection: driving direction there
			* @param checkpoints In driving direction
			* @param keepClear Objects of the track that the world must not cover (signs, the finish line, the rocks)
			* @param physicsWorld Gets the terrain and the trees around the bear
			*/
			World(const aiScene& track, glm::vec2 start, glm::vec2 startDirection, const std::vector<glm::vec2>& checkpoints,
				const std::vector<KeepClear>& keepClear, physics::PhysicsWorld* physicsWorld);
			~World();
			World(const World&) = delete;
			World& operator=(const World&) = delete;

			/**
			* Streams the world for the camera and the bear (physics), lets the time pass (wind, polar lights, snow)
			*/
			void update(float timeDelta, glm::vec3 camera, glm::vec3 bear);
			/**
			* Waits until the world around the camera and the bear is complete (loading)
			*/
			void finishLoading(glm::vec3 camera, glm::vec3 bear, float timeoutSeconds);
			/**
			* The ground under the bear is in the physics world right now (e.g. after it was moved back to the start)
			*/
			void ensureCollision(glm::vec3 bear);

			/**
			* Sun, sky, fog, wind, time and the world map (the camera, the shadows and the settings are set by the renderer)
			*/
			void fillFrameUniforms(render::FrameUniforms& uniforms) const;
			/**
			* Renders what the frame needs before the scene (the sky cube map), after the frame uniforms were uploaded
			*/
			void prepareFrame();
			/**
			* Binds the textures of the world that the shaders of the scene use (materials, sky, world map)
			*/
			void bindTextures() const;
			void drawShadows(const render::ShadowCascade& cascade, glm::vec3 camera);
			/**
			* Terrain and vegetation
			* @param culling Leave out what is outside of the view frustum
			*/
			void drawOpaque(const scene::ViewFrustum& frustum, glm::vec3 camera, bool culling);
			void drawSky();
			/**
			* Water and snow (after everything else)
			*/
			void drawTransparent(bool transparency);

			/**
			* @param level 0 low, 1 medium, 2 high: how far the levels of detail and the vegetation reach
			*/
			void setDetail(int level);
			const WorldGenerator& getGenerator() const { return *generator; }
			const Atmosphere& getAtmosphere() const { return atmosphere; }
			float getTime() const { return time; }
			Statistics getStatistics() const;

		private:
			void createWorldMap();

			std::unique_ptr<WorldGenerator> generator;
			Atmosphere atmosphere;
			bool lowDetail;
			int detailLevel = 2;
			float time = 0.0f;
			std::unique_ptr<MaterialTextures> materials;
			std::unique_ptr<SkyRenderer> sky;
			std::unique_ptr<TerrainRenderer> terrain;
			std::unique_ptr<VegetationRenderer> vegetation;
			std::unique_ptr<WaterRenderer> water;
			std::unique_ptr<SnowRenderer> snow;
			GLuint worldMap = 0;
			glm::vec2 worldMapOrigin = glm::vec2(0.0f);
			float worldMapSize = 1.0f;
			int drawnChunks = 0;
			int drawnInstances = 0;
			//Declared last: destroyed first (it removes its bodies from the physics world and stops its threads)
			std::unique_ptr<WorldStreamer> streamer;
		};
	}
}

#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "Chunk.hpp"
#include "Vegetation.hpp"
#include "WorkerPool.hpp"
#include "../Physics/Physics.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* Streams the world: decides which level of detail every chunk needs (by its distance from the camera), lets the
		* worker threads mesh the chunks and scatter the vegetation, and hands the results to the renderer. Around the bear
		* the chunks get collision shapes (and the trees their trunks) in the physics world, further away they are removed.
		*/
		class WorldStreamer
		{
		public:
			struct Settings
			{
				//A chunk closer than lodDistances[i] to the camera gets level of detail i (or finer), further away the coarsest
				float lodDistances[LOD_COUNT - 1] = { 64.0f, 136.0f, 280.0f };
				//Chunks and trees closer than this to the bear are in the physics world
				float physicsRadius = 48.0f;
				//Grass, ferns, mushrooms and pebbles are placed in the columns closer than this to the camera
				float smallRadius = 72.0f;
				//Results integrated per update (each is uploaded to the graphics card)
				int resultsPerUpdate = 48;
			};

			/**
			* A chunk got a new mesh (or none: removed)
			*/
			struct MeshUpdate
			{
				ChunkKey key;
				std::shared_ptr<const ChunkMesh> mesh; //nullptr: remove
			};

			/**
			* A column got new instances of a layer (or none: removed)
			*/
			struct VegetationUpdate
			{
				ColumnKey key;
				Layer layer;
				std::shared_ptr<const std::vector<Instance>> instances; //nullptr: remove
			};

			struct Statistics
			{
				int chunks = 0; //that can have a surface
				int chunksAtLod[LOD_COUNT] = {};
				int collisionBodies = 0;
				int pendingJobs = 0;
				int smallColumns = 0;
			};

			/**
			* @param physicsWorld Gets the collision shapes (nullptr: no physics)
			* @param threads Number of worker threads (0: depends on the processor)
			*/
			WorldStreamer(const WorldGenerator& world, physics::PhysicsWorld* physicsWorld, const Settings& settings, unsigned int threads = 0);
			~WorldStreamer();
			WorldStreamer(const WorldStreamer&) = delete;
			WorldStreamer& operator=(const WorldStreamer&) = delete;

			/**
			* Takes the finished jobs and starts new ones
			* @param viewer Position of the camera (level of detail, small vegetation)
			* @param focus Position of the bear (physics)
			*/
			void update(glm::vec3 viewer, glm::vec3 focus);
			/**
			* Generates what the bear needs to stand on right now in this thread, if the workers did not do it yet (e.g. at the start)
			*/
			void ensureCollision(glm::vec3 focus, float radius);
			/**
			* Waits until everything that is needed for the viewer and focus is done (e.g. while loading)
			* @return false if it took longer than the timeout
			*/
			bool finish(glm::vec3 viewer, glm::vec3 focus, float timeoutSeconds);

			std::vector<MeshUpdate> takeMeshUpdates();
			std::vector<VegetationUpdate> takeVegetationUpdates();
			Statistics getStatistics();
			const Settings& getSettings() const { return settings; }
			void setSettings(const Settings& newSettings) { settings = newSettings; }
			const WorldGenerator& getWorld() const { return world; }

		private:
			struct ChunkState
			{
				ChunkKey key;
				Box box;
				int lod = -1; //of the current mesh (-1: none yet)
				int requested = -1; //lod of the job in progress
				int target = -1;
				std::uint64_t generation = 0; //results of jobs started before ensureCollision meshed the chunk are ignored
				bool needsCollision = false;
				std::shared_ptr<physics::CollisionShape> shape; //of the lod 0 mesh
				physics::BodyHandle body = physics::INVALID_BODY;
			};

			struct ColumnState
			{
				ColumnKey key;
				Box box;
				bool largeRequested = false, largeDone = false;
				std::shared_ptr<physics::CollisionShape> shape; //trunks and boulders
				physics::BodyHandle body = physics::INVALID_BODY;
				bool smallWanted = false, smallRequested = false, smallLoaded = false;
				std::uint64_t smallGeneration = 0; //results of older requests are ignored
			};

			struct Result
			{
				enum Kind { CHUNK, LARGE, SMALL };
				Kind kind = CHUNK;
				std::size_t index = 0;
				int lod = 0;
				std::uint64_t generation = 0;
				std::shared_ptr<ChunkMesh> mesh;
				std::shared_ptr<physics::CollisionShape> shape;
				std::shared_ptr<std::vector<Instance>> instances;
			};

			void requestChunk(std::size_t index, int lod, float priority);
			void requestColumn(std::size_t index, Layer layer, float priority);
			Result makeChunk(std::size_t index, int lod, std::uint64_t generation) const;
			Result makeColumn(std::size_t index, Layer layer, std::uint64_t generation) const;
			void integrate(Result& result);
			void updatePhysics(glm::vec3 focus);
			int targetLod(const ChunkState& chunk, glm::vec3 viewer) const;
			bool isDone(glm::vec3 viewer, glm::vec3 focus);

			const WorldGenerator& world;
			physics::PhysicsWorld* physicsWorld;
			Settings settings;
			std::vector<ChunkState> chunks;
			std::vector<ColumnState> columns;
			std::unordered_map<ColumnKey, std::size_t, ColumnKeyHash> columnIndex;

			std::mutex resultMutex;
			std::vector<Result> results;
			std::vector<MeshUpdate> meshUpdates;
			std::vector<VegetationUpdate> vegetationUpdates;
			int inFlight = 0; //jobs submitted whose results were not integrated yet
			//Declared last: destroyed first, so that no job runs while the rest is destroyed
			std::unique_ptr<WorkerPool> pool;
		};

		/**
		* Distance from a point to a box (0 inside)
		*/
		float distanceToBox(glm::vec3 point, const Box& box);
	}
}

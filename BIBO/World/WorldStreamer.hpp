#ifndef BIBO_WORLD_WORLDSTREAMER_HPP_
#define BIBO_WORLD_WORLDSTREAMER_HPP_
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../Physics/Physics.hpp"
#include "Chunk.hpp"
#include "Vegetation.hpp"
#include "WorkerPool.hpp"

namespace bibo {
namespace world {
/**
 * Streams the world: decides which level of detail every chunk needs (by its
 * distance from the camera), lets the worker threads mesh the chunks and
 * scatter the vegetation, and hands the results to the renderer. Around the
 * bear the chunks get collision shapes (and the trees their trunks) in the
 * physics world, further away they are removed.
 */
class WorldStreamer {
 public:
  struct Settings {
    // A chunk closer than lod_distances[i] to the camera gets level of detail i
    // (or finer), further away the coarsest
    float lod_distances[kLodCount - 1] = {64.0f, 136.0f, 280.0f};
    // Chunks and trees closer than this to the bear are in the physics world
    float physics_radius = 48.0f;
    // Grass, ferns, mushrooms and pebbles are placed in the columns closer than
    // this to the camera
    float small_radius = 72.0f;
    // Results integrated per update (each is uploaded to the graphics card)
    int results_per_update = 48;
  };

  /**
   * A chunk got a new mesh (or none: removed)
   */
  struct MeshUpdate {
    ChunkKey key;
    std::shared_ptr<const ChunkMesh> mesh;  // nullptr: remove
  };

  /**
   * A column got new instances of a layer (or none: removed)
   */
  struct VegetationUpdate {
    ColumnKey key;
    Layer layer;
    std::shared_ptr<const std::vector<Instance>> instances;  // nullptr: remove
  };

  struct Statistics {
    int chunks = 0;  // that can have a surface
    int chunks_at_lod[kLodCount] = {};
    int collision_bodies = 0;
    int pending_jobs = 0;
    int small_columns = 0;
  };

  /**
   * @param physicsWorld Gets the collision shapes (nullptr: no physics)
   * @param threads Number of worker threads (0: depends on the processor)
   */
  WorldStreamer(const WorldGenerator& world,
                physics::PhysicsWorld* physics_world, const Settings& settings,
                unsigned int threads = 0);
  ~WorldStreamer();
  WorldStreamer(const WorldStreamer&) = delete;
  WorldStreamer& operator=(const WorldStreamer&) = delete;

  /**
   * Takes the finished jobs and starts new ones
   * @param viewer Position of the camera (level of detail, small vegetation)
   * @param focus Position of the bear (physics)
   */
  void Update(glm::vec3 viewer, glm::vec3 focus);
  /**
   * Generates what the bear needs to stand on right now in this thread, if the
   * workers did not do it yet (e.g. at the start)
   */
  void EnsureCollision(glm::vec3 focus, float radius);
  /**
   * Waits until everything that is needed for the viewer and focus is done
   * (e.g. while loading)
   * @return false if it took longer than the timeout
   */
  bool Finish(glm::vec3 viewer, glm::vec3 focus, float timeout_seconds);

  std::vector<MeshUpdate> TakeMeshUpdates();
  std::vector<VegetationUpdate> TakeVegetationUpdates();
  Statistics GetStatistics();
  const Settings& GetSettings() const { return settings_; }
  void SetSettings(const Settings& new_settings) { settings_ = new_settings; }
  const WorldGenerator& GetWorld() const { return world_; }

 private:
  struct ChunkState {
    ChunkKey key;
    Box box;
    int lod = -1;        // of the current mesh (-1: none yet)
    int requested = -1;  // lod of the job in progress
    int target = -1;
    // results of jobs started before EnsureCollision meshed the chunk are
    // ignored
    std::uint64_t generation = 0;
    bool needs_collision = false;
    std::shared_ptr<physics::CollisionShape> shape;  // of the lod 0 mesh
    physics::BodyHandle body = physics::kInvalidBody;
  };

  struct ColumnState {
    ColumnKey key;
    Box box;
    bool large_requested = false, large_done = false;
    std::shared_ptr<physics::CollisionShape> shape;  // trunks and boulders
    physics::BodyHandle body = physics::kInvalidBody;
    bool small_wanted = false, small_requested = false, small_loaded = false;
    std::uint64_t small_generation =
        0;  // results of older requests are ignored
  };

  struct Result {
    enum Kind { kChunk, kLarge, kSmall };
    Kind kind = kChunk;
    std::size_t index = 0;
    int lod = 0;
    std::uint64_t generation = 0;
    std::shared_ptr<ChunkMesh> mesh;
    std::shared_ptr<physics::CollisionShape> shape;
    std::shared_ptr<std::vector<Instance>> instances;
  };

  void RequestChunk(std::size_t index, int lod, float priority);
  void RequestColumn(std::size_t index, Layer layer, float priority);
  Result MakeChunk(std::size_t index, int lod, std::uint64_t generation) const;
  Result MakeColumn(std::size_t index, Layer layer,
                    std::uint64_t generation) const;
  void Integrate(Result& result);
  void UpdatePhysics(glm::vec3 focus);
  int TargetLod(const ChunkState& chunk, glm::vec3 viewer) const;
  bool IsDone(glm::vec3 viewer, glm::vec3 focus);

  const WorldGenerator& world_;
  physics::PhysicsWorld* physics_world_;
  Settings settings_;
  std::vector<ChunkState> chunks_;
  std::vector<ColumnState> columns_;
  std::unordered_map<ColumnKey, std::size_t, ColumnKeyHash> column_index_;

  std::mutex result_mutex_;
  std::vector<Result> results_;
  std::vector<MeshUpdate> mesh_updates_;
  std::vector<VegetationUpdate> vegetation_updates_;
  int in_flight_ = 0;  // jobs submitted whose results were not integrated yet
  // Declared last: destroyed first, so that no job runs while the rest is
  // destroyed
  std::unique_ptr<WorkerPool> pool_;
};

/**
 * Distance from a point to a box (0 inside)
 */
float DistanceToBox(glm::vec3 point, const Box& box);
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_WORLDSTREAMER_HPP_

#include "WorldStreamer.hpp"

#include <algorithm>
#include <chrono>

namespace bibo::world {

namespace {
// The levels of detail of a chunk only get coarser again when it is this much
// further away than where they got finer
const float kLodHysteresis = 12.0f;
// Bodies are removed when they are this much further away than where they were
// added
const float kPhysicsHysteresis = 16.0f;
const float kSmallHysteresis = 24.0f;
}  // namespace

float DistanceToBox(glm::vec3 point, const Box& box) {
  glm::vec3 outside = glm::max(
      glm::max(box.minimum - point, point - box.maximum), glm::vec3(0.0f));
  return glm::length(outside);
}

WorldStreamer::WorldStreamer(const WorldGenerator& world,
                             physics::PhysicsWorld* physics_world,
                             const Settings& settings, unsigned int threads)
    : world_(world), physics_world_(physics_world), settings_(settings) {
  Box bounds = world.GetBounds();
  ChunkKey low = ChunkAt(bounds.minimum), high = ChunkAt(bounds.maximum);
  for (int z = low.z; z <= high.z; ++z) {
    for (int x = low.x; x <= high.x; ++x) {
      bool column_has_chunks = false;
      for (int y = low.y; y <= high.y; ++y) {
        ChunkState chunk;
        chunk.key = {x, y, z};
        chunk.box = ChunkBox(chunk.key);
        Box margin = chunk.box;
        margin.minimum -= glm::vec3(3.0f);
        margin.maximum += glm::vec3(3.0f);
        if (world.MayContainSurface(margin)) {
          chunks_.push_back(chunk);
          column_has_chunks = true;
        }
      }
      if (column_has_chunks) {
        ColumnState column;
        column.key = {x, z};
        column.box.minimum =
            glm::vec3(static_cast<float>(x) * kChunkSize, bounds.minimum.y,
                      static_cast<float>(z) * kChunkSize);
        column.box.maximum =
            glm::vec3(static_cast<float>(x + 1) * kChunkSize, bounds.maximum.y,
                      static_cast<float>(z + 1) * kChunkSize);
        column_index_[column.key] = columns_.size();
        columns_.push_back(column);
      }
    }
  }
  pool_ = std::make_unique<WorkerPool>(threads);
}

WorldStreamer::~WorldStreamer() {
  // No job may run while the streamer is destroyed
  pool_.reset();
  if (physics_world_) {
    for (ChunkState& chunk : chunks_) {
      physics_world_->RemoveBody(chunk.body);
    }
    for (ColumnState& column : columns_) {
      physics_world_->RemoveBody(column.body);
    }
  }
}

int WorldStreamer::TargetLod(const ChunkState& chunk, glm::vec3 viewer) const {
  float distance = DistanceToBox(viewer, chunk.box);
  int lod = kLodCount - 1;
  for (int i = 0; i < kLodCount - 1; ++i) {
    if (distance < settings_.lod_distances[i]) {
      lod = i;
      break;
    }
  }
  // A finer level is kept a bit longer, so that a chunk at the border does not
  // switch back and forth
  if (chunk.lod >= 0 && chunk.lod < lod && chunk.lod < kLodCount - 1 &&
      distance < settings_.lod_distances[chunk.lod] + kLodHysteresis) {
    lod = chunk.lod;
  }
  return lod;
}

WorldStreamer::Result WorldStreamer::MakeChunk(std::size_t index, int lod,
                                               std::uint64_t generation) const {
  Result result;
  result.kind = Result::kChunk;
  result.index = index;
  result.lod = lod;
  result.generation = generation;
  result.mesh =
      std::make_shared<ChunkMesh>(MeshChunk(world_, chunks_[index].key, lod));
  if (lod == 0 && physics_world_ && !result.mesh->Empty()) {
    std::vector<glm::vec3> positions(result.mesh->vertices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
      positions[i] = result.mesh->vertices[i].position;
    }
    // Without the skirts: the neighbors of a chunk in the physics world all
    // have level 0, there are no cracks
    std::vector<std::uint32_t> surface(
        result.mesh->indices.begin(),
        result.mesh->indices.begin() +
            static_cast<std::ptrdiff_t>(result.mesh->surface_index_count));
    result.shape = physics::PhysicsWorld::BuildMeshShape(positions, surface);
  }
  return result;
}

WorldStreamer::Result WorldStreamer::MakeColumn(
    std::size_t index, Layer layer, std::uint64_t generation) const {
  Result result;
  result.kind = layer == Layer::kLarge ? Result::kLarge : Result::kSmall;
  result.index = index;
  result.generation = generation;
  std::vector<physics::ShapePart> parts;
  result.instances = std::make_shared<std::vector<Instance>>(
      Scatter(world_, columns_[index].key, layer,
              layer == Layer::kLarge && physics_world_ ? &parts : nullptr));
  if (!parts.empty()) {
    result.shape = physics::PhysicsWorld::BuildCompoundShape(parts);
  }
  return result;
}

void WorldStreamer::RequestChunk(std::size_t index, int lod, float priority) {
  chunks_[index].requested = lod;
  std::uint64_t generation = chunks_[index].generation;
  ++in_flight_;
  pool_->Submit(
      [this, index, lod, generation] {
        Result result = MakeChunk(index, lod, generation);
        std::lock_guard<std::mutex> lock(result_mutex_);
        results_.push_back(std::move(result));
      },
      priority);
}

void WorldStreamer::RequestColumn(std::size_t index, Layer layer,
                                  float priority) {
  ColumnState& column = columns_[index];
  std::uint64_t generation = column.small_generation;
  (layer == Layer::kLarge ? column.large_requested : column.small_requested) =
      true;
  ++in_flight_;
  pool_->Submit(
      [this, index, layer, generation] {
        Result result = MakeColumn(index, layer, generation);
        std::lock_guard<std::mutex> lock(result_mutex_);
        results_.push_back(std::move(result));
      },
      priority);
}

void WorldStreamer::Integrate(Result& result) {
  if (result.kind == Result::kChunk) {
    ChunkState& chunk = chunks_[result.index];
    if (result.generation != chunk.generation) {
      return;  // EnsureCollision meshed the chunk while the job was running
    }
    chunk.lod = result.lod;
    mesh_updates_.push_back(
        {chunk.key, result.mesh->Empty()
                        ? nullptr
                        : std::shared_ptr<const ChunkMesh>(result.mesh)});
    if (result.lod == 0) {
      chunk.shape = result.shape;
    }
    return;
  }
  ColumnState& column = columns_[result.index];
  if (result.kind == Result::kLarge) {
    column.large_done = true;
    column.shape = result.shape;
    vegetation_updates_.push_back(
        {column.key, Layer::kLarge, result.instances});
    return;
  }
  if (result.generation != column.small_generation || !column.small_wanted) {
    return;  // no longer needed
  }
  column.small_loaded = true;
  vegetation_updates_.push_back({column.key, Layer::kSmall, result.instances});
}

void WorldStreamer::Update(glm::vec3 viewer, glm::vec3 focus) {
  // 1. Finished jobs (a limited number per update, every one is uploaded to the
  // graphics card)
  std::vector<Result> ready;
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    std::size_t count = std::min(
        results_.size(),
        static_cast<std::size_t>(std::max(settings_.results_per_update, 1)));
    ready.assign(std::make_move_iterator(results_.begin()),
                 std::make_move_iterator(results_.begin() +
                                         static_cast<std::ptrdiff_t>(count)));
    results_.erase(results_.begin(),
                   results_.begin() + static_cast<std::ptrdiff_t>(count));
  }
  for (Result& result : ready) {
    --in_flight_;
    if (result.kind == Result::kChunk) {
      chunks_[result.index].requested = -1;
    } else if (result.kind == Result::kSmall) {
      columns_[result.index].small_requested = false;
    }
    Integrate(result);
  }

  // 2. Every chunk in the level of detail it needs, at the bear in the finest
  // one (for the physics)
  for (std::size_t i = 0; i < chunks_.size(); ++i) {
    ChunkState& chunk = chunks_[i];
    chunk.needs_collision = physics_world_ && DistanceToBox(focus, chunk.box) <
                                                  settings_.physics_radius;
    chunk.target = chunk.needs_collision ? 0 : TargetLod(chunk, viewer);
    if (chunk.requested < 0 && chunk.lod != chunk.target) {
      float distance = DistanceToBox(viewer, chunk.box);
      // What the bear stands on first, then chunks that have nothing yet, then
      // the closest
      float priority = distance - (chunk.needs_collision ? 100000.0f : 0.0f) -
                       (chunk.lod < 0 ? 1000.0f : 0.0f);
      RequestChunk(i, chunk.target, priority);
    }
  }

  // 3. Vegetation: the large layer everywhere, the small one close to the
  // camera
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    ColumnState& column = columns_[i];
    float distance = DistanceToBox(viewer, column.box);
    if (!column.large_requested) {
      RequestColumn(
          i, Layer::kLarge,
          distance + 40.0f -
              (DistanceToBox(focus, column.box) < settings_.physics_radius
                   ? 100000.0f
                   : 0.0f));
    }
    if (distance < settings_.small_radius) {
      column.small_wanted = true;
      if (!column.small_loaded && !column.small_requested) {
        RequestColumn(i, Layer::kSmall, distance);
      }
    } else if (distance > settings_.small_radius + kSmallHysteresis &&
               column.small_wanted) {
      column.small_wanted = false;
      ++column.small_generation;
      if (column.small_loaded) {
        column.small_loaded = false;
        vegetation_updates_.push_back({column.key, Layer::kSmall, nullptr});
      }
    }
  }

  UpdatePhysics(focus);
}

void WorldStreamer::UpdatePhysics(glm::vec3 focus) {
  if (!physics_world_) {
    return;
  }
  for (ChunkState& chunk : chunks_) {
    float distance = DistanceToBox(focus, chunk.box);
    if (chunk.body == physics::kInvalidBody && chunk.lod == 0 && chunk.shape &&
        distance < settings_.physics_radius) {
      chunk.body = physics_world_->AddStaticBody(*chunk.shape);
    } else if (chunk.body != physics::kInvalidBody &&
               distance > settings_.physics_radius + kPhysicsHysteresis) {
      physics_world_->RemoveBody(chunk.body);
      chunk.body = physics::kInvalidBody;
    }
  }
  for (ColumnState& column : columns_) {
    float distance = DistanceToBox(focus, column.box);
    if (column.body == physics::kInvalidBody && column.shape &&
        distance < settings_.physics_radius) {
      column.body = physics_world_->AddStaticBody(*column.shape);
    } else if (column.body != physics::kInvalidBody &&
               distance > settings_.physics_radius + kPhysicsHysteresis) {
      physics_world_->RemoveBody(column.body);
      column.body = physics::kInvalidBody;
    }
  }
}

void WorldStreamer::EnsureCollision(glm::vec3 focus, float radius) {
  if (!physics_world_) {
    return;
  }
  for (std::size_t i = 0; i < chunks_.size(); ++i) {
    if (chunks_[i].lod != 0 && DistanceToBox(focus, chunks_[i].box) < radius) {
      // A job for the chunk that is still running delivers its result later, it
      // is ignored then
      Result result = MakeChunk(i, 0, ++chunks_[i].generation);
      Integrate(result);
    }
  }
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (!columns_[i].large_done &&
        DistanceToBox(focus, columns_[i].box) < radius) {
      Result result = MakeColumn(i, Layer::kLarge, 0);
      Integrate(result);
    }
  }
  UpdatePhysics(focus);
}

bool WorldStreamer::IsDone(glm::vec3 viewer, glm::vec3 focus) {
  if (in_flight_ > 0) {
    return false;
  }
  for (const ChunkState& chunk : chunks_) {
    if (chunk.lod != chunk.target) {
      return false;
    }
  }
  for (const ColumnState& column : columns_) {
    if (!column.large_done || (column.small_wanted && !column.small_loaded)) {
      return false;
    }
  }
  return true;
}

bool WorldStreamer::Finish(glm::vec3 viewer, glm::vec3 focus,
                           float timeout_seconds) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::duration<float>(timeout_seconds);
  while (true) {
    Update(viewer, focus);
    if (IsDone(viewer, focus)) {
      return true;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::vector<WorldStreamer::MeshUpdate> WorldStreamer::TakeMeshUpdates() {
  std::vector<MeshUpdate> taken;
  taken.swap(mesh_updates_);
  return taken;
}

std::vector<WorldStreamer::VegetationUpdate>
WorldStreamer::TakeVegetationUpdates() {
  std::vector<VegetationUpdate> taken;
  taken.swap(vegetation_updates_);
  return taken;
}

WorldStreamer::Statistics WorldStreamer::GetStatistics() {
  Statistics statistics;
  statistics.chunks = static_cast<int>(chunks_.size());
  for (const ChunkState& chunk : chunks_) {
    if (chunk.lod >= 0) {
      ++statistics.chunks_at_lod[chunk.lod];
    }
    statistics.collision_bodies += chunk.body != physics::kInvalidBody ? 1 : 0;
  }
  for (const ColumnState& column : columns_) {
    statistics.collision_bodies += column.body != physics::kInvalidBody ? 1 : 0;
    statistics.small_columns += column.small_loaded ? 1 : 0;
  }
  statistics.pending_jobs = in_flight_;
  return statistics;
}

}  // namespace bibo::world

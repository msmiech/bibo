#include "WorldStreamer.hpp"

#include <algorithm>
#include <chrono>

using namespace bibo::world;

namespace {
// The levels of detail of a chunk only get coarser again when it is this much
// further away than where they got finer
const float LOD_HYSTERESIS = 12.0f;
// Bodies are removed when they are this much further away than where they were
// added
const float PHYSICS_HYSTERESIS = 16.0f;
const float SMALL_HYSTERESIS = 24.0f;
}  // namespace

float bibo::world::distanceToBox(glm::vec3 point, const Box& box) {
  glm::vec3 outside = glm::max(
      glm::max(box.minimum - point, point - box.maximum), glm::vec3(0.0f));
  return glm::length(outside);
}

WorldStreamer::WorldStreamer(const WorldGenerator& world,
                             physics::PhysicsWorld* physicsWorld,
                             const Settings& settings, unsigned int threads)
    : world(world), physicsWorld(physicsWorld), settings(settings) {
  Box bounds = world.getBounds();
  ChunkKey low = chunkAt(bounds.minimum), high = chunkAt(bounds.maximum);
  for (int z = low.z; z <= high.z; ++z) {
    for (int x = low.x; x <= high.x; ++x) {
      bool columnHasChunks = false;
      for (int y = low.y; y <= high.y; ++y) {
        ChunkState chunk;
        chunk.key = {x, y, z};
        chunk.box = chunkBox(chunk.key);
        Box margin = chunk.box;
        margin.minimum -= glm::vec3(3.0f);
        margin.maximum += glm::vec3(3.0f);
        if (world.mayContainSurface(margin)) {
          chunks.push_back(chunk);
          columnHasChunks = true;
        }
      }
      if (columnHasChunks) {
        ColumnState column;
        column.key = {x, z};
        column.box.minimum =
            glm::vec3(static_cast<float>(x) * CHUNK_SIZE, bounds.minimum.y,
                      static_cast<float>(z) * CHUNK_SIZE);
        column.box.maximum =
            glm::vec3(static_cast<float>(x + 1) * CHUNK_SIZE, bounds.maximum.y,
                      static_cast<float>(z + 1) * CHUNK_SIZE);
        columnIndex[column.key] = columns.size();
        columns.push_back(column);
      }
    }
  }
  pool = std::make_unique<WorkerPool>(threads);
}

WorldStreamer::~WorldStreamer() {
  // No job may run while the streamer is destroyed
  pool.reset();
  if (physicsWorld) {
    for (ChunkState& chunk : chunks) {
      physicsWorld->removeBody(chunk.body);
    }
    for (ColumnState& column : columns) {
      physicsWorld->removeBody(column.body);
    }
  }
}

int WorldStreamer::targetLod(const ChunkState& chunk, glm::vec3 viewer) const {
  float distance = distanceToBox(viewer, chunk.box);
  int lod = LOD_COUNT - 1;
  for (int i = 0; i < LOD_COUNT - 1; ++i) {
    if (distance < settings.lodDistances[i]) {
      lod = i;
      break;
    }
  }
  // A finer level is kept a bit longer, so that a chunk at the border does not
  // switch back and forth
  if (chunk.lod >= 0 && chunk.lod < lod && chunk.lod < LOD_COUNT - 1 &&
      distance < settings.lodDistances[chunk.lod] + LOD_HYSTERESIS) {
    lod = chunk.lod;
  }
  return lod;
}

WorldStreamer::Result WorldStreamer::makeChunk(std::size_t index, int lod,
                                               std::uint64_t generation) const {
  Result result;
  result.kind = Result::CHUNK;
  result.index = index;
  result.lod = lod;
  result.generation = generation;
  result.mesh =
      std::make_shared<ChunkMesh>(meshChunk(world, chunks[index].key, lod));
  if (lod == 0 && physicsWorld && !result.mesh->empty()) {
    std::vector<glm::vec3> positions(result.mesh->vertices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
      positions[i] = result.mesh->vertices[i].position;
    }
    // Without the skirts: the neighbors of a chunk in the physics world all
    // have level 0, there are no cracks
    std::vector<std::uint32_t> surface(
        result.mesh->indices.begin(),
        result.mesh->indices.begin() +
            static_cast<std::ptrdiff_t>(result.mesh->surfaceIndexCount));
    result.shape = physics::PhysicsWorld::buildMeshShape(positions, surface);
  }
  return result;
}

WorldStreamer::Result WorldStreamer::makeColumn(
    std::size_t index, Layer layer, std::uint64_t generation) const {
  Result result;
  result.kind = layer == Layer::LARGE ? Result::LARGE : Result::SMALL;
  result.index = index;
  result.generation = generation;
  std::vector<physics::ShapePart> parts;
  result.instances = std::make_shared<std::vector<Instance>>(
      scatter(world, columns[index].key, layer,
              layer == Layer::LARGE && physicsWorld ? &parts : nullptr));
  if (!parts.empty()) {
    result.shape = physics::PhysicsWorld::buildCompoundShape(parts);
  }
  return result;
}

void WorldStreamer::requestChunk(std::size_t index, int lod, float priority) {
  chunks[index].requested = lod;
  std::uint64_t generation = chunks[index].generation;
  ++inFlight;
  pool->submit(
      [this, index, lod, generation] {
        Result result = makeChunk(index, lod, generation);
        std::lock_guard<std::mutex> lock(resultMutex);
        results.push_back(std::move(result));
      },
      priority);
}

void WorldStreamer::requestColumn(std::size_t index, Layer layer,
                                  float priority) {
  ColumnState& column = columns[index];
  std::uint64_t generation = column.smallGeneration;
  (layer == Layer::LARGE ? column.largeRequested : column.smallRequested) =
      true;
  ++inFlight;
  pool->submit(
      [this, index, layer, generation] {
        Result result = makeColumn(index, layer, generation);
        std::lock_guard<std::mutex> lock(resultMutex);
        results.push_back(std::move(result));
      },
      priority);
}

void WorldStreamer::integrate(Result& result) {
  if (result.kind == Result::CHUNK) {
    ChunkState& chunk = chunks[result.index];
    if (result.generation != chunk.generation) {
      return;  // ensureCollision meshed the chunk while the job was running
    }
    chunk.lod = result.lod;
    meshUpdates.push_back(
        {chunk.key, result.mesh->empty()
                        ? nullptr
                        : std::shared_ptr<const ChunkMesh>(result.mesh)});
    if (result.lod == 0) {
      chunk.shape = result.shape;
    }
    return;
  }
  ColumnState& column = columns[result.index];
  if (result.kind == Result::LARGE) {
    column.largeDone = true;
    column.shape = result.shape;
    vegetationUpdates.push_back({column.key, Layer::LARGE, result.instances});
    return;
  }
  if (result.generation != column.smallGeneration || !column.smallWanted) {
    return;  // no longer needed
  }
  column.smallLoaded = true;
  vegetationUpdates.push_back({column.key, Layer::SMALL, result.instances});
}

void WorldStreamer::update(glm::vec3 viewer, glm::vec3 focus) {
  // 1. Finished jobs (a limited number per update, every one is uploaded to the
  // graphics card)
  std::vector<Result> ready;
  {
    std::lock_guard<std::mutex> lock(resultMutex);
    std::size_t count = std::min(
        results.size(),
        static_cast<std::size_t>(std::max(settings.resultsPerUpdate, 1)));
    ready.assign(std::make_move_iterator(results.begin()),
                 std::make_move_iterator(results.begin() +
                                         static_cast<std::ptrdiff_t>(count)));
    results.erase(results.begin(),
                  results.begin() + static_cast<std::ptrdiff_t>(count));
  }
  for (Result& result : ready) {
    --inFlight;
    if (result.kind == Result::CHUNK) {
      chunks[result.index].requested = -1;
    } else if (result.kind == Result::SMALL) {
      columns[result.index].smallRequested = false;
    }
    integrate(result);
  }

  // 2. Every chunk in the level of detail it needs, at the bear in the finest
  // one (for the physics)
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    ChunkState& chunk = chunks[i];
    chunk.needsCollision = physicsWorld && distanceToBox(focus, chunk.box) <
                                               settings.physicsRadius;
    chunk.target = chunk.needsCollision ? 0 : targetLod(chunk, viewer);
    if (chunk.requested < 0 && chunk.lod != chunk.target) {
      float distance = distanceToBox(viewer, chunk.box);
      // What the bear stands on first, then chunks that have nothing yet, then
      // the closest
      float priority = distance - (chunk.needsCollision ? 100000.0f : 0.0f) -
                       (chunk.lod < 0 ? 1000.0f : 0.0f);
      requestChunk(i, chunk.target, priority);
    }
  }

  // 3. Vegetation: the large layer everywhere, the small one close to the
  // camera
  for (std::size_t i = 0; i < columns.size(); ++i) {
    ColumnState& column = columns[i];
    float distance = distanceToBox(viewer, column.box);
    if (!column.largeRequested) {
      requestColumn(
          i, Layer::LARGE,
          distance + 40.0f -
              (distanceToBox(focus, column.box) < settings.physicsRadius
                   ? 100000.0f
                   : 0.0f));
    }
    if (distance < settings.smallRadius) {
      column.smallWanted = true;
      if (!column.smallLoaded && !column.smallRequested) {
        requestColumn(i, Layer::SMALL, distance);
      }
    } else if (distance > settings.smallRadius + SMALL_HYSTERESIS &&
               column.smallWanted) {
      column.smallWanted = false;
      ++column.smallGeneration;
      if (column.smallLoaded) {
        column.smallLoaded = false;
        vegetationUpdates.push_back({column.key, Layer::SMALL, nullptr});
      }
    }
  }

  updatePhysics(focus);
}

void WorldStreamer::updatePhysics(glm::vec3 focus) {
  if (!physicsWorld) {
    return;
  }
  for (ChunkState& chunk : chunks) {
    float distance = distanceToBox(focus, chunk.box);
    if (chunk.body == physics::INVALID_BODY && chunk.lod == 0 && chunk.shape &&
        distance < settings.physicsRadius) {
      chunk.body = physicsWorld->addStaticBody(*chunk.shape);
    } else if (chunk.body != physics::INVALID_BODY &&
               distance > settings.physicsRadius + PHYSICS_HYSTERESIS) {
      physicsWorld->removeBody(chunk.body);
      chunk.body = physics::INVALID_BODY;
    }
  }
  for (ColumnState& column : columns) {
    float distance = distanceToBox(focus, column.box);
    if (column.body == physics::INVALID_BODY && column.shape &&
        distance < settings.physicsRadius) {
      column.body = physicsWorld->addStaticBody(*column.shape);
    } else if (column.body != physics::INVALID_BODY &&
               distance > settings.physicsRadius + PHYSICS_HYSTERESIS) {
      physicsWorld->removeBody(column.body);
      column.body = physics::INVALID_BODY;
    }
  }
}

void WorldStreamer::ensureCollision(glm::vec3 focus, float radius) {
  if (!physicsWorld) {
    return;
  }
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    if (chunks[i].lod != 0 && distanceToBox(focus, chunks[i].box) < radius) {
      // A job for the chunk that is still running delivers its result later, it
      // is ignored then
      Result result = makeChunk(i, 0, ++chunks[i].generation);
      integrate(result);
    }
  }
  for (std::size_t i = 0; i < columns.size(); ++i) {
    if (!columns[i].largeDone &&
        distanceToBox(focus, columns[i].box) < radius) {
      Result result = makeColumn(i, Layer::LARGE, 0);
      integrate(result);
    }
  }
  updatePhysics(focus);
}

bool WorldStreamer::isDone(glm::vec3 viewer, glm::vec3 focus) {
  if (inFlight > 0) {
    return false;
  }
  for (const ChunkState& chunk : chunks) {
    if (chunk.lod != chunk.target) {
      return false;
    }
  }
  for (const ColumnState& column : columns) {
    if (!column.largeDone || (column.smallWanted && !column.smallLoaded)) {
      return false;
    }
  }
  return true;
}

bool WorldStreamer::finish(glm::vec3 viewer, glm::vec3 focus,
                           float timeoutSeconds) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::duration<float>(timeoutSeconds);
  while (true) {
    update(viewer, focus);
    if (isDone(viewer, focus)) {
      return true;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::vector<WorldStreamer::MeshUpdate> WorldStreamer::takeMeshUpdates() {
  std::vector<MeshUpdate> taken;
  taken.swap(meshUpdates);
  return taken;
}

std::vector<WorldStreamer::VegetationUpdate>
WorldStreamer::takeVegetationUpdates() {
  std::vector<VegetationUpdate> taken;
  taken.swap(vegetationUpdates);
  return taken;
}

WorldStreamer::Statistics WorldStreamer::getStatistics() {
  Statistics statistics;
  statistics.chunks = static_cast<int>(chunks.size());
  for (const ChunkState& chunk : chunks) {
    if (chunk.lod >= 0) {
      ++statistics.chunksAtLod[chunk.lod];
    }
    statistics.collisionBodies += chunk.body != physics::INVALID_BODY ? 1 : 0;
  }
  for (const ColumnState& column : columns) {
    statistics.collisionBodies += column.body != physics::INVALID_BODY ? 1 : 0;
    statistics.smallColumns += column.smallLoaded ? 1 : 0;
  }
  statistics.pendingJobs = inFlight;
  return statistics;
}

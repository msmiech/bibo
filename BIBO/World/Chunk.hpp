#ifndef BIBO_WORLD_CHUNK_HPP_
#define BIBO_WORLD_CHUNK_HPP_
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

#include "WorldGenerator.hpp"

namespace bibo {
namespace world {
/**
 * The world is partitioned into cubes of kChunkSize: every chunk is meshed on
 * its own, in a level of detail that depends on its distance from the camera.
 * Level 0 has kChunkCells voxels per axis (1 unit each), every further level
 * half as many.
 */
constexpr float kChunkSize = 32.0f;
constexpr int kChunkCells = 32;
constexpr int kLodCount = 4;

struct ChunkKey {
  int x = 0, y = 0, z = 0;
  bool operator==(const ChunkKey&) const = default;
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey& key) const {
    return static_cast<std::size_t>(key.x * 73856093) ^
           static_cast<std::size_t>(key.y * 19349663) ^
           static_cast<std::size_t>(key.z * 83492791);
  }
};

inline Box ChunkBox(const ChunkKey& key) {
  Box box;
  box.minimum = glm::vec3(static_cast<float>(key.x), static_cast<float>(key.y),
                          static_cast<float>(key.z)) *
                kChunkSize;
  box.maximum = box.minimum + glm::vec3(kChunkSize);
  return box;
}

inline ChunkKey ChunkAt(glm::vec3 position) {
  return {static_cast<int>(std::floor(position.x / kChunkSize)),
          static_cast<int>(std::floor(position.y / kChunkSize)),
          static_cast<int>(std::floor(position.z / kChunkSize))};
}

/**
 * Vertex of the terrain (32 bytes): position, normal and what the ground is
 * covered with
 */
struct TerrainVertex {
  glm::vec3 position;
  glm::vec3 normal;
  // trail, sand, bare rock (cave, cliff), snow from 0 to 255
  std::uint8_t material[4];
  // ambient occlusion: 0 in a closed cave, 255 on open ground
  std::uint8_t sky_visibility;
  std::uint8_t padding[3];
};

/**
 * The surface in a chunk as triangles. The skirts (after surface_index_count)
 * hang from the border of the surface into the ground: they hide the cracks to
 * neighbors with another level of detail.
 */
struct ChunkMesh {
  ChunkKey key;
  int lod = 0;
  std::vector<TerrainVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::size_t surface_index_count = 0;
  Box bounds;
  bool Empty() const { return surface_index_count == 0; }
};

/**
 * Extracts the surface of the density field in a chunk with Surface Nets: one
 * vertex in every voxel that the surface goes through (placed on the surface),
 * a quad for every voxel edge it crosses. Neighboring chunks with the same
 * level of detail fit together exactly (the voxels at the border are computed
 * the same way in both).
 */
ChunkMesh MeshChunk(const WorldGenerator& world, const ChunkKey& key, int lod);
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_CHUNK_HPP_

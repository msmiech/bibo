#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "Chunk.hpp"

using bibo::world::ChunkMesh;
using bibo::world::WorldGenerator;

namespace {
// The 12 edges of a voxel as pairs of its corners (corner bits: 1 = +x, 2 = +y,
// 4 = +z)
const int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7},   // along x
                           {0, 2}, {1, 3}, {4, 6}, {5, 7},   // along y
                           {0, 4}, {1, 5}, {2, 6}, {3, 7}};  // along z

std::uint8_t ToByte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f +
                                   0.5f);
}

glm::vec3 DensityGradient(const WorldGenerator& world, glm::vec3 position,
                          float epsilon) {
  return glm::vec3(world.Density(position + glm::vec3(epsilon, 0, 0)) -
                       world.Density(position - glm::vec3(epsilon, 0, 0)),
                   world.Density(position + glm::vec3(0, epsilon, 0)) -
                       world.Density(position - glm::vec3(0, epsilon, 0)),
                   world.Density(position + glm::vec3(0, 0, epsilon)) -
                       world.Density(position - glm::vec3(0, 0, epsilon))) /
         (2.0f * epsilon);
}
}  // namespace

ChunkMesh bibo::world::MeshChunk(const WorldGenerator& world,
                                 const ChunkKey& key, int lod) {
  ChunkMesh mesh;
  mesh.key = key;
  mesh.lod = lod;
  const int n = kChunkCells >> lod;              // voxels per axis
  const float h = static_cast<float>(1 << lod);  // size of a voxel
  Box box = ChunkBox(key);
  box.minimum -= glm::vec3(2.0f * h);
  box.maximum += glm::vec3(2.0f * h);
  if (!world.MayContainSurface(box)) {
    return mesh;
  }

  // Densities at the corners of the voxels: one more layer on every side than
  // the chunk has, so that the voxels at the border can be computed like the
  // neighbor does. Global sample index g is at g * h (exact, the same in every
  // chunk).
  const int s = n + 3;
  const glm::ivec3 first = glm::ivec3(key.x, key.y, key.z) * n - glm::ivec3(1);
  std::vector<float> densities(static_cast<std::size_t>(s) * s * s);
  auto density = [&](int i, int j, int k) -> float& {
    return densities[(static_cast<std::size_t>(k) * s + i) * s + j];
  };
  bool any_solid = false, any_air = false;
  for (int k = 0; k < s; ++k) {
    for (int i = 0; i < s; ++i) {
      float* column = &density(i, 0, k);
      world.DensityColumn(static_cast<float>(first.x + i) * h,
                          static_cast<float>(first.z + k) * h, first.y, h, s,
                          column);
      for (int j = 0; j < s; ++j) {
        (column[j] > 0.0f ? any_solid : any_air) = true;
      }
    }
  }
  if (!any_solid || !any_air) {
    return mesh;
  }
  auto sample_position = [&](int i, int j, int k) {
    return glm::vec3(static_cast<float>(first.x + i),
                     static_cast<float>(first.y + j),
                     static_cast<float>(first.z + k)) *
           h;
  };

  // 1. A vertex in every voxel with corners inside and outside: the mean of the
  // points where its edges cross the surface, moved onto the surface (one
  // Newton step) and kept inside of its voxel. The quads (2.) only use the
  // voxels 0 to n.
  const int voxels = n + 1;  // voxels per axis
  std::vector<int> voxel_vertex(
      static_cast<std::size_t>(voxels) * voxels * voxels, -1);
  auto voxel = [&](int i, int j, int k) -> int& {
    return voxel_vertex[(static_cast<std::size_t>(k) * voxels + j) * voxels +
                        i];
  };
  for (int k = 0; k < voxels; ++k) {
    for (int j = 0; j < voxels; ++j) {
      for (int i = 0; i < voxels; ++i) {
        float corners[8];
        int solid = 0;
        for (int c = 0; c < 8; ++c) {
          corners[c] =
              density(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
          solid |= (corners[c] > 0.0f ? 1 : 0) << c;
        }
        if (solid == 0 || solid == 255) {
          continue;
        }
        glm::vec3 sum(0.0f);
        int crossings = 0;
        for (const auto& edge : kEdges) {
          float a = corners[edge[0]], b = corners[edge[1]];
          if ((a > 0.0f) == (b > 0.0f)) {
            continue;
          }
          float t = a / (a - b);
          glm::vec3 pa(edge[0] & 1, (edge[0] >> 1) & 1, (edge[0] >> 2) & 1);
          glm::vec3 pb(edge[1] & 1, (edge[1] >> 1) & 1, (edge[1] >> 2) & 1);
          sum += glm::mix(pa, pb, t);
          ++crossings;
        }
        glm::vec3 low = sample_position(i, j, k);
        glm::vec3 position = low + h * (sum / static_cast<float>(crossings));
        glm::vec3 gradient =
            DensityGradient(world, position, 0.25f * std::min(h, 2.0f));
        float squared = glm::dot(gradient, gradient);
        if (squared > 1e-8f) {
          position -= gradient * (world.Density(position) / squared);
          position = glm::clamp(position, low - glm::vec3(0.25f * h),
                                low + glm::vec3(1.25f * h));
        }
        TerrainVertex vertex{};
        vertex.position = position;
        voxel(i, j, k) = static_cast<int>(mesh.vertices.size());
        mesh.vertices.push_back(vertex);
      }
    }
  }

  // 2. A quad for every voxel edge that crosses the surface. The chunk makes
  // the quads of the edges that start in it (samples 0 to n - 1, local index +
  // 1), its neighbors the others.
  for (int k = 1; k <= n; ++k) {
    for (int j = 1; j <= n; ++j) {
      for (int i = 1; i <= n; ++i) {
        const int start[3] = {i, j, k};
        bool start_solid = density(i, j, k) > 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
          int end[3] = {i, j, k};
          ++end[axis];
          if (start_solid == (density(end[0], end[1], end[2]) > 0.0f)) {
            continue;
          }
          // The 4 voxels around the edge, counterclockwise around the axis (u x
          // v = axis)
          int u = (axis + 1) % 3, v = (axis + 2) % 3;
          int quad[4];
          const int du[4] = {-1, 0, 0, -1}, dv[4] = {-1, -1, 0, 0};
          bool complete = true;
          for (int q = 0; q < 4; ++q) {
            int cell[3] = {start[0], start[1], start[2]};
            cell[u] += du[q];
            cell[v] += dv[q];
            quad[q] = voxel(cell[0], cell[1], cell[2]);
            complete = complete && quad[q] >= 0;
          }
          if (!complete) {
            continue;
          }
          if (!start_solid) {
            // The ground is on the far side: the surface faces the other way
            std::swap(quad[1], quad[3]);
          }
          const glm::vec3& p0 = mesh.vertices[quad[0]].position;
          const glm::vec3& p1 = mesh.vertices[quad[1]].position;
          const glm::vec3& p2 = mesh.vertices[quad[2]].position;
          const glm::vec3& p3 = mesh.vertices[quad[3]].position;
          // Split along the shorter diagonal
          if (glm::distance(p0, p2) <= glm::distance(p1, p3)) {
            mesh.indices.insert(mesh.indices.end(),
                                {static_cast<std::uint32_t>(quad[0]),
                                 static_cast<std::uint32_t>(quad[1]),
                                 static_cast<std::uint32_t>(quad[2]),
                                 static_cast<std::uint32_t>(quad[0]),
                                 static_cast<std::uint32_t>(quad[2]),
                                 static_cast<std::uint32_t>(quad[3])});
          } else {
            mesh.indices.insert(mesh.indices.end(),
                                {static_cast<std::uint32_t>(quad[0]),
                                 static_cast<std::uint32_t>(quad[1]),
                                 static_cast<std::uint32_t>(quad[3]),
                                 static_cast<std::uint32_t>(quad[1]),
                                 static_cast<std::uint32_t>(quad[2]),
                                 static_cast<std::uint32_t>(quad[3])});
          }
        }
      }
    }
  }
  mesh.surface_index_count = mesh.indices.size();
  if (mesh.indices.empty()) {
    mesh.vertices.clear();
    return mesh;
  }

  // 3. Only the vertices of the quads are kept, with their normal and material
  std::vector<int> remap(mesh.vertices.size(), -1);
  std::vector<TerrainVertex> used;
  used.reserve(mesh.vertices.size());
  for (std::uint32_t& index : mesh.indices) {
    if (remap[index] < 0) {
      remap[index] = static_cast<int>(used.size());
      used.push_back(mesh.vertices[index]);
    }
    index = static_cast<std::uint32_t>(remap[index]);
  }
  mesh.vertices.swap(used);
  for (TerrainVertex& vertex : mesh.vertices) {
    glm::vec3 normal = world.Normal(vertex.position);
    vertex.normal = normal;
    SurfaceMaterial material = world.Material(vertex.position, normal);
    vertex.material[1] = ToByte(material.sand);
    // In a cave the walls and the roof are rock, the floor is the trail
    float floor = glm::smoothstep(0.55f, 0.8f, normal.y);
    vertex.material[2] =
        ToByte(std::max(material.cave * (1.0f - floor), 0.8f * material.cliff));
    vertex.material[0] =
        ToByte(std::max(material.trail, material.cave * floor));
    float visibility = world.SkyVisibility(vertex.position, normal);
    vertex.material[3] =
        ToByte(world.SnowCover(vertex.position, normal, visibility, material));
    vertex.sky_visibility = ToByte(visibility);
  }

  // 4. Skirts: the edges at the border of the surface (used by only one
  // triangle) are extended into the ground
  std::unordered_map<std::uint64_t, int> edge_use;
  auto edge_key = [](std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
  };
  for (std::size_t t = 0; t < mesh.surface_index_count; t += 3) {
    for (int e = 0; e < 3; ++e) {
      ++edge_use[edge_key(mesh.indices[t + e], mesh.indices[t + (e + 1) % 3])];
    }
  }
  const float skirt_depth = 1.5f * h + 0.5f;
  std::unordered_map<std::uint32_t, std::uint32_t> lowered;
  auto lower = [&](std::uint32_t index) {
    auto found = lowered.find(index);
    if (found != lowered.end()) {
      return found->second;
    }
    TerrainVertex vertex = mesh.vertices[index];
    vertex.position -= vertex.normal * skirt_depth;
    std::uint32_t added = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(vertex);
    lowered[index] = added;
    return added;
  };
  for (std::size_t t = 0; t < mesh.surface_index_count; t += 3) {
    for (int e = 0; e < 3; ++e) {
      std::uint32_t a = mesh.indices[t + e], b = mesh.indices[t + (e + 1) % 3];
      if (edge_use[edge_key(a, b)] != 1) {
        continue;
      }
      // Continues the surface over the edge (reversed edge), folded down into
      // the ground
      std::uint32_t low_a = lower(a), low_b = lower(b);
      mesh.indices.insert(mesh.indices.end(), {b, a, low_a, b, low_a, low_b});
    }
  }

  mesh.bounds.minimum = glm::vec3(std::numeric_limits<float>::max());
  mesh.bounds.maximum = glm::vec3(std::numeric_limits<float>::lowest());
  for (const TerrainVertex& vertex : mesh.vertices) {
    mesh.bounds.minimum = glm::min(mesh.bounds.minimum, vertex.position);
    mesh.bounds.maximum = glm::max(mesh.bounds.maximum, vertex.position);
  }
  return mesh;
}

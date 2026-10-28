#include <assimp/scene.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Physics/Physics.hpp"
#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"
#include "World/Chunk.hpp"
#include "World/Landmarks.hpp"
#include "World/TrackPath.hpp"
#include "World/TrackTerrain.hpp"
#include "World/Vegetation.hpp"
#include "World/WorldGenerator.hpp"
#include "World/WorldStreamer.hpp"

using bibo::test::NodeModelMatrix;
using bibo::test::SharedWorldGenerator;
using bibo::test::TrackNode;
using bibo::test::TrackScene;
using bibo::world::ChunkAt;
using bibo::world::ChunkKey;
using bibo::world::ChunkKeyHash;
using bibo::world::ChunkMesh;
using bibo::world::ColumnKey;
using bibo::world::FootPath;
using bibo::world::Instance;
using bibo::world::kLodCount;
using bibo::world::Landmarks;
using bibo::world::PointOfInterest;
using bibo::world::SurfaceMaterial;
using bibo::world::TerrainVertex;
using bibo::world::TrackPath;
using bibo::world::TrackTerrain;
using bibo::world::WorldGenerator;
using bibo::world::WorldStreamer;

namespace rules = bibo::scene::rules;
using bibo::world::Box;
using bibo::world::Cave;
using bibo::world::Layer;
using bibo::world::Model;
using glm::vec2;
using glm::vec3;

namespace {
vec3 StartPosition() { return vec3(NodeModelMatrix(TrackNode("BearBody"))[3]); }

vec2 Ground(vec3 position) { return vec2(position.x, position.z); }

}  // namespace

TEST_CASE("The height field of the track has the height of the terrain mesh",
          "[assets][world]") {
  std::vector<glm::vec3> vertices;
  std::vector<unsigned int> indices;
  REQUIRE(TrackTerrain::LoadNodeMesh(TrackScene(), "Boden", vertices, indices));
  const TrackTerrain& terrain = SharedWorldGenerator().GetTrack();
  // At the vertices of the mesh (where the triangles meet at an angle, the
  // height between the cells is a bit rounded). A few walls of the mesh are
  // vertical: a height field only has their top.
  int checked = 0, off = 0;
  for (const glm::vec3& vertex : vertices) {
    if (terrain.DistanceInside(Ground(vertex)) > 1.0f) {
      ++checked;
      off += std::abs(terrain.Height(Ground(vertex)) - vertex.y) > 1.0f ? 1 : 0;
    }
  }
  CAPTURE(checked, off);
  REQUIRE(checked > 1000);
  CHECK(off < checked / 50);
  // In the middle of triangles it is the plane of the triangle
  float sum_of_differences = 0.0f;
  int samples = 0;
  for (std::size_t t = 0; t + 2 < indices.size(); t += 3 * 7) {
    glm::vec3 middle = (vertices[indices[t]] + vertices[indices[t + 1]] +
                        vertices[indices[t + 2]]) /
                       3.0f;
    if (terrain.DistanceInside(Ground(middle)) > 1.0f) {
      sum_of_differences += std::abs(terrain.Height(Ground(middle)) - middle.y);
      ++samples;
    }
  }
  REQUIRE(samples > 100);
  CHECK(sum_of_differences / static_cast<float>(samples) < 0.05f);
}

TEST_CASE(
    "The path of the track goes around the lap on the floor of the canyon",
    "[assets][world]") {
  const TrackPath& path = SharedWorldGenerator().GetPath();
  const TrackTerrain& terrain = SharedWorldGenerator().GetTrack();
  const std::vector<TrackPath::Sample>& samples = path.GetSamples();
  REQUIRE(samples.size() > 300);
  CHECK(path.GetLength() > 400.0f);
  CHECK(path.GetLength() < 900.0f);

  SECTION(
      "It starts at the start and goes in the driving direction (along -z)") {
    CHECK(glm::distance(Ground(samples[0].position), Ground(StartPosition())) <
          3.0f);
    CHECK(samples[0].tangent.y < -0.8f);
  }

  SECTION("It goes through all checkpoints in their order") {
    float previous = 0.0f;
    for (std::size_t i = 0; i < rules::kCheckpointCount; ++i) {
      TrackPath::Nearest nearest = path.FindNearest(rules::kCheckpoints[i]);
      CAPTURE(i, nearest.distance, nearest.lateral);
      REQUIRE(nearest.found);
      CHECK(std::abs(nearest.lateral) < rules::kCheckpointRadius);
      CHECK(nearest.distance > previous);
      previous = nearest.distance;
    }
  }

  SECTION(
      "The samples are evenly spaced, on the floor (the lowest point across "
      "the canyon) and in its middle") {
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const TrackPath::Sample& sample = samples[i];
      const TrackPath::Sample& next = samples[(i + 1) % samples.size()];
      CAPTURE(i, sample.position, sample.floor_left, sample.floor_right);
      REQUIRE(glm::distance(Ground(sample.position), Ground(next.position)) <
              1.5f);
      CHECK(sample.position.y == terrain.Height(Ground(sample.position)));
      CHECK(sample.floor_left > 1.5f);
      CHECK(sample.floor_right > 1.5f);
      // The walls are higher than the floor on both sides
      CHECK(sample.wall_left > 4.0f);
      CHECK(sample.wall_right > 4.0f);
    }
  }

  SECTION("The nearest point of the path is found from the side") {
    for (std::size_t i = 0; i < samples.size(); i += 17) {
      const TrackPath::Sample& sample = samples[i];
      vec2 right(-sample.tangent.y, sample.tangent.x);
      TrackPath::Nearest nearest =
          path.FindNearest(Ground(sample.position) + right * 3.0f);
      CAPTURE(i);
      REQUIRE(nearest.found);
      CHECK(std::abs(nearest.lateral - 3.0f) < 0.3f);
      CHECK(std::abs(nearest.distance - sample.distance) < 1.0f);
    }
    CHECK_FALSE(path.FindNearest(vec2(5000.0f, 5000.0f)).found);
  }
}

TEST_CASE("The world keeps the track and puts an island around it",
          "[assets][world]") {
  const WorldGenerator& world = SharedWorldGenerator();
  const TrackTerrain& track = world.GetTrack();
  const TrackPath& path = world.GetPath();

  SECTION("On the floor of the track the ground is the track terrain") {
    for (const TrackPath::Sample& sample : path.GetSamples()) {
      vec2 right(-sample.tangent.y, sample.tangent.x);
      for (float lateral : {-1.0f, 0.0f, 1.0f}) {
        vec2 point =
            Ground(sample.position) +
            right * (lateral * std::min(sample.floor_left, sample.floor_right));
        REQUIRE(world.Height(point) == track.Height(point));
      }
    }
  }

  SECTION("Far from the track it is open sea, close to it land") {
    vec2 center = world.GetCenter();
    for (int i = 0; i < 16; ++i) {
      float angle = static_cast<float>(i) * 6.2831853f / 16.0f;
      vec2 direction(std::cos(angle), std::sin(angle));
      CAPTURE(angle);
      CHECK(world.Height(center + direction * 340.0f) <
            world.GetSeaLevel() - 3.0f);
      CHECK(world.Height(center + direction * 185.0f) >
            world.GetSeaLevel() + 2.0f);
      float inland, cliff;
      world.Coast(center + direction * 400.0f, inland, cliff);
      CHECK(inland < 0.0f);
    }
  }

  SECTION("The coast has beaches and cliffs") {
    int beaches = 0, cliffs = 0;
    for (int i = 0; i < 360; ++i) {
      float angle = static_cast<float>(i) * 6.2831853f / 360.0f;
      float inland, cliff;
      world.Coast(
          world.GetCenter() + vec2(std::cos(angle), std::sin(angle)) * 260.0f,
          inland, cliff);
      beaches += cliff < 0.2f ? 1 : 0;
      cliffs += cliff > 0.8f ? 1 : 0;
    }
    CHECK(beaches > 30);
    CHECK(cliffs > 30);
  }

  SECTION("The density is positive in the ground and negative in the air") {
    for (const TrackPath::Sample& sample : path.GetSamples()) {
      if (world.IsInCave(sample.position + vec3(0.0f, 1.0f, 0.0f))) {
        continue;
      }
      REQUIRE(world.Density(sample.position - vec3(0.0f, 0.5f, 0.0f)) > 0.0f);
      REQUIRE(world.Density(sample.position + vec3(0.0f, 0.5f, 0.0f)) < 0.0f);
    }
  }

  SECTION("densityColumn gives exactly the values of density") {
    float column[40];
    for (vec2 point :
         {vec2(10.0f, 20.0f), Ground(path.GetSamples()[0].position),
          vec2(-150.0f, 160.0f)}) {
      world.DensityColumn(point.x, point.y, -10, 1.0f, 40, column);
      for (int i = 0; i < 40; ++i) {
        REQUIRE(
            column[i] ==
            world.Density(vec3(point.x, static_cast<float>(i - 10), point.y)));
      }
    }
  }
}

TEST_CASE("Caves cover parts of the track and leave room on the floor",
          "[assets][world]") {
  const WorldGenerator& world = SharedWorldGenerator();
  const TrackPath& path = world.GetPath();
  REQUIRE(world.GetCaves().size() >= 1);
  for (const Cave& cave : world.GetCaves()) {
    CAPTURE(cave.start, cave.end);
    CHECK(cave.end - cave.start > 30.0f);
    // Not at the start and finish
    CHECK(cave.start > 40.0f);
    CHECK(cave.end < path.GetLength() - 40.0f);
    for (float distance = cave.start + 4.0f; distance < cave.end - 4.0f;
         distance += 2.0f) {
      TrackPath::Sample sample = path.At(distance);
      CAPTURE(distance, sample.position);
      // Air above the trail for the bear and the camera
      for (float height = 0.5f; height < 6.0f; height += 0.5f) {
        REQUIRE(world.Density(sample.position + vec3(0.0f, height, 0.0f)) <
                0.0f);
      }
      CHECK(world.IsInCave(sample.position + vec3(0.0f, 1.0f, 0.0f)));
      // Rock above (the roof follows the floor, but not down into the puddles)
      float floor = std::max(sample.position.y, cave.floor_height - 1.5f);
      CHECK(world.Density(vec3(sample.position.x,
                               floor + cave.ceiling_height + 3.0f,
                               sample.position.z)) > 0.0f);
      CHECK(world.CaveRoofTop(Ground(sample.position)) >
            floor + cave.ceiling_height);
    }
  }
  // Not under a roof outside of the caves
  CHECK_FALSE(world.IsInCave(StartPosition()));
  CHECK(world.CaveRoofTop(Ground(StartPosition())) < -1000.0f);
}

TEST_CASE(
    "The trail goes around the pools where the floor has room, through the "
    "ones that fill it",
    "[assets][world]") {
  const WorldGenerator& world = SharedWorldGenerator();
  const TrackTerrain& track = world.GetTrack();
  const float sea = world.GetSeaLevel();
  auto dry = [&](const TrackPath::Sample& sample, float offset) {
    vec2 right(-sample.tangent.y, sample.tangent.x);
    for (float lateral = offset - TrackPath::kTrailHalfWidth;
         lateral <= offset + TrackPath::kTrailHalfWidth; lateral += 0.25f) {
      if (track.Height(Ground(sample.position) + right * lateral) <
          sea + 0.05f) {
        return false;
      }
    }
    return true;
  };
  int wet = 0, around_pools = 0;
  bool in_pool = false, fills_floor = false;
  const std::vector<TrackPath::Sample>& samples = world.GetPath().GetSamples();
  for (const TrackPath::Sample& sample : samples) {
    CAPTURE(sample.distance, sample.position, sample.trail_offset,
            sample.floor_left, sample.floor_right);
    // On the floor (it may reach a bit over its edge)
    CHECK(sample.trail_offset - TrackPath::kTrailHalfWidth >
          -sample.floor_left - 1.6f);
    CHECK(sample.trail_offset + TrackPath::kTrailHalfWidth <
          sample.floor_right + 1.6f);
    if (dry(sample, sample.trail_offset)) {
      // A stretch of the trail in the water only crosses a pool that fills the
      // floor somewhere
      CHECK((!in_pool || fills_floor));
      in_pool = fills_floor = false;
      bool next = false;
      for (float lateral = -1.0f; lateral <= 1.0f; lateral += 0.5f) {
        next = next || !dry(sample, lateral);
      }
      around_pools += next ? 1 : 0;  // the middle of the floor is wet
      continue;
    }
    ++wet;
    in_pool = true;
    bool room = false;
    for (float offset = -sample.floor_left + TrackPath::kTrailHalfWidth;
         offset <= sample.floor_right - TrackPath::kTrailHalfWidth;
         offset += 0.25f) {
      room = room || dry(sample, offset);
    }
    fills_floor = fills_floor || !room;
  }
  CHECK(wet > 0);  // the race jumps over pools
  CHECK(around_pools > 10);
}

TEST_CASE(
    "Foot paths lead from the race track to the points of interest: beaches, a "
    "summit, a camp, standing stones, a cliff",
    "[assets][world]") {
  const WorldGenerator& world = SharedWorldGenerator();
  const Landmarks& landmarks = world.GetLandmarks();
  const std::vector<PointOfInterest>& points = landmarks.GetPointsOfInterest();
  const TrackPath& track = world.GetPath();
  const float sea = world.GetSeaLevel();

  SECTION(
      "There are beaches on different sides of the island and one of each "
      "other kind") {
    std::vector<float> beaches;
    int summits = 0, camps = 0, stones = 0, cliffs = 0;
    for (const PointOfInterest& point : points) {
      CAPTURE(point.position, static_cast<int>(point.kind));
      vec2 offset = Ground(point.position) - world.GetCenter();
      float inland, cliff;
      world.Coast(Ground(point.position), inland, cliff);
      CHECK(point.position.y > sea + 0.3f);
      CHECK(inland > 0.0f);
      switch (point.kind) {
        case PointOfInterest::Kind::kBeach:
          beaches.push_back(std::atan2(offset.y, offset.x));
          CHECK(inland < 20.0f);
          CHECK(world.Material(point.position, vec3(0.0f, 1.0f, 0.0f)).sand >
                0.5f);
          break;
        case PointOfInterest::Kind::kSummit:
          ++summits;
          CHECK(point.position.y > 40.0f);
          break;
        case PointOfInterest::Kind::kCamp:
          ++camps;
          break;
        case PointOfInterest::Kind::kStandingStones:
          ++stones;
          break;
        case PointOfInterest::Kind::kCliffView:
          ++cliffs;
          CHECK(cliff > 0.8f);
          CHECK(point.position.y > sea + 8.0f);
          break;
      }
    }
    CHECK(summits == 1);
    CHECK(camps == 1);
    CHECK(stones == 1);
    CHECK(cliffs == 1);
    REQUIRE(beaches.size() >= 3);
    for (std::size_t i = 0; i < beaches.size(); ++i) {
      for (std::size_t j = i + 1; j < beaches.size(); ++j) {
        float apart = std::fmod(std::abs(beaches[i] - beaches[j]), 6.2831853f);
        CHECK(std::min(apart, 6.2831853f - apart) > glm::radians(50.0f));
      }
    }
  }

  SECTION(
      "Every point of interest has a path that starts at the race track or "
      "branches off from another path") {
    std::vector<int> paths_to(points.size(), 0);
    const std::vector<FootPath>& paths = landmarks.GetPaths();
    int from_track = 0;
    for (std::size_t p = 0; p < paths.size(); ++p) {
      const FootPath& path = paths[p];
      REQUIRE(path.points.size() > 20);
      ++paths_to[path.point_of_interest];
      CHECK(glm::distance(Ground(path.points.back()),
                          Ground(points[path.point_of_interest].position)) <
            0.5f);
      vec2 start = Ground(path.points.front());
      if (path.from_track) {
        ++from_track;
        TrackPath::Nearest nearest = track.FindNearest(start);
        REQUIRE(nearest.found);
        CHECK(std::abs(nearest.lateral) < nearest.floor_half_width);
        CHECK(world.CaveRoofTop(start) < -1000.0f);
      } else {
        float closest = 1e9f;
        for (std::size_t other = 0; other < p; ++other) {
          for (const vec3& point : paths[other].points) {
            closest = std::min(closest, glm::distance(Ground(point), start));
          }
        }
        CHECK(closest < 0.01f);
      }
    }
    // where the side of the canyon is gentle, the others branch off
    CHECK(from_track >= 1);
    for (std::size_t i = 0; i < points.size(); ++i) {
      CAPTURE(i);
      CHECK(paths_to[i] == 1);
    }
  }

  SECTION(
      "The paths are cut into the ground: not steep along them, walkable "
      "across, out of the water, trodden earth") {
    for (const FootPath& path : landmarks.GetPaths()) {
      for (std::size_t i = 1; i < path.points.size(); ++i) {
        const vec3& point = path.points[i];
        CAPTURE(point);
        float step = glm::distance(Ground(point), Ground(path.points[i - 1]));
        REQUIRE(step < 1.3f);
        CHECK(std::abs(point.y - path.points[i - 1].y) <=
              Landmarks::kMaxGrade * step + 0.05f);
        TrackPath::Nearest nearest = track.FindNearest(Ground(point));
        if (nearest.found &&
            std::abs(nearest.lateral) < nearest.floor_half_width + 4.0f) {
          // where it comes out of the canyon of the track, the floor is not
          // changed
          continue;
        }
        vec3 surface, normal;
        REQUIRE(world.Surface(Ground(point), surface, normal));
        CHECK(std::abs(surface.y - point.y) < 0.2f);
        CHECK(surface.y > sea);
        CHECK(normal.y > 0.75f);
        SurfaceMaterial material = world.Material(surface, normal);
        CHECK(material.trail + material.sand > 0.5f);
      }
    }
  }

  SECTION(
      "The paths climb gently, also out of the canyon of the track, and do not "
      "run along crags") {
    for (const FootPath& path : landmarks.GetPaths()) {
      for (std::size_t i = 1; i < path.points.size(); ++i) {
        vec2 from = Ground(path.points[i - 1]), to = Ground(path.points[i]);
        CAPTURE(to);
        TrackPath::Nearest nearest = track.FindNearest(to);
        if (nearest.found &&
            std::abs(nearest.lateral) < nearest.floor_half_width) {
          continue;  // on the floor of the track
        }
        // The ground the bear walks on (not only the middle line of the path).
        // Right at the edge of the floor the side of the canyon is not cut (the
        // track terrain), it may be steeper there, but far less than the bear
        // can climb.
        bool edge = nearest.found &&
                    std::abs(nearest.lateral) < nearest.floor_half_width + 2.5f;
        CHECK(std::abs(world.Height(to) - world.Height(from)) <=
              (edge ? 0.75f : 0.5f) * glm::distance(from, to) + 0.02f);
        // Beside the path: no walls of rock
        vec2 along = glm::normalize(to - from), side(-along.y, along.x);
        float steepest = 0.0f;
        for (float sign : {-1.0f, 1.0f}) {
          for (float distance = 1.5f; distance < 5.5f; distance += 0.5f) {
            float h0 = world.Height(to + side * (sign * distance)),
                  h1 = world.Height(to + side * (sign * (distance + 0.5f)));
            steepest = std::max(steepest, std::abs(h1 - h0) / 0.5f);
          }
        }
        CHECK(steepest < 2.6f);
      }
    }
  }

  SECTION(
      "Nothing grows on the paths and in the places, the places have their "
      "stones and logs") {
    std::set<std::pair<int, int>> keys;
    for (const FootPath& path : landmarks.GetPaths()) {
      for (std::size_t i = 0; i < path.points.size(); i += 6) {
        ChunkKey key = ChunkAt(path.points[i]);
        keys.insert({key.x, key.z});
      }
      ChunkKey end = ChunkAt(path.points.back());
      keys.insert({end.x, end.z});
    }
    std::vector<int> props(points.size(), 0);
    for (const auto& key : keys) {
      std::vector<bibo::physics::ShapePart> collision;
      std::vector<Instance> large =
          Scatter(world, {key.first, key.second}, Layer::kLarge, &collision);
      int solid = 0;
      for (const Instance& instance : large) {
        vec3 position(instance.position_scale);
        CAPTURE(position, static_cast<int>(instance.GetModel()));
        bool plant = instance.GetModel() <= Model::kJuniper;
        solid += instance.GetModel() <= Model::kPine ||
                         instance.GetModel() >= Model::kStoneRound
                     ? 1
                     : 0;
        if (plant) {
          CHECK(landmarks.PathDistance(Ground(position)) >
                Landmarks::kPathHalfWidth + 0.5f);
          CHECK(landmarks.PointAt(Ground(position)) < 0);
        } else if (landmarks.PointAt(Ground(position)) >= 0) {
          ++props[landmarks.PointAt(Ground(position))];
        }
      }
      CHECK(collision.size() == static_cast<std::size_t>(solid));
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
      CAPTURE(i, static_cast<int>(points[i].kind));
      CHECK(props[i] >= 3);
    }
  }
}

TEST_CASE("The chunks of the world fit together", "[assets][world]") {
  const WorldGenerator& world = SharedWorldGenerator();
  // A chunk on the track (with the start) and its neighbor along x
  const ChunkKey key = ChunkAt(StartPosition());
  const ChunkKey neighbor{key.x + 1, key.y, key.z};
  ChunkMesh mesh = MeshChunk(world, key, 0);
  ChunkMesh next = MeshChunk(world, neighbor, 0);
  REQUIRE_FALSE(mesh.Empty());
  REQUIRE_FALSE(next.Empty());

  SECTION("The vertices are on the surface, their normals point into the air") {
    for (std::size_t i = 0; i < mesh.surface_index_count; ++i) {
      const TerrainVertex& vertex = mesh.vertices[mesh.indices[i]];
      CAPTURE(vertex.position);
      // Distance to the surface: the density divided by its gradient (steep at
      // the walls of the track)
      const float e = 0.05f;
      glm::vec3 gradient(world.Density(vertex.position + vec3(e, 0, 0)) -
                             world.Density(vertex.position - vec3(e, 0, 0)),
                         world.Density(vertex.position + vec3(0, e, 0)) -
                             world.Density(vertex.position - vec3(0, e, 0)),
                         world.Density(vertex.position + vec3(0, 0, e)) -
                             world.Density(vertex.position - vec3(0, 0, e)));
      float distance = std::abs(world.Density(vertex.position)) /
                       std::max(glm::length(gradient) / (2.0f * e), 1e-3f);
      REQUIRE(distance < 0.25f);  // a voxel is 1 unit
      REQUIRE(world.Density(vertex.position + vertex.normal * 0.3f) < 0.0f);
    }
  }

  SECTION("Neighbors fit together without cracks") {
    // Both chunks compute the voxels at their common border the same way: the
    // vertices are exactly the same, the surfaces of the two chunks together
    // have no open edges along the border
    std::map<std::tuple<float, float, float>, int> ids;
    auto id = [&ids](const glm::vec3& p) {
      return ids
          .emplace(std::make_tuple(p.x, p.y, p.z), static_cast<int>(ids.size()))
          .first->second;
    };
    std::map<std::pair<int, int>, int> edges;
    for (const ChunkMesh* part : {&mesh, &next}) {
      for (std::size_t t = 0; t < part->surface_index_count; t += 3) {
        for (int e = 0; e < 3; ++e) {
          int a = id(part->vertices[part->indices[t + e]].position),
              b = id(part->vertices[part->indices[t + (e + 1) % 3]].position);
          ++edges[{std::min(a, b), std::max(a, b)}];
        }
      }
    }
    std::vector<glm::vec3> positions(ids.size());
    for (const auto& entry : ids) {
      positions[entry.second] =
          glm::vec3(std::get<0>(entry.first), std::get<1>(entry.first),
                    std::get<2>(entry.first));
    }
    float border = ChunkBox(neighbor).minimum.x;
    Box box = ChunkBox(key);
    auto near_border = [&](const glm::vec3& p) {
      // Away from the other sides of the two chunks, where the surface is open
      return std::abs(p.x - border) < 2.0f && p.y > box.minimum.y + 2.0f &&
             p.y < box.maximum.y - 2.0f && p.z > box.minimum.z + 2.0f &&
             p.z < box.maximum.z - 2.0f;
    };
    int bordering = 0;
    for (const auto& edge : edges) {
      const glm::vec3& a = positions[edge.first.first];
      const glm::vec3& b = positions[edge.first.second];
      if (near_border(a) && near_border(b)) {
        ++bordering;
        CAPTURE(a, b);
        CHECK(edge.second == 2);
      }
    }
    CHECK(bordering > 20);
  }

  SECTION(
      "Every edge of the surface belongs to at most two triangles, the open "
      "ones get skirts") {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (std::size_t t = 0; t < mesh.surface_index_count; t += 3) {
      for (int e = 0; e < 3; ++e) {
        std::uint32_t a = mesh.indices[t + e],
                      b = mesh.indices[t + (e + 1) % 3];
        ++edges[{std::min(a, b), std::max(a, b)}];
      }
    }
    int open = 0;
    for (const auto& edge : edges) {
      REQUIRE(edge.second <= 2);
      open += edge.second == 1 ? 1 : 0;
    }
    CHECK(open > 0);
    CHECK(mesh.indices.size() - mesh.surface_index_count ==
          static_cast<std::size_t>(open) * 6);
  }

  SECTION("Coarser levels of detail have fewer triangles, but the same shape") {
    std::size_t previous = mesh.indices.size();
    for (int lod = 1; lod < kLodCount; ++lod) {
      ChunkMesh coarse = MeshChunk(world, key, lod);
      CAPTURE(lod);
      REQUIRE_FALSE(coarse.Empty());
      CHECK(coarse.indices.size() < previous);
      previous = coarse.indices.size();
      // Its vertices are on the surface too (they are moved onto it)
      int on_surface = 0;
      for (const TerrainVertex& vertex : coarse.vertices) {
        on_surface += std::abs(world.Density(vertex.position)) < 0.5f ? 1 : 0;
      }
      // the others are the skirts
      CHECK(on_surface >= static_cast<int>(coarse.vertices.size()) / 2);
    }
  }

  SECTION("Chunks high in the air and deep in the ground are empty") {
    CHECK(MeshChunk(world, {key.x, 5, key.z}, 0).Empty());
    CHECK(MeshChunk(world, {key.x, -4, key.z}, 0).Empty());
  }
}

TEST_CASE("Trees grow on the slopes and in the forest, but not on the track",
          "[assets][world][vegetation]") {
  const WorldGenerator& world = SharedWorldGenerator();
  const TrackPath& path = world.GetPath();
  // The columns along the track
  std::set<std::pair<int, int>> keys;
  for (std::size_t i = 0; i < path.GetSamples().size(); i += 8) {
    ChunkKey key = ChunkAt(path.GetSamples()[i].position);
    keys.insert({key.x, key.z});
  }
  int trees_next_to_the_track = 0, glowing_mushrooms = 0;
  for (const auto& key : keys) {
    ColumnKey column{key.first, key.second};
    std::vector<bibo::physics::ShapePart> collision;
    std::vector<Instance> large =
        Scatter(world, column, Layer::kLarge, &collision);
    std::vector<Instance> small = Scatter(world, column, Layer::kSmall);
    // Deterministic
    std::vector<Instance> again = Scatter(world, column, Layer::kLarge);
    REQUIRE(again.size() == large.size());
    for (std::size_t i = 0; i < large.size(); ++i) {
      REQUIRE(again[i].position_scale == large[i].position_scale);
    }
    for (const Instance& instance : large) {
      vec3 position(instance.position_scale);
      TrackPath::Nearest nearest = path.FindNearest(Ground(position));
      bool tree = instance.GetModel() <= Model::kPine;
      CAPTURE(position, static_cast<int>(instance.GetModel()), nearest.lateral,
              nearest.floor_half_width);
      REQUIRE(position.y > world.GetSeaLevel() - 2.0f);
      if (tree && nearest.found) {
        // Not on the floor, where the bear runs
        REQUIRE(std::abs(nearest.lateral) > nearest.floor_half_width + 1.0f);
        trees_next_to_the_track +=
            std::abs(nearest.lateral) < nearest.floor_half_width + 20.0f ? 1
                                                                         : 0;
      }
    }
    for (const Instance& instance : small) {
      vec3 position(instance.position_scale);
      if (instance.GetModel() == Model::kMushroomGlow) {
        ++glowing_mushrooms;
        // only in the caves
        CHECK(world.CaveRoofTop(Ground(position)) > position.y);
      } else {
        TrackPath::Nearest nearest = path.FindNearest(Ground(position));
        bool on_trail = nearest.found &&
                        std::abs(nearest.trail_lateral) <
                            TrackPath::kTrailHalfWidth - 1.0f &&
                        position.y < nearest.floor_height + 1.0f;
        // Pebbles may lie on the trail, nothing else
        bool pebble = instance.GetModel() >= Model::kStoneRound &&
                      instance.GetModel() <= Model::kStoneSharp;
        CHECK((!on_trail || pebble));
      }
    }
    // Every trunk and boulder has its collision shape
    int solid = 0;
    for (const Instance& instance : large) {
      solid += instance.GetModel() <= Model::kPine ||
                       instance.GetModel() >= Model::kStoneRound
                   ? 1
                   : 0;
    }
    CHECK(collision.size() == static_cast<std::size_t>(solid));
  }
  CHECK(trees_next_to_the_track > 300);
  CHECK(glowing_mushrooms > 20);
}

TEST_CASE("The world is streamed with the level of detail of the distance",
          "[assets][world][physics]") {
  bibo::physics::PhysicsWorld physics_world;
  const vec3 start = StartPosition();
  WorldStreamer::Settings settings;
  auto streamer = std::make_unique<WorldStreamer>(SharedWorldGenerator(),
                                                  &physics_world, settings, 2);

  SECTION("The bear has ground under its feet right away") {
    streamer->EnsureCollision(start, 10.0f);
    bibo::physics::RayHit hit;
    REQUIRE(physics_world.CastRay(start + vec3(0.0f, 5.0f, 0.0f),
                                  vec3(0.0f, -15.0f, 0.0f), hit, true));
    CHECK(std::abs(hit.position.y -
                   SharedWorldGenerator().Height(Ground(start))) < 0.1f);
  }

  SECTION(
      "Close chunks get full detail, far ones less; the vegetation is "
      "complete") {
    REQUIRE(streamer->Finish(start, start, 120.0f));
    std::unordered_map<ChunkKey, int, ChunkKeyHash> lods;
    for (const WorldStreamer::MeshUpdate& update :
         streamer->TakeMeshUpdates()) {
      if (update.mesh) {
        lods[update.key] = update.mesh->lod;
      }
    }
    REQUIRE_FALSE(lods.empty());
    for (const auto& entry : lods) {
      float distance = DistanceToBox(start, ChunkBox(entry.first));
      CAPTURE(distance, entry.second);
      if (distance < settings.lod_distances[0]) {
        CHECK(entry.second == 0);
      }
      if (distance > settings.lod_distances[kLodCount - 2] + 20.0f) {
        CHECK(entry.second == kLodCount - 1);
      }
    }
    WorldStreamer::Statistics statistics = streamer->GetStatistics();
    CHECK(statistics.pending_jobs == 0);
    CHECK(statistics.collision_bodies > 0);
    CHECK(statistics.small_columns > 0);
    bool small = false, large = false;
    for (const WorldStreamer::VegetationUpdate& update :
         streamer->TakeVegetationUpdates()) {
      (update.layer == Layer::kSmall ? small : large) = true;
    }
    CHECK(large);
    CHECK(small);
  }

  SECTION("The physics follows the bear, the grass the camera") {
    REQUIRE(streamer->Finish(start, start, 120.0f));
    vec3 far = vec3(SharedWorldGenerator().GetPath().At(300.0f).position);
    REQUIRE(streamer->Finish(far, far, 120.0f));
    // Bodies around the new position, the ones at the start are gone
    bibo::physics::RayHit hit;
    CHECK(physics_world.CastRay(far + vec3(0.0f, 5.0f, 0.0f),
                                vec3(0.0f, -15.0f, 0.0f), hit, true));
    CHECK_FALSE(physics_world.CastRay(start + vec3(0.0f, 5.0f, 0.0f),
                                      vec3(0.0f, -15.0f, 0.0f), hit, true));
    bool removed = false;
    for (const WorldStreamer::VegetationUpdate& update :
         streamer->TakeVegetationUpdates()) {
      removed = removed || (update.layer == Layer::kSmall && !update.instances);
    }
    CHECK(removed);
  }

  SECTION("The streamer can be destroyed while its threads work") {
    streamer->Update(start, start);
    streamer.reset();
    CHECK(physics_world.GetBodyCount() == 0);
  }
}

#ifndef BIBO_WORLD_WORLDGENERATOR_HPP_
#define BIBO_WORLD_WORLDGENERATOR_HPP_
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "Landmarks.hpp"
#include "TrackPath.hpp"
#include "TrackTerrain.hpp"

namespace bibo {
namespace world {
struct Box {
  glm::vec3 minimum = glm::vec3(0.0f), maximum = glm::vec3(0.0f);
  bool Intersects(const Box& other) const {
    return glm::all(glm::lessThanEqual(minimum, other.maximum)) &&
           glm::all(glm::lessThanEqual(other.minimum, maximum));
  }
  bool Contains(glm::vec3 point) const {
    return glm::all(glm::lessThanEqual(minimum, point)) &&
           glm::all(glm::lessThanEqual(point, maximum));
  }
};

/**
 * Something of the old track that the new world must not cover, e.g. the finish
 * line or a sign
 */
struct KeepClear {
  glm::vec3 center;
  float radius;
};

/**
 * A cave: a roof of rock over the track between two distances along it
 */
struct Cave {
  float start = 0.0f, end = 0.0f;  // distance along the track
  float floor_height = 0.0f;       // average height of the floor
  float ceiling_height = 0.0f;  // of the roof above the floor (in the middle)
  float roof_height = 0.0f;     // top of the roof above the floor
  Box bounds;                   // everything of the roof is inside
};

/**
 * What a point of the surface is covered with (the shader adds snow, rock on
 * steep slopes and the forest floor)
 */
struct SurfaceMaterial {
  float trail = 0.0f;  // the dirt trail of the race track and of the foot paths
  float sand = 0.0f;   // beach
  float cave = 0.0f;   // inside of a cave (bare rock, no snow)
  float cliff = 0.0f;  // rocky coast
};

/**
 * The island: a signed density field that is positive inside of the ground
 * (rock, earth) and negative in the air. Its surface (density 0) is the
 * terrain, which can have overhangs and caves (a voxel world, the chunks are
 * meshed from it).
 *
 * In the middle the density is the height field of the race track, so the track
 * stays exactly as it was. Around it the forest goes on over hills down to the
 * coast, which has beaches and cliffs that go into the sea. Some stretches of
 * the track go through caves. Everything is computed from the position
 * (deterministic), so any part of the world can be generated at any time in any
 * thread.
 */
class WorldGenerator {
 public:
  static constexpr float kSeaFloor = -26.0f;
  static constexpr std::uint32_t kSeed = 1337u;

  struct Description {
    std::unique_ptr<TrackTerrain> track;
    glm::vec2 start = glm::vec2(0.0f);  // start and finish of the lap
    glm::vec2 start_direction = glm::vec2(0.0f, -1.0f);
    std::vector<glm::vec2> checkpoints;  // in driving direction
    std::vector<KeepClear> keep_clear;
    float sea_level = 0.0f;
  };

  explicit WorldGenerator(Description description);
  ~WorldGenerator();

  /**
   * @return Positive inside of the ground, negative in the air, about the
   * distance to the surface (exactly for flat ground)
   */
  float Density(glm::vec3 position) const;
  /**
   * Density at count points above each other, at (x, (first_index + i) * step,
   * z): the same values as Density() at these points, but faster (what does not
   * depend on the height is computed once)
   */
  void DensityColumn(float x, float z, int first_index, float step, int count,
                     float* densities) const;
  /**
   * @return Normal of the surface (pointing into the air) at or close to the
   * position
   */
  glm::vec3 Normal(glm::vec3 position) const;
  /**
   * Height of the ground without caves and overhangs (the height field the
   * world is made of), with the foot paths
   */
  float Height(glm::vec2 position) const;
  /**
   * Height of the ground before the foot paths are cut into it and the
   * clearings are leveled
   */
  float NaturalHeight(glm::vec2 position) const;
  /**
   * Finds the highest surface at a point of the ground plane (e.g. on the roof
   * of a cave, not in it)
   * @return false if there is no ground there
   */
  bool TopSurface(glm::vec2 position, glm::vec3& surface,
                  glm::vec3& surface_normal) const;
  /**
   * Like TopSurface, but faster where the ground is a height field (no cave, no
   * cliff)
   */
  bool Surface(glm::vec2 position, glm::vec3& surface,
               glm::vec3& surface_normal) const;
  /**
   * How much snow lies on the ground (patches, more on the mountains, none on
   * the trail, in caves and on the beach)
   * @param skyVisibility See skyVisibility (snow needs an open sky)
   */
  float SnowCover(glm::vec3 position, glm::vec3 surface_normal,
                  float sky_visibility, const SurfaceMaterial& material) const;
  /**
   * @return The top of the cave roof above the point, or a large negative
   * number if there is no cave
   */
  float CaveRoofTop(glm::vec2 position) const;
  /**
   * @return true if the point is under the roof of a cave
   */
  bool IsInCave(glm::vec3 position) const;
  SurfaceMaterial Material(glm::vec3 position, glm::vec3 surface_normal) const;
  /**
   * @return How much of the sky the point sees (0 in a closed cave, 1 on open
   * flat ground), from the density around it
   */
  float SkyVisibility(glm::vec3 position, glm::vec3 surface_normal) const;

  /**
   * Distance from the coast (positive inland) and how rocky the coast is there
   * (0 beach, 1 cliff)
   */
  void Coast(glm::vec2 position, float& inland, float& cliff) const;

  /**
   * Everything that can have ground (the island and the sea floor around it)
   */
  Box GetBounds() const { return bounds_; }
  /**
   * Lowest and highest density 0 in a box (plus a margin), so that chunks
   * completely in the air or in the ground can be skipped
   * @return false if the box surely contains no surface
   */
  bool MayContainSurface(const Box& box) const;
  float GetSeaLevel() const { return sea_level_; }
  glm::vec2 GetCenter() const { return center_; }
  const TrackTerrain& GetTrack() const { return *track_; }
  const TrackPath& GetPath() const { return *path_; }
  const std::vector<Cave>& GetCaves() const { return caves_; }
  const std::vector<KeepClear>& GetKeepClear() const { return keep_clear_; }
  /**
   * The points of interest and the foot paths to them
   */
  const Landmarks& GetLandmarks() const { return *landmarks_; }
  /**
   * Where the mountains are highest (about)
   */
  std::vector<glm::vec2> GetMountainPeaks() const;

 private:
  /**
   * What the density of a vertical column of points needs (independent of the
   * height)
   */
  struct Column {
    float height = 0.0f;
    float inland = 0.0f, cliff = 0.0f;
    bool rugged = false;  // cliff noise
    struct Roof {
      float ceiling, top, side, ends;  // without the 3D noise
      float bottom, upper;             // heights of the bounds of the cave
    };
    Roof roofs[4];
    int roof_count = 0;
  };
  Column ColumnAt(glm::vec2 position) const;
  float DensityIn(const Column& column, glm::vec3 position) const;
  float ProceduralHeight(glm::vec2 position) const;
  float CoastRadius(glm::vec2 direction) const;
  float CliffNoise(glm::vec3 position, float inland, float cliff) const;
  void ChooseCaves();
  void ComputeColumnRanges();

  std::unique_ptr<TrackTerrain> track_;
  std::unique_ptr<TrackPath> path_;
  std::vector<Cave> caves_;
  // null while they are made (the height is the natural height then)
  std::unique_ptr<Landmarks> landmarks_;
  std::vector<KeepClear> keep_clear_;
  float sea_level_;
  glm::vec2 center_;
  Box bounds_;

  // Lowest and highest ground in coarse columns of the bounds (for
  // MayContainSurface)
  static constexpr float kRangeCell = 16.0f;
  int range_columns_ = 0, range_rows_ = 0;
  std::vector<glm::vec2> column_ranges_;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_WORLDGENERATOR_HPP_

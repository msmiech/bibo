#ifndef BIBO_WORLD_TRACKPATH_HPP_
#define BIBO_WORLD_TRACKPATH_HPP_
#include <glm/glm.hpp>
#include <vector>

#include "TrackTerrain.hpp"

namespace bibo {
namespace world {
/**
 * The center line of the race track: a closed loop along the floor of the
 * canyon of the track terrain. It is found in the terrain (not stored in the
 * track file): the cheapest way through the checkpoints in their order, where
 * it is cheap to go along the floor of the canyon and far from its walls. The
 * world uses it for the trail, the caves and to keep the trees off the track.
 */
class TrackPath {
 public:
  struct Sample {
    glm::vec3 position;     // on the floor, in the middle of the canyon
    glm::vec2 tangent;      // direction of the track in the ground plane
    float distance = 0.0f;  // along the track from the start
    float floor_left = 0.0f,
          // width of the flat floor to the left and right of the center
        floor_right = 0.0f;
    float wall_left = 0.0f,
          // height of the walls above the floor (highest point close to the
          // track)
        wall_right = 0.0f;
    // the trail winds a bit on the floor (and around the pools): offset of its
    // middle to the right
    float trail_offset = 0.0f;
  };

  /**
   * What a point of the ground plane is next to
   */
  struct Nearest {
    float distance = 0.0f;  // along the track
    // from the center line, positive to the right (of the driving direction)
    float lateral = 0.0f;
    float trail_lateral = 0.0f;  // from the middle of the trail
    float floor_half_width =
        0.0f;  // width of the floor on the side of the point
    float floor_height = 0.0f;
    // false if the point is far from the track (further than kSearchRadius)
    bool found = false;
  };

  static constexpr float kSampleSpacing = 1.0f;
  static constexpr float kTrailHalfWidth = 2.3f;
  // Points further from the track are not looked up exactly
  static constexpr float kSearchRadius = 48.0f;

  /**
   * @param start Start (and finish) of the lap
   * @param startDirection Driving direction at the start
   * @param waypoints Points on the track in driving direction (the checkpoints)
   * @param waterLevel Below it the floor is under water (the pools on the
   * track): the trail goes around them where it can
   */
  TrackPath(const TrackTerrain& terrain, glm::vec2 start,
            glm::vec2 start_direction, const std::vector<glm::vec2>& waypoints,
            float water_level);

  const std::vector<Sample>& GetSamples() const { return samples_; }
  float GetLength() const { return length_; }
  /**
   * Interpolated sample at a distance along the track (wraps around)
   */
  Sample At(float distance) const;
  Nearest FindNearest(glm::vec2 position) const;

 private:
  void Measure(const TrackTerrain& terrain, Sample& sample) const;
  /**
   * @return true if the trail with the offset (and half width) is out of the
   * water at the sample
   */
  bool DryTrail(const TrackTerrain& terrain, float water_level,
                const Sample& sample, float offset, float half_width) const;
  void AvoidWater(const TrackTerrain& terrain, float water_level,
                  std::vector<float>& offsets) const;
  void BuildBuckets();

  std::vector<Sample> samples_;
  float length_ = 0.0f;

  // Segments of the center line sorted into a grid for the nearest point
  // queries
  static constexpr float kBucketSize = 16.0f;
  glm::vec2 bucket_origin_;
  int bucket_columns_ = 0, bucket_rows_ = 0;
  // indices of the segments (from sample i to i + 1) that come close
  std::vector<std::vector<int>> buckets_;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_TRACKPATH_HPP_

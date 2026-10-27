#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace bibo {
namespace world {
class WorldGenerator;

/**
 * A place of the island worth going to, e.g. for a quest: beaches, a lookout on
 * the highest summit, a camp in the forest, a circle of standing stones on a
 * hill and a view from the top of a cliff. A foot path leads to each of them.
 */
struct PointOfInterest {
  enum class Kind { BEACH, SUMMIT, CAMP, STANDING_STONES, CLIFF_VIEW };
  Kind kind = Kind::BEACH;
  glm::vec3 position = glm::vec3(0.0f);  // on the ground, in the middle
  glm::vec2 facing = glm::vec2(
      1.0f,
      0.0f);  // where the view is (e.g. the sea), from the middle of the island
  float radius = 0.0f;  // no trees and bushes in it
  bool level = false;   // the ground in it is leveled (a clearing)
};

/**
 * A foot path from the race track (or from another foot path) to a point of
 * interest
 */
struct FootPath {
  // along its middle, about 1 apart, at the height of the path, from its start
  // to the point of interest
  std::vector<glm::vec3> points;
  int pointOfInterest = 0;
  // starts at the floor of the race track, otherwise at another foot path
  bool fromTrack = true;
};

/**
 * The points of interest of the island and the foot paths to them. The paths
 * are found in the natural ground: the cheapest way from the point of interest
 * to the race track or to a path that is already there (so they branch), where
 * steep steps are expensive (they wind up the hills) and so is steep ground
 * beside the way (no ledges under crags), the sea, the caves and the cliffs are
 * avoided. The crags around the summit are worn down a bit. They are cut into
 * the ground with an even slope (WorldGenerator::height), a bit wider where the
 * cut is deeper. Deterministic like the rest of the world.
 */
class Landmarks {
 public:
  static constexpr float PATH_HALF_WIDTH = 1.2f;
  // Up and down along a path at most this much per unit
  static constexpr float MAX_GRADE = 0.4f;

  /**
   * @param world Its natural height, the coast, the track and the caves (the
   * landmarks are not used yet)
   */
  explicit Landmarks(const WorldGenerator& world);

  /**
   * @param natural Height of the natural ground at the position
   * (WorldGenerator::naturalHeight)
   * @return The height with the clearings and the paths
   */
  float height(glm::vec2 position, float natural) const;
  /**
   * @return Distance from the middle of the closest foot path (about, large far
   * from all of them)
   */
  float pathDistance(glm::vec2 position) const;
  /**
   * How much of the ground is the trodden earth of a foot path (or of a camp)
   */
  float trail(glm::vec2 position) const;
  /**
   * @return Index of the point of interest whose radius the position is in, -1
   * if none
   */
  int pointAt(glm::vec2 position) const;

  const std::vector<PointOfInterest>& getPointsOfInterest() const {
    return points;
  }
  const std::vector<FootPath>& getPaths() const { return paths; }
  /**
   * Where a foot path leaves the race track or another path: a small cairn
   * marks it, next to the path
   */
  const std::vector<glm::vec3>& getMarkers() const { return markers; }

 private:
  struct Grid;
  void choosePoints(const WorldGenerator& world);
  void findPaths(const WorldGenerator& world);
  void rasterize(const WorldGenerator& world);
  float leveled(const WorldGenerator& world, glm::vec2 position) const;
  float applyLeveling(glm::vec2 position, float height) const;
  /**
   * Wears the ground down around a point: the natural ground is blurred, fading
   * out towards the radius
   */
  void soften(const WorldGenerator& world, glm::vec2 center, float radius);
  float applySoftening(glm::vec2 position, float natural) const;
  // The natural ground, worn down where it is softened
  float naturalGround(const WorldGenerator& world, glm::vec2 position) const;

  // The blurred natural ground around a point (on a grid of RASTER_CELL)
  struct Softened {
    glm::vec2 center = glm::vec2(0.0f), origin = glm::vec2(0.0f);
    float radius = 0.0f;
    int columns = 0;  // as many rows
    std::vector<float> heights;
  };
  std::vector<Softened> softened;
  std::vector<PointOfInterest> points;
  std::vector<FootPath> paths;
  std::vector<glm::vec3> markers;

  // Around the paths: the two closest legs of the paths (a leg is a part of a
  // path, e.g. one side of a hairpin bend) and how much they may cut the ground
  // (not on the floor of the race track and in the caves). Bilinear between the
  // cells.
  struct Cell {
    float distance = 1e6f, height = 0.0f;  // closest leg
    float secondDistance = 1e6f, secondHeight = 0.0f;
    float allowed = 1.0f;
  };
  static constexpr float RASTER_CELL = 1.0f;
  glm::vec2 rasterOrigin = glm::vec2(0.0f);
  int rasterColumns = 0, rasterRows = 0;
  std::vector<Cell> raster;
  Cell cellAt(glm::vec2 position) const;
};
}  // namespace world
}  // namespace bibo

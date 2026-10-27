#ifndef BIBO_WORLD_TRACKTERRAIN_HPP_
#define BIBO_WORLD_TRACKTERRAIN_HPP_
#include <glm/glm.hpp>
#include <vector>

// NOLINTNEXTLINE(readability-identifier-naming): Assimp type
struct aiScene;

namespace bibo {
namespace world {
/**
 * The terrain of the race track (the node "Boden" of the track file) as a
 * height field: the height of the surface for every point of the ground plane.
 * The triangles are rasterized once (the terrain has no overhangs), afterwards
 * the height of any point is a bilinear lookup. The world is built around it,
 * so the track stays exactly as it was.
 */
class TrackTerrain {
 public:
  static constexpr float kCellSize = 0.5f;
  // The outermost border of the mesh is ragged, the usable rectangle starts
  // this far inside
  static constexpr float kBorderMargin = 4.0f;

  /**
   * @param vertices Triangle mesh in world space (y up)
   * @param indices 3 per triangle
   */
  TrackTerrain(const std::vector<glm::vec3>& vertices,
               const std::vector<unsigned int>& indices);

  /**
   * Vertices and triangles of a node of the scene in world space (with the
   * transformation of the node, like the game draws it)
   * @return false if the node does not exist or has no mesh
   */
  static bool LoadNodeMesh(const aiScene& scene, const char* node_name,
                           std::vector<glm::vec3>& vertices,
                           std::vector<unsigned int>& indices);
  /**
   * Sphere around the meshes of a node of the scene (world space)
   * @return false if the node does not exist or has no mesh
   */
  static bool NodeSphere(const aiScene& scene, const char* node_name,
                         glm::vec3& center, float& radius);

  /**
   * Height of the surface (bilinear between the cells, clamped to the rectangle
   * of the terrain)
   */
  float Height(glm::vec2 position) const;
  /**
   * Height of the terrain blurred over about 2 * kSmoothRadius (for continuing
   * the terrain outside of its border)
   */
  float SmoothHeight(glm::vec2 position) const;
  /**
   * @return Distance from the border of the usable rectangle with rounded
   * corners, negative outside of it
   */
  float DistanceInside(glm::vec2 position) const;
  /**
   * @return The closest point inside of the usable rectangle with rounded
   * corners
   */
  glm::vec2 ClampInside(glm::vec2 position) const;
  static constexpr float kCornerRadius = 60.0f;
  static constexpr float kSmoothRadius = 10.0f;
  glm::vec2 GetMinimum() const { return minimum_; }
  glm::vec2 GetMaximum() const { return maximum_; }
  glm::vec2 GetCenter() const { return 0.5f * (minimum_ + maximum_); }
  float GetLowest() const { return lowest_; }
  float GetHighest() const { return highest_; }

  // Raw grid (for the path finding)
  int GetColumns() const { return columns_; }
  int GetRows() const { return rows_; }
  glm::vec2 GetOrigin() const { return origin_; }
  float HeightAt(int column, int row) const {
    return heights_[static_cast<std::size_t>(row) * columns_ + column];
  }

 private:
  float Bilinear(const std::vector<float>& grid, glm::vec2 position) const;

  glm::vec2 origin_;  // position of the cell (0, 0)
  int columns_ = 0, rows_ = 0;
  std::vector<float> heights_;
  std::vector<float> smooth_heights_;
  glm::vec2 minimum_, maximum_;  // usable rectangle
  float lowest_ = 0.0f, highest_ = 0.0f;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_TRACKTERRAIN_HPP_

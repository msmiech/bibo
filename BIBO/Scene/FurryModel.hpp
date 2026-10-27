#ifndef BIBO_SCENE_FURRYMODEL_HPP_
#define BIBO_SCENE_FURRYMODEL_HPP_
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include "../Resources/SkinnedModel.hpp"

namespace bibo {
namespace scene {
/**
 * An animal for the renderer of the bear (the same skin shading, eyes and fur
 * strands), made of rigid parts that move separately (e.g. the body and the
 * feet of a hedgehog). Drawn by the FurryRenderer. Every vertex belongs to a
 * part and is in the space of that part.
 */
struct FurryModel {
  // Per vertex
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  // like SkinnedModel: linear color, fur length (0 to 1); for the eyes the
  // direction in eye space
  std::vector<glm::vec4> colors;
  std::vector<float> surfaces;  // SkinnedModel::Surface
  // index of the part (into the matrices of FurryRenderer::SetPose)
  std::vector<int> parts;
  // direction of the fur and the spines (model space)
  std::vector<glm::vec3> combs;
  std::vector<bool> spines;  // the strands of this vertex are spines (else fur)
  std::vector<unsigned int> indices;  // 3 per triangle
  // where the spines grow (3 indices per triangle)
  std::vector<unsigned int> spine_triangles;
  std::vector<unsigned int> fur_triangles;  // where the fur grows
};

/**
 * Builds a FurryModel from spheres, ellipsoids and surfaces around a center
 */
class FurryModelBuilder {
 public:
  explicit FurryModelBuilder(FurryModel& model) : model_(model) {}

  /**
   * @param comb Direction of the fur (model space)
   * @return Index of the vertex
   */
  unsigned int Add(glm::vec3 position, glm::vec3 normal, glm::vec4 color,
                   resources::SkinnedModel::Surface surface, int part,
                   bool spine = false,
                   glm::vec3 comb = glm::vec3(0.0f, 0.0f, -1.0f));

  /**
   * Rings of latitude around the y axis, the vertices come from a function of
   * the direction
   * @param vertex Adds the vertex in a direction and returns its index
   * @return The first triangle (index into indices / 3)
   */
  template <typename Vertex>
  std::size_t Sphere(int longitude, int latitude, Vertex vertex) {
    const float pi = glm::pi<float>();
    std::size_t first_triangle = model_.indices.size() / 3;
    std::vector<unsigned int> grid;
    for (int i = 0; i <= latitude; ++i) {
      float polar = pi * static_cast<float>(i) / static_cast<float>(latitude);
      for (int j = 0; j <= longitude; ++j) {
        float azimuth = 2.0f * pi * static_cast<float>(j % longitude) /
                        static_cast<float>(longitude);
        grid.push_back(vertex(glm::vec3(std::sin(polar) * std::sin(azimuth),
                                        std::cos(polar),
                                        std::sin(polar) * std::cos(azimuth))));
      }
    }
    for (int i = 0; i < latitude; ++i) {
      for (int j = 0; j < longitude; ++j) {
        unsigned int a = grid[i * (longitude + 1) + j],
                     b = grid[(i + 1) * (longitude + 1) + j];
        unsigned int c = grid[(i + 1) * (longitude + 1) + j + 1],
                     d = grid[i * (longitude + 1) + j + 1];
        Triangle(a, b, c);
        Triangle(a, c, d);
      }
    }
    return first_triangle;
  }

  /**
   * @return The first triangle (index into indices / 3)
   */
  std::size_t Ellipsoid(glm::vec3 center, glm::vec3 radii, int longitude,
                        int latitude, glm::vec4 color,
                        resources::SkinnedModel::Surface surface, int part);
  /**
   * The triangle faces outwards (where its vertex normals point)
   */
  void Triangle(unsigned int a, unsigned int b, unsigned int c);
  /**
   * Normals of the vertices from the triangles (for surfaces whose shape is not
   * an ellipsoid)
   */
  void SmoothNormals(std::size_t first_triangle, std::size_t last_triangle);
  /**
   * Fur grows on the triangles (e.g. the ears)
   */
  void GrowFur(std::size_t first_triangle, std::size_t last_triangle);
  /**
   * Sorts the triangles into spine_triangles and fur_triangles: spines where
   * most of the vertices of a triangle have spines
   */
  void GrowFurAndSpines(std::size_t first_triangle, std::size_t last_triangle);
  std::size_t TriangleCount() const { return model_.indices.size() / 3; }

 private:
  FurryModel& model_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_FURRYMODEL_HPP_

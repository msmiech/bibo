#pragma once
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
  // index of the part (into the matrices of FurryRenderer::setPose)
  std::vector<int> parts;
  // direction of the fur and the spines (model space)
  std::vector<glm::vec3> combs;
  std::vector<bool> spines;  // the strands of this vertex are spines (else fur)
  std::vector<unsigned int> indices;  // 3 per triangle
  // where the spines grow (3 indices per triangle)
  std::vector<unsigned int> spineTriangles;
  std::vector<unsigned int> furTriangles;  // where the fur grows
};

/**
 * Builds a FurryModel from spheres, ellipsoids and surfaces around a center
 */
class FurryModelBuilder {
 public:
  explicit FurryModelBuilder(FurryModel& model) : model(model) {}

  /**
   * @param comb Direction of the fur (model space)
   * @return Index of the vertex
   */
  unsigned int add(glm::vec3 position, glm::vec3 normal, glm::vec4 color,
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
  std::size_t sphere(int longitude, int latitude, Vertex vertex) {
    const float PI = glm::pi<float>();
    std::size_t firstTriangle = model.indices.size() / 3;
    std::vector<unsigned int> grid;
    for (int i = 0; i <= latitude; ++i) {
      float polar = PI * static_cast<float>(i) / static_cast<float>(latitude);
      for (int j = 0; j <= longitude; ++j) {
        float azimuth = 2.0f * PI * static_cast<float>(j % longitude) /
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
        triangle(a, b, c);
        triangle(a, c, d);
      }
    }
    return firstTriangle;
  }

  /**
   * @return The first triangle (index into indices / 3)
   */
  std::size_t ellipsoid(glm::vec3 center, glm::vec3 radii, int longitude,
                        int latitude, glm::vec4 color,
                        resources::SkinnedModel::Surface surface, int part);
  /**
   * The triangle faces outwards (where its vertex normals point)
   */
  void triangle(unsigned int a, unsigned int b, unsigned int c);
  /**
   * Normals of the vertices from the triangles (for surfaces whose shape is not
   * an ellipsoid)
   */
  void smoothNormals(std::size_t firstTriangle, std::size_t lastTriangle);
  /**
   * Fur grows on the triangles (e.g. the ears)
   */
  void growFur(std::size_t firstTriangle, std::size_t lastTriangle);
  /**
   * Sorts the triangles into spineTriangles and furTriangles: spines where most
   * of the vertices of a triangle have spines
   */
  void growFurAndSpines(std::size_t firstTriangle, std::size_t lastTriangle);
  std::size_t triangleCount() const { return model.indices.size() / 3; }

 private:
  FurryModel& model;
};
}  // namespace scene
}  // namespace bibo

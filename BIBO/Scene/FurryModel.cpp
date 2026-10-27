#include "FurryModel.hpp"

#include <algorithm>
#include <cmath>

namespace bibo::scene {

using resources::SkinnedModel;

unsigned int FurryModelBuilder::Add(glm::vec3 position, glm::vec3 normal,
                                    glm::vec4 color,
                                    SkinnedModel::Surface surface, int part,
                                    bool spine, glm::vec3 comb) {
  model_.positions.push_back(position);
  model_.normals.push_back(glm::length(normal) > 1e-6f
                               ? glm::normalize(normal)
                               : glm::vec3(0.0f, 1.0f, 0.0f));
  model_.colors.push_back(color);
  model_.surfaces.push_back(static_cast<float>(surface));
  model_.parts.push_back(part);
  model_.combs.push_back(glm::length(comb) > 1e-6f
                             ? glm::normalize(comb)
                             : glm::vec3(0.0f, 0.0f, -1.0f));
  model_.spines.push_back(spine);
  return static_cast<unsigned int>(model_.positions.size() - 1);
}

std::size_t FurryModelBuilder::Ellipsoid(glm::vec3 center, glm::vec3 radii,
                                         int longitude, int latitude,
                                         glm::vec4 color,
                                         SkinnedModel::Surface surface,
                                         int part) {
  return Sphere(longitude, latitude, [&](glm::vec3 direction) {
    return Add(center + direction * radii, direction / radii, color, surface,
               part);
  });
}

void FurryModelBuilder::Triangle(unsigned int a, unsigned int b,
                                 unsigned int c) {
  const std::vector<glm::vec3>& p = model_.positions;
  glm::vec3 face = glm::cross(p[b] - p[a], p[c] - p[a]);
  if (glm::dot(face, model_.normals[a] + model_.normals[b] +
                         model_.normals[c]) < 0.0f) {
    std::swap(b, c);
  }
  model_.indices.insert(model_.indices.end(), {a, b, c});
}

void FurryModelBuilder::SmoothNormals(std::size_t first_triangle,
                                      std::size_t last_triangle) {
  std::vector<glm::vec3> sums(model_.positions.size(), glm::vec3(0.0f));
  std::vector<bool> used(model_.positions.size(), false);
  for (std::size_t t = first_triangle; t < last_triangle; ++t) {
    unsigned int a = model_.indices[t * 3], b = model_.indices[t * 3 + 1],
                 c = model_.indices[t * 3 + 2];
    glm::vec3 face = glm::cross(model_.positions[b] - model_.positions[a],
                                model_.positions[c] - model_.positions[a]);
    for (unsigned int v : {a, b, c}) {
      sums[v] += face;
      used[v] = true;
    }
  }
  for (std::size_t v = 0; v < sums.size(); ++v) {
    if (used[v] && glm::length(sums[v]) > 1e-12f) {
      model_.normals[v] = glm::normalize(sums[v]);
    }
  }
}

void FurryModelBuilder::GrowFur(std::size_t first_triangle,
                                std::size_t last_triangle) {
  model_.fur_triangles.insert(model_.fur_triangles.end(),
                              model_.indices.begin() + first_triangle * 3,
                              model_.indices.begin() + last_triangle * 3);
}

void FurryModelBuilder::GrowFurAndSpines(std::size_t first_triangle,
                                         std::size_t last_triangle) {
  for (std::size_t t = first_triangle; t < last_triangle; ++t) {
    int spine_vertices = 0;
    for (int k = 0; k < 3; ++k) {
      spine_vertices += model_.spines[model_.indices[t * 3 + k]] ? 1 : 0;
    }
    std::vector<unsigned int>& target =
        spine_vertices >= 2 ? model_.spine_triangles : model_.fur_triangles;
    target.insert(target.end(), model_.indices.begin() + t * 3,
                  model_.indices.begin() + t * 3 + 3);
  }
}

}  // namespace bibo::scene

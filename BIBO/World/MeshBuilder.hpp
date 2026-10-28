#ifndef BIBO_WORLD_MESHBUILDER_HPP_
#define BIBO_WORLD_MESHBUILDER_HPP_
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

#include "VegetationMeshes.hpp"

namespace bibo {
namespace world {
inline std::uint8_t ToByte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f +
                                   0.5f);
}

/**
 * Colors are given in sRGB (0 to 255) like in a paint program
 */
inline glm::vec3 Rgb(float r, float g, float b) {
  return glm::vec3(r, g, b) / 255.0f;
}

/**
 * Builds the generated meshes of the vegetation and the animals
 * (VegetationVertex)
 */
class MeshBuilder {
 public:
  VegetationMesh mesh;

  std::uint32_t Add(glm::vec3 position, glm::vec3 normal, glm::vec2 uv,
                    glm::vec3 color, VegetationMaterial material, float wind,
                    float snow, float occlusion) {
    VegetationVertex vertex;
    vertex.position = position;
    vertex.normal = glm::length(normal) > 1e-6f ? glm::normalize(normal)
                                                : glm::vec3(0.0f, 1.0f, 0.0f);
    vertex.uv = uv;
    vertex.color[0] = ToByte(color.r);
    vertex.color[1] = ToByte(color.g);
    vertex.color[2] = ToByte(color.b);
    vertex.color[3] = 255;
    vertex.parameters[0] = material;
    vertex.parameters[1] = ToByte(wind);
    vertex.parameters[2] = ToByte(snow);
    vertex.parameters[3] = ToByte(occlusion);
    mesh.vertices.push_back(vertex);
    return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
  }

  /**
   * The triangle faces where its vertex normals point (the order of the
   * vertices does not matter)
   */
  void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const VegetationVertex& va = mesh.vertices[a];
    const VegetationVertex& vb = mesh.vertices[b];
    const VegetationVertex& vc = mesh.vertices[c];
    glm::vec3 face =
        glm::cross(vb.position - va.position, vc.position - va.position);
    if (glm::dot(face, va.normal + vb.normal + vc.normal) < 0.0f) {
      std::swap(b, c);
    }
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
  }

  void Quad(std::uint32_t a, std::uint32_t b, std::uint32_t c,
            std::uint32_t d) {
    Triangle(a, b, c);
    Triangle(a, c, d);
  }

  /**
   * Normals of the vertices from the triangles (for displaced, round shapes)
   */
  void SmoothNormals(std::size_t first_vertex, std::size_t first_index) {
    std::vector<glm::vec3> sums(mesh.vertices.size() - first_vertex,
                                glm::vec3(0.0f));
    for (std::size_t i = first_index; i + 2 < mesh.indices.size(); i += 3) {
      std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1],
                    c = mesh.indices[i + 2];
      glm::vec3 face =
          glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                     mesh.vertices[c].position - mesh.vertices[a].position);
      for (std::uint32_t v : {a, b, c}) {
        if (v >= first_vertex) {
          sums[v - first_vertex] += face;
        }
      }
    }
    for (std::size_t v = 0; v < sums.size(); ++v) {
      if (glm::length(sums[v]) > 1e-9f) {
        mesh.vertices[first_vertex + v].normal = glm::normalize(sums[v]);
      }
    }
  }

  VegetationMesh Finish() {
    mesh.radius = 0.0f;
    for (const VegetationVertex& vertex : mesh.vertices) {
      mesh.radius = std::max(mesh.radius, glm::length(vertex.position));
    }
    return std::move(mesh);
  }
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_MESHBUILDER_HPP_

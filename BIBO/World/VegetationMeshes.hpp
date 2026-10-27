#ifndef BIBO_WORLD_VEGETATIONMESHES_HPP_
#define BIBO_WORLD_VEGETATIONMESHES_HPP_
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "Vegetation.hpp"

namespace bibo {
namespace world {
/**
 * How a part of a model is shaded (the vegetation shader uses the same numbers)
 */
enum VegetationMaterial : std::uint8_t {
  kVegetationPlain,    // vertex color
  kVegetationBark,     // bark texture along the uv (u around, v along)
  kVegetationNeedles,  // needle texture, translucent
  kVegetationLeaves,   // leaf texture, translucent
  // rock texture projected in model space, moss and snow on top
  kVegetationStone,
  kVegetationCapSpotted,  // mushroom cap with white spots
  kVegetationStem,        // mushroom stem
  kVegetationGlow,        // glowing mushroom
  kVegetationWood,        // cut wood with year rings (uv: center at 0.5, 0.5)
  kVegetationGrass,       // blades of grass, translucent
  kVegetationGlossy       // wet and shiny: eyes, noses, the scales of a fish
};

/**
 * Vertex of a vegetation model (40 bytes)
 */
struct VegetationVertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  std::uint8_t color[4];  // sRGB color (rgb), a: unused
  // material, wind weight (how much it sways), snow (how much it can hold),
  // ambient occlusion
  std::uint8_t parameters[4];
};

struct VegetationMesh {
  std::vector<VegetationVertex> vertices;
  std::vector<std::uint32_t> indices;
  float radius = 0.0f;  // of the bounding sphere around the origin
};

/**
 * The levels of detail of a model (1 to 3, the first is the most detailed).
 * Generated, deterministic.
 */
std::vector<VegetationMesh> BuildModel(Model model);
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_VEGETATIONMESHES_HPP_

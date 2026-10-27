#pragma once
#include <array>
#include <cstddef>
#include <glm/glm.hpp>

#include "FurryRenderer.hpp"
#include "HedgehogModel.hpp"

namespace bibo {
namespace scene {
/**
 * Draws the hedgehogs like the bear (FurryRenderer): the skin with the eyes and
 * the nose, the fur on the face and the belly and the spines (stiff, banded
 * strands). Needs an OpenGL context.
 */
class HedgehogRenderer : public FurryRenderer {
 public:
  static constexpr std::size_t SPINE_COUNT = 4500;
  static constexpr std::size_t FUR_COUNT = 16000;
  // Length of the longest spines (world units), the fur is as long as the fur
  // of the bear
  static constexpr float SPINE_LENGTH = 0.24f;
  static constexpr float SPINE_WIDTH = 0.016f;

  /**
   * @param count Number of hedgehogs
   */
  explicit HedgehogRenderer(std::size_t count);

  /**
   * Where a hedgehog is in this frame
   * @param body Model matrix of the body, feet: of the feet (with the scale)
   * @param bristle 0 to 1: the spines stand up (when it is startled)
   */
  void setPose(std::size_t index, const glm::mat4& body,
               const std::array<glm::mat4, 4>& feet, float bristle);
};
}  // namespace scene
}  // namespace bibo

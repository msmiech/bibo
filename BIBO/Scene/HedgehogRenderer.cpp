#include "HedgehogRenderer.hpp"

#include <vector>

namespace bibo::scene {

namespace {
FurryRenderer::Strands HedgehogStrands() {
  return {.fur_count = HedgehogRenderer::kFurCount,
          .spine_count = HedgehogRenderer::kSpineCount,
          .spine_length = HedgehogRenderer::kSpineLength,
          .spine_width = HedgehogRenderer::kSpineWidth,
          .fur_seed = 78,
          .spine_seed = 77};
}

FurryRenderer::Bounds HedgehogBounds() {
  return {.center = glm::vec3(0.0f, kHedgehogBodyHeight, 0.0f),
          .radius = 0.75f};
}
}  // namespace

HedgehogRenderer::HedgehogRenderer(std::size_t count)
    : FurryRenderer(HedgehogModel::Build(), count, HedgehogStrands(),
                    HedgehogBounds()) {}

void HedgehogRenderer::SetPose(std::size_t index, const glm::mat4& body,
                               const std::array<glm::mat4, 4>& feet,
                               float bristle) {
  std::vector<glm::mat4> parts = {body, feet[0], feet[1], feet[2], feet[3]};
  FurryRenderer::SetPose(index, parts, bristle);
}

}  // namespace bibo::scene

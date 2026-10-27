#pragma once
#include "FurryModel.hpp"

namespace bibo {
namespace scene {
// Where the feet of the hedgehog are under its body (model space, front left;
// the others mirrored)
constexpr float HEDGEHOG_FOOT_SIDE = 0.16f;
constexpr float HEDGEHOG_FOOT_FRONT = 0.13f;
constexpr float HEDGEHOG_FOOT_BACK = -0.2f;
// Center and radius of the round body (without the spines), for the collisions
constexpr float HEDGEHOG_BODY_HEIGHT = 0.25f;
constexpr float HEDGEHOG_BODY_RADIUS = 0.33f;

/**
 * The hedgehog for the renderer of the bear (FurryModel): a round body with a
 * pointed snout, spines on the back and the sides, short fur on the face and
 * the belly, small dark eyes, a wet nose, round ears and four feet with claws.
 * The feet are in the space of their foot (origin on the ground under it), the
 * rest in the space of the body. Model space: +Z forward, +Y up, the origin on
 * the ground under the body, about 1 long (scaled up in the world,
 * Hedgehog::SCALE). Generated, the same every time.
 */
struct HedgehogModel : FurryModel {
  enum Part {
    BODY = 0,
    FOOT_FRONT_LEFT = 1,
    FOOT_FRONT_RIGHT,
    FOOT_HIND_LEFT,
    FOOT_HIND_RIGHT,
    PART_COUNT
  };

  static HedgehogModel build();
};
}  // namespace scene
}  // namespace bibo

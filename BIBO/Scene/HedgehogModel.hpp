#ifndef BIBO_SCENE_HEDGEHOGMODEL_HPP_
#define BIBO_SCENE_HEDGEHOGMODEL_HPP_
#include "FurryModel.hpp"

namespace bibo {
namespace scene {
// Where the feet of the hedgehog are under its body (model space, front left;
// the others mirrored)
constexpr float kHedgehogFootSide = 0.16f;
constexpr float kHedgehogFootFront = 0.13f;
constexpr float kHedgehogFootBack = -0.2f;
// Center and radius of the round body (without the spines), for the collisions
constexpr float kHedgehogBodyHeight = 0.25f;
constexpr float kHedgehogBodyRadius = 0.33f;

/**
 * The hedgehog for the renderer of the bear (FurryModel): a round body with a
 * pointed snout, spines on the back and the sides, short fur on the face and
 * the belly, small dark eyes, a wet nose, round ears and four feet with claws.
 * The feet are in the space of their foot (origin on the ground under it), the
 * rest in the space of the body. Model space: +Z forward, +Y up, the origin on
 * the ground under the body, about 1 long (scaled up in the world,
 * Hedgehog::kScale). Generated, the same every time.
 */
struct HedgehogModel : FurryModel {
  enum Part {
    kBody = 0,
    kFootFrontLeft = 1,
    kFootFrontRight,
    kFootHindLeft,
    kFootHindRight,
    kPartCount
  };

  static HedgehogModel Build();
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_HEDGEHOGMODEL_HPP_

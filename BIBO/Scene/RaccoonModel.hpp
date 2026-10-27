#pragma once
#include <glm/glm.hpp>

#include "FurryModel.hpp"

namespace bibo {
namespace scene {
// Joints of the parts of the raccoon (model space of the body, the jaw: of the
// head) when it stands on all fours
constexpr glm::vec3 RACCOON_HIPS(
    0.0f, 0.3f, -0.12f);  // it rears up around this point (on the middle line)
constexpr glm::vec3 RACCOON_NECK(0.0f, 0.37f, 0.25f);
constexpr glm::vec3 RACCOON_JAW(0.0f, 0.0f, 0.1f);
constexpr glm::vec3 RACCOON_TAIL(
    0.0f, 0.36f, -0.24f);  // where the tail grows out of the body
constexpr glm::vec3 RACCOON_SHOULDER(
    0.085f, 0.27f, 0.17f);  // left, the right one mirrored (inside the chest)
constexpr glm::vec3 RACCOON_HIP(
    0.09f, 0.3f, -0.12f);  // left hip joint, the right one mirrored
// Length of the legs from their joint to the ground
constexpr float RACCOON_FRONT_LEG = 0.27f, RACCOON_HIND_LEG = 0.3f;
// Height of the back when it stands on all fours
constexpr float RACCOON_HEIGHT = 0.5f;

/**
 * The raccoon for the renderer of the bear (FurryModel), standing on all fours:
 * a round, gray body with thick fur that is higher at the hips, a broad head
 * with a pointed muzzle, the black mask around its eyes framed with white, a
 * black nose, pointed ears with white rims, a lower jaw that opens when it
 * chatters, slim legs with black, hand-like paws and the bushy tail with dark
 * rings. It can rear up on its hind legs (Raccoon). Every part is in its own
 * space with the origin at its joint (see the constants above), the parts reach
 * into the body (or the head), so they stay connected when they move. Model
 * space: +Z forward, +Y up, +X left, the origin on the ground under the body,
 * about 1 long (scaled up in the world, Raccoon::SCALE). Generated, the same
 * every time.
 */
struct RaccoonModel : FurryModel {
  enum Part {
    BODY = 0,
    HEAD,
    JAW,
    TAIL,
    FRONT_LEFT,
    FRONT_RIGHT,
    HIND_LEFT,
    HIND_RIGHT,
    PART_COUNT
  };

  /**
   * @param girth How round its belly is (1: an ordinary raccoon, more: a well
   * fed one)
   */
  static RaccoonModel build(float girth = 1.0f);
};
}  // namespace scene
}  // namespace bibo

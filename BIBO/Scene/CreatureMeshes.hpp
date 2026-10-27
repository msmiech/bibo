#ifndef BIBO_SCENE_CREATUREMESHES_HPP_
#define BIBO_SCENE_CREATUREMESHES_HPP_
#include "../World/VegetationMeshes.hpp"

namespace bibo {
namespace scene {
/**
 * The salmon and the splashes of water as parts that move separately, and the
 * glowing exclamation mark above the beaver that offers a quest (drawn by the
 * CreatureRenderer, the hedgehogs and the beaver are drawn like the bear, see
 * FurryModel). In model space +Z is forward and +Y up, the origin is in the
 * middle of the fish, which is 1 long.
 */
enum class CreatureModel {
  kSalmonHead,  // from the gills to the snout (eaten first)
  kSalmonBody,
  // origin at the joint to the body (z = kSalmonTailJoint of the whole fish),
  // it wiggles
  kSalmonTail,
  kDroplet,  // splash of water, radius 1
  // glowing golden exclamation mark, 1 high, the origin at its bottom
  kQuestMarker,
  // the same in blue: a quest that was completed and can be done again
  kQuestMarkerRepeatable,
  kCount
};
constexpr int kCreatureModelCount = static_cast<int>(CreatureModel::kCount);

// Where the parts of the salmon meet (along its length, -0.5 tail to 0.5 snout)
constexpr float kSalmonHeadJoint = 0.2f;
constexpr float kSalmonTailJoint = -0.15f;

/**
 * Generated, the same every time
 */
world::VegetationMesh BuildCreatureModel(CreatureModel model);
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_CREATUREMESHES_HPP_

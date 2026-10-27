#ifndef BIBO_ANIMATION_INVERSEKINEMATICS_HPP_
#define BIBO_ANIMATION_INVERSEKINEMATICS_HPP_
#include <glm/glm.hpp>

namespace bibo {
namespace animation {
/**
 * Result of SolveTwoBoneIk
 */
struct TwoBoneSolution {
  glm::vec3 joint;  // new position of the middle joint (knee, elbow)
  // new position of the end of the chain (the target, moved closer if it is out
  // of reach)
  glm::vec3 end;
  bool reached;  // false if the target was too far away or too close
};

/**
 * Analytic inverse kinematics for a chain of two bones (e.g. thigh and shin):
 * where the middle joint has to be so that the end of the chain is at the
 * target and both bones keep their length.
 * @param root Start of the chain (hip, shoulder), it does not move
 * @param upper_length Length from the root to the middle joint
 * @param lower_length Length from the middle joint to the end
 * @param target Where the end of the chain should be
 * @param pole The middle joint bends towards this point (e.g. in front of the
 * knee)
 * @param minimum_bend The chain is never stretched completely (cosine of the
 * angle at the middle joint stays above -1 + minimum_bend), so that the bend
 * direction stays well defined
 */
TwoBoneSolution SolveTwoBoneIk(glm::vec3 root, float upper_length,
                               float lower_length, glm::vec3 target,
                               glm::vec3 pole, float minimum_bend = 0.002f);
}  // namespace animation
}  // namespace bibo

#endif  // BIBO_ANIMATION_INVERSEKINEMATICS_HPP_

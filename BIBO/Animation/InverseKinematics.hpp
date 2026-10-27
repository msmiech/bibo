#pragma once
#include <glm/glm.hpp>

namespace bibo {
namespace animation {
/**
 * Result of solveTwoBoneIK
 */
struct TwoBoneSolution {
  glm::vec3 joint;  // new position of the middle joint (knee, elbow)
  glm::vec3 end;    // new position of the end of the chain (the target, moved
                    // closer if it is out of reach)
  bool reached;     // false if the target was too far away or too close
};

/**
 * Analytic inverse kinematics for a chain of two bones (e.g. thigh and shin):
 * where the middle joint has to be so that the end of the chain is at the
 * target and both bones keep their length.
 * @param root Start of the chain (hip, shoulder), it does not move
 * @param upperLength Length from the root to the middle joint
 * @param lowerLength Length from the middle joint to the end
 * @param target Where the end of the chain should be
 * @param pole The middle joint bends towards this point (e.g. in front of the
 * knee)
 * @param minimumBend The chain is never stretched completely (cosine of the
 * angle at the middle joint stays above -1 + minimumBend), so that the bend
 * direction stays well defined
 */
TwoBoneSolution solveTwoBoneIK(glm::vec3 root, float upperLength,
                               float lowerLength, glm::vec3 target,
                               glm::vec3 pole, float minimumBend = 0.002f);
}  // namespace animation
}  // namespace bibo

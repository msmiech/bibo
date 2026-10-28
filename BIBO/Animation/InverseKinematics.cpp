#include "InverseKinematics.hpp"

#include <algorithm>
#include <cmath>

namespace bibo::animation {

TwoBoneSolution SolveTwoBoneIk(glm::vec3 root, float upper_length,
                               float lower_length, glm::vec3 target,
                               glm::vec3 pole, float minimum_bend) {
  TwoBoneSolution solution;
  glm::vec3 to_target = target - root;
  float distance = glm::length(to_target);
  glm::vec3 direction =
      distance > 1e-6f ? to_target / distance : glm::vec3(0.0f, -1.0f, 0.0f);

  // Law of cosines: the chain reaches at most (upper + lower) and at least
  // |upper - lower|
  float cos_max = 1.0f - minimum_bend;
  float max_distance =
      std::sqrt(upper_length * upper_length + lower_length * lower_length +
                2.0f * upper_length * lower_length * cos_max);
  float min_distance = std::abs(upper_length - lower_length) + 1e-4f;
  float clamped = std::clamp(distance, min_distance, max_distance);
  solution.reached = std::abs(clamped - distance) < 1e-5f;
  solution.end = root + direction * clamped;

  // Distance of the middle joint along the line from the root to the end, and
  // its distance from that line
  float along = (upper_length * upper_length - lower_length * lower_length +
                 clamped * clamped) /
                (2.0f * clamped);
  float height =
      std::sqrt(std::max(upper_length * upper_length - along * along, 0.0f));

  // The middle joint bends towards the pole, perpendicular to the line
  glm::vec3 to_pole = pole - root;
  glm::vec3 bend = to_pole - glm::dot(to_pole, direction) * direction;
  if (glm::dot(bend, bend) < 1e-10f) {
    // Pole on the line: any perpendicular direction
    glm::vec3 helper = std::abs(direction.y) < 0.9f
                           ? glm::vec3(0.0f, 1.0f, 0.0f)
                           : glm::vec3(1.0f, 0.0f, 0.0f);
    bend = glm::cross(direction, helper);
  }
  bend = glm::normalize(bend);
  solution.joint = root + direction * along + bend * height;
  return solution;
}

}  // namespace bibo::animation

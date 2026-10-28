#ifndef BIBO_SCENE_GAMERULES_HPP_
#define BIBO_SCENE_GAMERULES_HPP_
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace bibo {
namespace scene {
/**
 * Rules of the race around the track (RaceQuest): a lap is complete when the
 * bear reaches the finish line after it passed all checkpoints along the track
 * in their order. The race starts at the finish line.
 */
namespace rules {
/**
 * Checkpoints along the track in driving direction (x and z in world
 * coordinates, where the salmon of the model are placed). Going backwards from
 * the start reaches them in the wrong order, so the bear has to go around the
 * whole track.
 */
inline const glm::vec2 kCheckpoints[] = {
    glm::vec2(51.2f, -35.7f),  glm::vec2(6.0f, -58.6f),
    glm::vec2(-7.8f, -101.6f), glm::vec2(-59.4f, -71.2f),
    glm::vec2(-66.0f, 1.8f),   glm::vec2(-55.3f, 76.9f),
    glm::vec2(31.3f, 77.1f),   glm::vec2(80.7f, 11.6f)};
const std::size_t kCheckpointCount =
    sizeof(kCheckpoints) / sizeof(kCheckpoints[0]);
// More than half the width of the track, so the bear cannot go around a
// checkpoint
const float kCheckpointRadius = 12.0f;

/**
 * @param nextCheckpoint Index of the checkpoint the bear has to pass next
 * (kCheckpointCount: all passed)
 * @return The new index: one more if the bear is at the next checkpoint (in the
 * xz plane, it may also jump over it)
 */
inline std::size_t PassCheckpoint(glm::vec3 player_position,
                                  std::size_t next_checkpoint) {
  if (next_checkpoint < kCheckpointCount &&
      glm::distance(glm::vec2(player_position.x, player_position.z),
                    kCheckpoints[next_checkpoint]) <= kCheckpointRadius) {
    return next_checkpoint + 1;
  }
  return next_checkpoint;
}

/**
 * Invisible trigger at the finish line: as wide as the finish line board and
 * deeper than it, so that the bear cannot step over it in one frame
 */
struct FinishLine {
  glm::vec3 center;
  // horizontal direction along the finish line, across the track
  glm::vec3 across;
  float half_width = 0.0f;
};
const float kFinishLineHalfDepth = 1.5f;

/**
 * @param model_matrix Model matrix of the finish line board (Geometry), which
 * goes along its x axis across the track
 * @param half_length Half the length of the mesh of the board along its x axis
 * (model space)
 */
inline FinishLine MakeFinishLine(const glm::mat4& model_matrix,
                                 float half_length) {
  glm::vec3 across = glm::vec3(model_matrix[0]);
  FinishLine line;
  line.center = glm::vec3(model_matrix[3]);
  line.half_width = half_length * glm::length(across);
  line.across = glm::normalize(glm::vec3(across.x, 0.0f, across.z));
  return line;
}

inline bool IsOnFinishLine(glm::vec3 player_position, const FinishLine& line) {
  glm::vec3 offset = player_position - line.center;
  offset.y = 0.0f;  // the bear may also jump over it
  float across_distance = glm::dot(offset, line.across);
  float along_distance = glm::length(offset - across_distance * line.across);
  return std::abs(across_distance) <= line.half_width &&
         along_distance <= kFinishLineHalfDepth;
}

inline bool HasCompletedLap(std::size_t checkpoints_passed,
                            glm::vec3 player_position, const FinishLine& line) {
  return checkpoints_passed == kCheckpointCount &&
         IsOnFinishLine(player_position, line);
}
}  // namespace rules
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_GAMERULES_HPP_

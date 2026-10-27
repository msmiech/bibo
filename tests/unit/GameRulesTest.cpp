#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"

namespace rules = bibo::scene::rules;
using glm::vec2;
using glm::vec3;

namespace {
vec3 AtCheckpoint(std::size_t index, vec2 offset = vec2(0.0f)) {
  vec2 position = rules::kCheckpoints[index] + offset;
  return vec3(position.x, 3.0f, position.y);
}

// Finish line board along the x axis (20 units long, half length 10 in model
// space), centered at (5, 2, -10)
rules::FinishLine TestFinishLine() {
  glm::mat4 model_matrix(1.0f);
  model_matrix[3] = glm::vec4(5.0f, 2.0f, -10.0f, 1.0f);
  return rules::MakeFinishLine(model_matrix, 10.0f);
}
}  // namespace

TEST_CASE("A checkpoint is passed within its radius, at any height",
          "[rules]") {
  CHECK(rules::PassCheckpoint(AtCheckpoint(0), 0) == 1);
  CHECK(rules::PassCheckpoint(
            AtCheckpoint(0, vec2(rules::kCheckpointRadius - 0.1f, 0.0f)), 0) ==
        1);
  // jumping over it
  CHECK(rules::PassCheckpoint(AtCheckpoint(0) + vec3(0.0f, 20.0f, 0.0f), 0) ==
        1);

  CHECK(rules::PassCheckpoint(
            AtCheckpoint(0, vec2(0.0f, rules::kCheckpointRadius + 0.1f)), 0) ==
        0);
}

TEST_CASE("Only the next checkpoint counts", "[rules]") {
  SECTION("Later checkpoints do not count before the earlier ones") {
    const std::size_t later = GENERATE(1, 2, 7);
    CAPTURE(later);
    CHECK(rules::PassCheckpoint(AtCheckpoint(later), 0) == 0);
  }

  SECTION("Passed checkpoints do not count again") {
    CHECK(rules::PassCheckpoint(AtCheckpoint(0), 1) == 1);
  }

  SECTION("After the last checkpoint nothing changes") {
    CHECK(rules::PassCheckpoint(AtCheckpoint(rules::kCheckpointCount - 1),
                                rules::kCheckpointCount) ==
          rules::kCheckpointCount);
  }
}

TEST_CASE("The finish line trigger is as wide as the board and deeper than it",
          "[rules]") {
  const rules::FinishLine line = TestFinishLine();
  CHECK(line.half_width == 10.0f);

  CHECK(rules::IsOnFinishLine(vec3(5.0f, 2.0f, -10.0f), line));
  CHECK(rules::IsOnFinishLine(vec3(14.9f, 2.0f, -10.0f),
                              line));  // at the end of the board
  CHECK(rules::IsOnFinishLine(
      vec3(5.0f, 2.0f, -10.0f + rules::kFinishLineHalfDepth - 0.1f), line));
  CHECK(rules::IsOnFinishLine(vec3(5.0f, 30.0f, -10.0f),
                              line));  // jumping over it

  CHECK_FALSE(rules::IsOnFinishLine(vec3(15.1f, 2.0f, -10.0f),
                                    line));  // next to the board
  CHECK_FALSE(rules::IsOnFinishLine(
      vec3(5.0f, 2.0f, -10.0f - rules::kFinishLineHalfDepth - 0.1f), line));
}

TEST_CASE(
    "The lap is complete at the finish line after all checkpoints in their "
    "order",
    "[rules]") {
  const rules::FinishLine line = TestFinishLine();
  const vec3 on_finish_line(5.0f, 2.0f, -10.0f);

  SECTION("Around the track") {
    std::size_t next_checkpoint = 0;
    for (std::size_t i = 0; i < rules::kCheckpointCount; ++i) {
      CHECK_FALSE(
          rules::HasCompletedLap(next_checkpoint, on_finish_line, line));
      next_checkpoint = rules::PassCheckpoint(AtCheckpoint(i), next_checkpoint);
    }
    CHECK(rules::HasCompletedLap(next_checkpoint, on_finish_line, line));
    CHECK_FALSE(rules::HasCompletedLap(next_checkpoint,
                                       on_finish_line + vec3(0.0f, 0.0f, 5.0f),
                                       line));  // not yet at the finish line
  }

  SECTION("Backwards around the track only the first checkpoint counts") {
    std::size_t next_checkpoint = 0;
    for (std::size_t i = rules::kCheckpointCount; i > 0; --i) {
      next_checkpoint =
          rules::PassCheckpoint(AtCheckpoint(i - 1), next_checkpoint);
    }
    CHECK(next_checkpoint == 1);
    CHECK_FALSE(rules::HasCompletedLap(next_checkpoint, on_finish_line, line));
  }
}

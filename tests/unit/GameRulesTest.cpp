#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Scene/GameRules.hpp"
#include "TestSupport.hpp"

using namespace bibo::scene;
using glm::vec2;
using glm::vec3;

namespace {
vec3 atCheckpoint(std::size_t index, vec2 offset = vec2(0.0f)) {
  vec2 position = rules::CHECKPOINTS[index] + offset;
  return vec3(position.x, 3.0f, position.y);
}

// Finish line board along the x axis (20 units long, half length 10 in model
// space), centered at (5, 2, -10)
rules::FinishLine testFinishLine() {
  glm::mat4 modelMatrix(1.0f);
  modelMatrix[3] = glm::vec4(5.0f, 2.0f, -10.0f, 1.0f);
  return rules::finishLine(modelMatrix, 10.0f);
}
}  // namespace

TEST_CASE("A checkpoint is passed within its radius, at any height",
          "[rules]") {
  CHECK(rules::passCheckpoint(atCheckpoint(0), 0) == 1);
  CHECK(rules::passCheckpoint(
            atCheckpoint(0, vec2(rules::CHECKPOINT_RADIUS - 0.1f, 0.0f)), 0) ==
        1);
  // jumping over it
  CHECK(rules::passCheckpoint(atCheckpoint(0) + vec3(0.0f, 20.0f, 0.0f), 0) ==
        1);

  CHECK(rules::passCheckpoint(
            atCheckpoint(0, vec2(0.0f, rules::CHECKPOINT_RADIUS + 0.1f)), 0) ==
        0);
}

TEST_CASE("Only the next checkpoint counts", "[rules]") {
  SECTION("Later checkpoints do not count before the earlier ones") {
    const std::size_t later = GENERATE(1, 2, 7);
    CAPTURE(later);
    CHECK(rules::passCheckpoint(atCheckpoint(later), 0) == 0);
  }

  SECTION("Passed checkpoints do not count again") {
    CHECK(rules::passCheckpoint(atCheckpoint(0), 1) == 1);
  }

  SECTION("After the last checkpoint nothing changes") {
    CHECK(rules::passCheckpoint(atCheckpoint(rules::CHECKPOINT_COUNT - 1),
                                rules::CHECKPOINT_COUNT) ==
          rules::CHECKPOINT_COUNT);
  }
}

TEST_CASE("The finish line trigger is as wide as the board and deeper than it",
          "[rules]") {
  const rules::FinishLine line = testFinishLine();
  CHECK(line.halfWidth == 10.0f);

  CHECK(rules::isOnFinishLine(vec3(5.0f, 2.0f, -10.0f), line));
  CHECK(rules::isOnFinishLine(vec3(14.9f, 2.0f, -10.0f),
                              line));  // at the end of the board
  CHECK(rules::isOnFinishLine(
      vec3(5.0f, 2.0f, -10.0f + rules::FINISH_LINE_HALF_DEPTH - 0.1f), line));
  CHECK(rules::isOnFinishLine(vec3(5.0f, 30.0f, -10.0f),
                              line));  // jumping over it

  CHECK_FALSE(rules::isOnFinishLine(vec3(15.1f, 2.0f, -10.0f),
                                    line));  // next to the board
  CHECK_FALSE(rules::isOnFinishLine(
      vec3(5.0f, 2.0f, -10.0f - rules::FINISH_LINE_HALF_DEPTH - 0.1f), line));
}

TEST_CASE(
    "The lap is complete at the finish line after all checkpoints in their "
    "order",
    "[rules]") {
  const rules::FinishLine line = testFinishLine();
  const vec3 onFinishLine(5.0f, 2.0f, -10.0f);

  SECTION("Around the track") {
    std::size_t nextCheckpoint = 0;
    for (std::size_t i = 0; i < rules::CHECKPOINT_COUNT; ++i) {
      CHECK_FALSE(rules::hasCompletedLap(nextCheckpoint, onFinishLine, line));
      nextCheckpoint = rules::passCheckpoint(atCheckpoint(i), nextCheckpoint);
    }
    CHECK(rules::hasCompletedLap(nextCheckpoint, onFinishLine, line));
    CHECK_FALSE(rules::hasCompletedLap(nextCheckpoint,
                                       onFinishLine + vec3(0.0f, 0.0f, 5.0f),
                                       line));  // not yet at the finish line
  }

  SECTION("Backwards around the track only the first checkpoint counts") {
    std::size_t nextCheckpoint = 0;
    for (std::size_t i = rules::CHECKPOINT_COUNT; i > 0; --i) {
      nextCheckpoint =
          rules::passCheckpoint(atCheckpoint(i - 1), nextCheckpoint);
    }
    CHECK(nextCheckpoint == 1);
    CHECK_FALSE(rules::hasCompletedLap(nextCheckpoint, onFinishLine, line));
  }
}

#ifndef TESTS_TESTSUPPORT_HPP_
#define TESTS_TESTSUPPORT_HPP_
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <filesystem>
#include <glm/ext/matrix_relational.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "Scene/GameRules.hpp"

struct GLFWwindow;
// Assimp types
// NOLINTNEXTLINE(readability-identifier-naming)
struct aiNode;
// NOLINTNEXTLINE(readability-identifier-naming)
struct aiScene;

namespace bibo::resources {
class SkinnedModel;
}

namespace bibo::world {
class WorldGenerator;
}

namespace bibo::test {
/**
 * @return Absolute path of a game asset in the source tree, e.g.
 * AssetPath("Model/SnowTerrain/track1.dae")
 */
std::filesystem::path AssetPath(const std::string& relative_path);

/**
 * The game loads its assets relative to the working directory: changes into the
 * asset folder and back again
 */
class AssetWorkingDirectory {
 public:
  AssetWorkingDirectory();
  ~AssetWorkingDirectory();
  AssetWorkingDirectory(const AssetWorkingDirectory&) = delete;
  AssetWorkingDirectory& operator=(const AssetWorkingDirectory&) = delete;

 private:
  std::filesystem::path previous_;
};

struct ShaderProgram {
  std::string vertex_shader;
  std::string fragment_shader;
  std::string defines;  // variant of the program (Shader::AddDefines)
};

/**
 * Shader programs of the game (Main.cpp InitShaders)
 */
const std::vector<ShaderProgram>& GameShaderPrograms();

const char* const kTrackFile = "Model/SnowTerrain/track1.dae";

/**
 * Nodes of the track file that the game loads (Scene, the start position of the
 * Bear, the homes of the hedgehogs)
 */
const std::vector<std::string>& GameNodeNames();

/**
 * Nodes that the game renders with a normal map (Scene)
 */
const std::vector<std::string>& NormalMappedNodeNames();

/**
 * The track file loaded with Assimp like the game does (Model), only loaded
 * once per test run
 */
const aiScene& TrackScene();

const char* const kBearFile = "Model/BearCub/BearCub.glb";

/**
 * The model of the bear loaded like the game does (SkinnedModel), only loaded
 * once per test run
 */
const resources::SkinnedModel& BearModel();

/**
 * The generator of the world around the track like the game creates it
 * (Scene::InitWorld: the start, the checkpoints, the objects it must not
 * cover), only created once per test run
 */
const world::WorldGenerator& SharedWorldGenerator();

/**
 * @return The node with this name in the track, fails the test if there is none
 */
aiNode& TrackNode(const std::string& name);

/**
 * Model matrix of a node like the game computes it (Geometry, Model): the
 * global transformation of the node
 */
glm::mat4 NodeModelMatrix(aiNode& node);

/**
 * The trigger at the finish line like Scene::InitFinishLine computes it from
 * the FinishLine node
 */
scene::rules::FinishLine TrackFinishLine();

enum class Clipped { kInside, kOutside, kBorder };

/**
 * Reference for view frustum culling: is the point inside of the volume the
 * view and projection matrix map onto the screen?
 * @return BORDER if the point is so close to the border of the volume that
 * rounding errors decide
 */
Clipped Clip(const glm::mat4& view_projection, glm::vec3 point);

/**
 * Hidden window with an OpenGL 4.3 core context, like the window of the game.
 * Skips the test if no context can be created (no display or OpenGL 4.3 not
 * supported, e.g. on CI servers without GPU). Errors reported by the driver
 * (debug output) are collected and can be checked with Errors().
 */
class GlContext {
 public:
  GlContext(int width = 800, int height = 600);
  ~GlContext();
  GlContext(const GlContext&) = delete;
  GlContext& operator=(const GlContext&) = delete;

  GLFWwindow& Window() const { return *glfw_window_; }
  /**
   * Errors reported by the OpenGL driver since the context was created
   */
  const std::vector<std::string>& Errors() const { return reported_errors_; }

 private:
  GLFWwindow* glfw_window_ = nullptr;
  std::vector<std::string> reported_errors_;
};

/**
 * Matches GLM vectors and matrices whose components differ by at most margin
 */
template <typename T>
class IsNearMatcher : public Catch::Matchers::MatcherBase<T> {
 public:
  IsNearMatcher(T expected, float margin)
      : expected_(expected), margin_(margin) {}

  bool match(const T& actual) const override {
    return glm::all(glm::equal(actual, expected_, margin_));
  }

  std::string describe() const override {
    std::ostringstream description;
    description << "is within " << margin_ << " of "
                << glm::to_string(expected_);
    return description.str();
  }

 private:
  T expected_;
  float margin_;
};

template <typename T>
IsNearMatcher<T> IsNear(T expected, float margin) {
  return IsNearMatcher<T>(expected, margin);
}
}  // namespace bibo::test

// Readable GLM values in the messages of failed assertions
namespace Catch {
template <glm::length_t L, typename T, glm::qualifier Q>
struct StringMaker<glm::vec<L, T, Q>> {
  // NOLINTNEXTLINE(readability-identifier-naming): Catch2 calls convert
  static std::string convert(const glm::vec<L, T, Q>& value) {
    return glm::to_string(value);
  }
};

template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct StringMaker<glm::mat<C, R, T, Q>> {
  // NOLINTNEXTLINE(readability-identifier-naming): Catch2 calls convert
  static std::string convert(const glm::mat<C, R, T, Q>& value) {
    return glm::to_string(value);
  }
};
}  // namespace Catch

#endif  // TESTS_TESTSUPPORT_HPP_

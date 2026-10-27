#ifndef BIBO_WORLD_FOLIAGEINTERACTION_HPP_
#define BIBO_WORLD_FOLIAGEINTERACTION_HPP_
#include <glm/glm.hpp>
#include <vector>

#include "../Render/FrameUniforms.hpp"

namespace bibo {
namespace world {
/**
 * The bear and the animals push the grass, the ferns and the bushes aside
 * (Shader/include/vegetation.glsl bends them). Where a body walked it leaves
 * imprints behind: the plants stay pressed down for a moment, then they spring
 * back up and swing past upright a bit. The bodies and the imprints closest to
 * the camera go to the shaders with the frame.
 */
class FoliageInteraction {
 public:
  /**
   * A body that moves through the plants, e.g. the bear (approximately a
   * sphere)
   */
  struct Body {
    glm::vec3 center;
    float radius;
  };

  /**
   * What pushes the plants aside: a body or where one just was
   */
  struct Imprint {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
    // direction of the movement (xz), 1: running
    glm::vec2 motion = glm::vec2(0.0f);
    // seconds since the body left it (negative: the body is here now)
    float age = 0.0f;
  };

  // How long the plants stay pressed down after a body left, how long they
  // swing back afterwards
  static constexpr float kHoldTime = 0.8f;
  static constexpr float kSpringTime = 1.8f;
  // Only the plants closer than this to the camera are bent (the grass is not
  // drawn further away)
  static constexpr float kRange = 60.0f;

  /**
   * Follows the bodies (in the same order every frame) and lets the imprints
   * age
   */
  void Update(float time_delta, const std::vector<Body>& bodies,
              glm::vec3 camera);
  /**
   * The selected bodies and imprints into foliage_interactors and
   * foliage_pushes, their number into wind.w
   */
  void FillFrameUniforms(render::FrameUniforms& uniforms) const;
  /**
   * @return The bodies and imprints for the shaders, the most important first
   * (at most kMaxFoliageInteractors)
   */
  const std::vector<Imprint>& GetSelected() const { return selected_; }
  std::size_t GetTrailLength() const { return trail_.size(); }

  /**
   * How far the plants are pushed aside: 1 while a body is there and for
   * kHoldTime after it left, then a damped swing back (a bit past upright), 0
   * after kSpringTime
   * @param age Seconds since the body left (negative: it is there)
   */
  static float Push(float age);

 private:
  struct Tracked {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 last_imprint = glm::vec3(0.0f);
    glm::vec2 motion = glm::vec2(0.0f);
    bool valid = false;
  };

  std::vector<Tracked> tracked_;
  std::vector<Imprint> trail_;
  std::vector<Imprint> selected_;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_FOLIAGEINTERACTION_HPP_

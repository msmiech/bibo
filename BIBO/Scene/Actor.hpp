#ifndef BIBO_SCENE_ACTOR_HPP_
#define BIBO_SCENE_ACTOR_HPP_
#include <glm/glm.hpp>

#include "../Physics/Physics.hpp"

namespace bibo {
namespace scene {
/**
 * What the scene needs of every animal (the hedgehogs, the beaver, Mishka, the
 * raccoons) to put it into the world: its collision sphere (around its body),
 * whether it is out, how it starts again
 */
class Animal {
 public:
  virtual ~Animal() = default;

  virtual glm::vec3 GetColliderCenter() const = 0;
  virtual float GetColliderRadius() const = 0;
  /**
   * @return false while it is away (e.g. a raccoon in the woods): it is not in
   * the way of anything then
   */
  virtual bool IsVisible() const { return true; }
  /**
   * Back to how it was at the start (e.g. when the game is restarted)
   */
  virtual void Reset() {}
  /**
   * The bear ran into it
   */
  virtual void Startle() {}
};

/**
 * An animal in the world (Actors): how it takes part in the physics and pushes
 * the plants aside. All animals are handled the same way, by these flags.
 */
struct Actor {
  Animal* animal = nullptr;
  // Its collision sphere in the physics world (kinematic, it follows the
  // animal), kInvalidBody while it has none
  physics::BodyHandle body = physics::kInvalidBody;
  // The bear cannot walk through it
  bool solid = true;
  // ... also not during the race (otherwise it is no obstacle then: it is
  // beside the route, but a racer may cut close past it)
  bool solid_while_racing = true;
  // Running into it knocks the bear over (the spines of the hedgehogs), the fur
  // of the bear lies on it
  bool knocks_down = false;
  // How much of its collision sphere pushes the plants aside (0: none)
  float foliage_scale = 1.0f;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_ACTOR_HPP_

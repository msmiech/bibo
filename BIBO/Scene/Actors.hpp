#ifndef BIBO_SCENE_ACTORS_HPP_
#define BIBO_SCENE_ACTORS_HPP_
#include <glm/glm.hpp>
#include <memory>
#include <random>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"
#include "../World/FoliageInteraction.hpp"
#include "Actor.hpp"
#include "Bear.hpp"
#include "Beaver.hpp"
#include "Camera.hpp"
#include "Events.hpp"
#include "Hedgehog.hpp"
#include "Raccoon.hpp"
#include "RestingBear.hpp"
#include "Salmon.hpp"
#include "StoneCircle.hpp"

namespace bibo {
namespace world {
class World;
}
namespace scene {
/**
 * The bear and the animals of the island: the hedgehogs that stroll around on
 * the track, the salmon that jump out of its pools, Bober the beaver at the
 * start, Mishka who lies at the largest pool and watches the salmon, Chonk the
 * raccoon at the trailhead of the foot paths, the raccoons that come out of the
 * woods to cheer for the bear, and the stone circle on the hill. Every animal
 * is an Actor: its collision sphere follows it in the physics world and it
 * pushes the plants aside, all the same way.
 *
 * Only the behavior, the scene draws them (SceneRenderer) and tells them what
 * happens on the quests (Situation).
 */
class Actors {
 public:
  // Mishka is only animated while the camera is this close (far away its fur
  // and its head are too small to see), the same for drawing Chonk
  static constexpr float kAnimateDistance = 90.0f;
  // Chonk is a well fed raccoon
  static constexpr float kChonkGirth = 1.45f;

  /**
   * Where the animals live that the track places (the others find their place
   * on the island)
   */
  struct Homes {
    std::vector<glm::vec2> hedgehogs;  // where the rocks of the track are
    // the beaver next to the starting line (on the ground), where it looks
    glm::vec3 beaver = glm::vec3(0.0f);
    glm::vec3 beaver_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  };
  /**
   * What happens on the quests in this frame
   */
  struct Situation {
    // The race runs (from the conversation on): no salmon jump, Mishka and
    // Chonk are no obstacles
    bool racing = false;
    // A quest runs that the raccoons come out of the woods to cheer for
    bool cheered_on = false;
    bool celebrate = false;  // the raccoons that cheer jump
    // A quest was completed: the raccoons go on cheering for a while
    bool completed = false;
    // No salmon jump out of these pools (empty: all are open)
    std::vector<bool> closed_pools;
    // The one the bear talks to (it looks at the bear), nullptr if nobody
    const TalkPartner* talking_to = nullptr;
  };

  /**
   * @param world Must stay alive as long as the actors
   * @param camera The raccoons come out of the woods where it does not see
   * them
   */
  Actors(std::unique_ptr<Bear> bear, const Homes& homes,
         const world::World& world, physics::PhysicsWorld& physics,
         Camera& camera);
  ~Actors();

  /**
   * The bear moves (with the input it was given, Bear::SetInput), the animals
   * react to it and to the quests, their collision spheres follow them
   * @param events Gets the splashes of the bear and the chatter of the
   * raccoons
   */
  void Update(float time_delta, const Situation& situation, EventQueue& events);
  /**
   * The bear and the animals go back to the start (the game is restarted)
   */
  void Restart();
  /**
   * The bear touched a physics body: an animal that knocks it down (the spines
   * of the hedgehogs) does, it tumbles and gets up again (the time is lost)
   */
  void OnBearHit(const physics::CharacterController::Hit& hit);
  /**
   * @return Who pushes the plants aside: the bear and the animals
   */
  std::vector<world::FoliageInteraction::Body> GetFoliageBodies() const;
  /**
   * @return The pool of the salmon that the bear ate up since the last call,
   * -1 if none (it counts for the salmon quest when it ate it, not when it
   * dropped it)
   */
  int TakeEatenPool();

  Bear& GetBear() { return *bear_; }
  const Bear& GetBear() const { return *bear_; }
  const std::vector<Hedgehog>& GetHedgehogs() const { return hedgehogs_; }
  const SalmonPools& GetSalmon() const { return *salmon_; }
  Beaver& GetBeaver() { return *beaver_; }
  const Beaver& GetBeaver() const { return *beaver_; }
  RestingBear& GetMishka() { return *mishka_; }
  const RestingBear& GetMishka() const { return *mishka_; }
  /**
   * The model of Mishka (darker fur than the bear's)
   */
  const resources::SkinnedModel& GetMishkaModel() const {
    return *mishka_model_;
  }
  /**
   * @return The pool where Mishka lies (the largest one)
   */
  int GetMishkaPool() const { return mishka_pool_; }
  Raccoon& GetChonk() { return *chonk_; }
  const Raccoon& GetChonk() const { return *chonk_; }
  const RaccoonFans& GetFans() const { return *fans_; }
  StoneCircle& GetStoneCircle() { return stones_; }
  const StoneCircle& GetStoneCircle() const { return stones_; }

 private:
  /**
   * The hedgehogs live where the rocks of the track were, the salmon in the
   * water on the track
   */
  void InitHedgehogs(const Homes& homes);
  /**
   * Mishka at the largest pool of water
   */
  void InitMishka();
  /**
   * The stone circle on the hill, Chonk at the trailhead of the paths to it
   * and the raccoons that cheer
   */
  void InitRaccoons();
  /**
   * The animal is in the world from now on (its collision sphere if it is
   * solid)
   */
  void Add(Animal& animal, Actor actor);
  /**
   * The collision spheres follow the animals: only the solid ones have one
   * (during the race only the ones that are solid then)
   */
  void UpdateBodies(float time_delta, bool racing);
  /**
   * Can a raccoon walk there (dry land, not in a cave)?
   */
  bool RaccoonGround(glm::vec2 position, float& height) const;
  /**
   * A spot for a raccoon that cheers, ahead of the bear beside its way (not on
   * the trail), and where it comes from: further out to the side, out of sight
   * of the camera, with nothing in the way (RaccoonFans::Finder)
   */
  bool FindFanSpot(glm::vec3 bear, glm::vec3 heading, std::mt19937& random,
                   glm::vec2& from, glm::vec2& spot);
  /**
   * Mishka watches the salmon of its pool, and the bear when it comes close
   */
  void UpdateMishka(float time_delta, bool talking);

  const world::World& world_;
  physics::PhysicsWorld& physics_;
  Camera& camera_;

  std::unique_ptr<Bear> bear_;
  std::vector<Hedgehog> hedgehogs_;
  std::unique_ptr<SalmonPools> salmon_;
  std::unique_ptr<Beaver> beaver_;
  std::unique_ptr<resources::SkinnedModel> mishka_model_;
  std::unique_ptr<RestingBear> mishka_;
  int mishka_pool_ = -1;
  std::unique_ptr<Raccoon> chonk_;
  std::unique_ptr<RaccoonFans> fans_;
  StoneCircle stones_;
  // All animals
  std::vector<Actor> actors_;

  // the salmon in the mouth of the bear is from this pool, -1 if none
  int carried_pool_ = -1;
  // the bear ate up a salmon of this pool (for TakeEatenPool)
  int eaten_pool_ = -1;
  // the raccoons go on cheering for a while after a quest was completed
  float cheer_linger_ = 0.0f;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_ACTORS_HPP_

#ifndef BIBO_SCENE_BEAR_HPP_
#define BIBO_SCENE_BEAR_HPP_
#include <assimp/scene.h>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"
#include "BearCharacter.hpp"

namespace bibo {
namespace scene {
/**
 * The player character: a young brown bear (Model/BearCub/BearCub.glb) with a
 * skeleton, procedural animation, fur strands and a ragdoll (see BearCharacter,
 * BearRig). The scene sets what the player wants it to do (SetInput). Only the
 * behavior, the scene draws it (BearRenderer).
 */
class Bear {
 public:
  static const char* const kModelFile;
  static constexpr std::size_t kStrandCount = 60000;

  /**
   * @param assimp_scene The track: the bear starts where its node BearBody is
   * @param onHit Called when the bear touches a body (e.g. to knock it down)
   */
  Bear(const aiScene* assimp_scene, physics::PhysicsWorld* physics_world,
       physics::CharacterController::HitCallback on_hit);
  ~Bear();

  void Update(float time_delta);
  /**
   * What the player wants the bear to do in the next updates
   */
  void SetInput(const BearInput& input) { this->input_ = input; }
  glm::vec3 GetPosition() const;
  glm::vec3 GetDirection() const;

  /**
   * Moves the bear back to its start position and direction (e.g. when the game
   * is restarted)
   */
  void Reset();
  /**
   * The bear tumbles as a ragdoll, then gets up again (e.g. after running into
   * a rock)
   */
  void KnockDown(const physics::CharacterController::Hit& hit);
  /**
   * @return true while the bear tumbles or gets up (the player cannot control
   * it)
   */
  bool IsKnockedDown() const;
  /**
   * Obstacles that the fur lies on
   */
  void SetObstacles(const std::vector<fur::SphereCollider>& obstacles);
  BearCharacter& GetCharacter() { return *character_; }
  const BearCharacter& GetCharacter() const { return *character_; }
  /**
   * The model of the bear (its skin moves with the character)
   */
  const resources::SkinnedModel& GetModel() const { return *model_; }
  /**
   * Shape and movement limits of the bear's physics capsule
   */
  static physics::CharacterControllerDesc ControllerDesc(glm::vec3 position);

 private:
  std::unique_ptr<resources::SkinnedModel> model_;
  std::unique_ptr<BearCharacter> character_;
  BearInput input_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEAR_HPP_

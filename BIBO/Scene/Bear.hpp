#ifndef BIBO_SCENE_BEAR_HPP_
#define BIBO_SCENE_BEAR_HPP_
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Resources/SkinnedModel.hpp"
#include "BearCharacter.hpp"
#include "BearRenderer.hpp"
#include "Geometry.hpp"

namespace bibo {
namespace scene {
/**
 * The player character: a young brown bear (Model/BearCub/BearCub.glb) with a
 * skeleton, procedural animation, fur strands and a ragdoll (see BearCharacter,
 * BearRig, BearRenderer). The scene sets what the player wants it to do
 * (SetInput).
 */
class Bear : public Geometry {
 public:
  static const char* const kModelFile;
  static constexpr std::size_t kStrandCount = 60000;

  /**
   * @param assimp_scene The track: the bear starts where its node BearBody is
   * @param onHit Called when the bear touches a body (e.g. to knock it down)
   */
  Bear(GLFWwindow& window, const aiScene* assimp_scene,
       physics::PhysicsWorld* physics_world,
       physics::CharacterController::HitCallback on_hit);
  ~Bear() override;

  void Update(float time_delta) override;
  /**
   * What the player wants the bear to do in the next updates
   */
  void SetInput(const BearInput& input) { this->input_ = input; }
  /**
   * Draws the surface of the bear with the given shader (e.g. into the shadow
   * map)
   */
  void Draw(Shader* shader) override;
  /**
   * Draws the bear with its fur (in the main pass)
   */
  void DrawShaded(const BearRenderContext& context);
  glm::vec3 GetPosition() override;
  glm::vec3 GetDirection() override;
  bool IsVisible(const ViewFrustum& frustum) override;

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
  /**
   * Shape and movement limits of the bear's physics capsule
   */
  static physics::CharacterControllerDesc ControllerDesc(glm::vec3 position);

 private:
  void UploadIfChanged();

  std::unique_ptr<resources::SkinnedModel> model_;
  std::unique_ptr<BearCharacter> character_;
  std::unique_ptr<BearRenderer> renderer_;
  unsigned int uploaded_version_ = 0;
  BearInput input_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_BEAR_HPP_

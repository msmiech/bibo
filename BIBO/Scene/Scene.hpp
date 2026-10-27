#ifndef BIBO_SCENE_SCENE_HPP_
#define BIBO_SCENE_SCENE_HPP_
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Render/Renderer.hpp"
#include "../Util/Input.hpp"
#include "../Util/Util.hpp"
#include "../World/World.hpp"
#include "Bear.hpp"
#include "Beaver.hpp"
#include "Camera.hpp"
#include "CreatureRenderer.hpp"
#include "FurryRenderer.hpp"
#include "GameRules.hpp"
#include "Geometry.hpp"
#include "Hedgehog.hpp"
#include "HedgehogRenderer.hpp"
#include "Raccoon.hpp"
#include "RaceQuest.hpp"
#include "RestingBear.hpp"
#include "Salmon.hpp"
#include "SalmonQuest.hpp"
#include "SceneObject.hpp"
#include "StonesQuest.hpp"

namespace bibo {
namespace scene {
/**
 * The island: the bear, the objects of the track (finish line, signs, ...), the
 * animals (hedgehogs that stroll around on the track, salmon that jump out of
 * the water, the beaver at the start who offers the race as a quest, Mishka the
 * bear at the largest pool who offers the salmon as a quest) and the world
 * around it (World: the island with its forest, caves, sky, water and snow).
 * The bear roams freely, swims in the water and catches and eats salmon; the
 * race around the track (RaceQuest) and fishing in all pools (SalmonQuest) are
 * quests, so is exploring the standing stones for Chonk, the raccoon at the
 * trailhead of the foot paths (StonesQuest), one at a time. On the race and
 * while fishing raccoons come out of the woods to cheer (RaccoonFans). The
 * player talks to the closest animal. During the conversations and the cutscene
 * of the race the scene moves the bear and the camera. No salmon jump during
 * the race.
 */
class Scene : public render::RenderableScene {
 public:
  /**
   * Who the player talks to (or would talk to when it presses the button)
   */
  enum class TalkPartner { kNone, kBeaver, kMishka, kChonk, kStones };

 private:
  void InitPhysicsBodies();
  void InitWorld(const aiScene& track);
  /**
   * The hedgehogs live where the rocks of the track were, the salmon in the
   * water on the track
   */
  void InitAnimals();
  /**
   * @param board The finish line (geometry), the trigger goes along it across
   * the track
   */
  void InitFinishLine(Geometry* board);
  /**
   * The beaver next to the starting line, the race quest, and the start of the
   * bear behind the line
   */
  void InitQuest();
  /**
   * Mishka at the largest pool of water, and the salmon quest
   */
  void InitMishka();
  /**
   * Chonk at the trailhead of the paths to the standing stones, the stones
   * quest and the raccoons that cheer
   */
  void InitChonk();
  /**
   * What the bear does for the quest in this frame (where it is, whether it
   * talks to the beaver, ...)
   */
  RaceQuest::Situation QuestSituation(const input::PlayerInput& player);
  SalmonQuest::Situation SalmonSituation(const input::PlayerInput& player);
  StonesQuest::Situation StonesSituation(const input::PlayerInput& player);
  /**
   * Who the player talks to when it presses the button: the closest animal it
   * can talk to (or the one it talks to)
   */
  void UpdateTalkPartner();
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
   * @return true if the bear can talk now (standing, not swimming or eating)
   * and is within the distance of a point
   */
  bool CanTalkAt(glm::vec3 position, float distance);
  /**
   * @return true while the player moves the bear (not in a conversation or the
   * cutscene)
   */
  bool IsPlayerInControl() const {
    return race_quest_->IsPlayerInControl() &&
           salmon_quest_->IsPlayerInControl() &&
           stones_quest_->IsPlayerInControl();
  }
  /**
   * The input of the bear: from the player, or from the cutscene and the
   * conversation
   */
  BearInput MakeBearInput(const input::PlayerInput& player);
  /**
   * Turns the bear on the spot towards a direction
   * @return true if it looks there
   */
  bool TurnTowards(glm::vec3 direction, BearInput& input);
  /**
   * The camera of the conversation and the cutscene (or the camera that follows
   * the bear)
   */
  void UpdateCamera(float time_delta);
  /**
   * The ground under a point (the height field of the world)
   */
  glm::vec3 GroundAt(glm::vec3 position) const;
  // Called when the bear touches a physics body: the hedgehogs knock it down
  void OnBearHit(const physics::CharacterController::Hit& hit);
  void UpdateAnimals(float time_delta);
  // Puts the parts of the animals where they are drawn in this frame
  void PlaceAnimals();
  void PlaceSalmon(glm::vec3 center, const glm::quat& rotation, int bites,
                   float wiggle);
  // The skinned vertices of Mishka to its renderer (when they changed)
  void UploadMishka();
  // What the camera follows: the bear
  Camera::FollowTarget CameraTarget();
  /**
   * The objects of the track (not the world)
   * @param frustum Geometries outside of it are not drawn (nullptr: draw all)
   * @param with_bear The bear is drawn with the given shader too (its surface),
   * otherwise left out
   * @return Number of drawn geometries
   */
  int DrawGeometries(Shader* shader, const glm::mat4& view,
                     const glm::mat4& projection, const ViewFrustum* frustum,
                     bool with_bear);
  /**
   * @return false if the geometry is outside of the frustum and was not drawn
   */
  bool DrawGeometry(Shader* shader, Geometry* geometry,
                    GLint normal_mapping_enabled_location,
                    const ViewFrustum* frustum);
  Geometry* AddGeometry(const aiScene* track, const char* node_name,
                        Texture* texture, Texture* normal_map = nullptr);

  GLFWwindow* window_;
  Shader* main_shader_;
  std::unique_ptr<Shader> depth_shader_;
  std::unique_ptr<Shader> light_shader_;
  std::vector<std::unique_ptr<Geometry>> geometries_;
  std::unique_ptr<Camera> camera_;
  std::vector<std::unique_ptr<Texture>> textures_;
  std::vector<std::unique_ptr<Geometry>> arrows_;
  std::unique_ptr<world::World> world_;

  // special pointers
  Bear* bear_;
  Geometry* finish_line_;
  Geometry* crate_;
  Texture* crate_normal_map_;

  // Animals
  std::vector<glm::vec2> hedgehog_homes_;  // where the rocks of the track are
  std::vector<Hedgehog> hedgehogs_;
  std::vector<physics::BodyHandle> hedgehog_bodies_;
  std::unique_ptr<SalmonPools> salmon_;
  std::unique_ptr<CreatureRenderer> creatures_;
  std::unique_ptr<HedgehogRenderer> hedgehog_renderer_;
  std::unique_ptr<Beaver> beaver_;
  physics::BodyHandle beaver_body_ = physics::kInvalidBody;
  std::unique_ptr<FurryRenderer> beaver_renderer_;
  // Mishka, who lies at the largest pool and watches the salmon (its model has
  // darker fur than the bear's)
  std::unique_ptr<resources::SkinnedModel> mishka_model_;
  std::unique_ptr<RestingBear> mishka_;
  std::unique_ptr<BearRenderer> mishka_renderer_;
  unsigned int mishka_uploaded_ = 0;
  physics::BodyHandle mishka_body_ = physics::kInvalidBody;
  int mishka_pool_ = -1;
  // the salmon in the mouth of the bear is from this pool, -1 if none
  int carried_pool_ = -1;
  // the bear ate up a salmon of this pool (for the next update of the salmon
  // quest)
  int eaten_pool_ = -1;
  // Chonk, the raccoon at the trailhead, and the raccoons that come out of the
  // woods to cheer
  std::unique_ptr<Raccoon> chonk_;
  physics::BodyHandle chonk_body_ = physics::kInvalidBody;
  std::unique_ptr<FurryRenderer> chonk_renderer_;
  std::unique_ptr<RaccoonFans> fans_;
  std::unique_ptr<FurryRenderer> fans_renderer_;
  // the fan is drawn under the island (its pose need not change)
  std::vector<bool> fans_hidden_;
  bool chonk_posed_ = false;
  // the raccoons go on cheering for a while after a quest was completed
  float cheer_linger_ = 0.0f;
  int raccoon_chatters_ = 0;  // for TakeRaccoonChatters

  // Physics
  physics::PhysicsWorld* physics_world_;

  bool view_frustum_culling_enabled_ = true;
  bool normal_mapping_enabled_ = true;
  int drawn_geometry_count_ = 0;

  // The race around the track
  rules::FinishLine finish_line_trigger_;
  std::unique_ptr<RaceQuest> race_quest_;
  RaceQuest::Stage last_stage_ = RaceQuest::Stage::kIdle;
  bool at_start_ = false;  // the bear reached the starting line in the cutscene
  // after a conversation until the jump button is released (it may have
  // answered)
  bool jump_blocked_ = false;
  TalkPartner talk_partner_ = TalkPartner::kNone;
  std::vector<RaceQuest::Event> events_;  // for TakeEvents
  // Fishing in all pools
  std::unique_ptr<SalmonQuest> salmon_quest_;
  std::vector<SalmonQuest::Event> salmon_events_;  // for TakeSalmonEvents
  // Exploring the standing stones for Chonk
  std::unique_ptr<StonesQuest> stones_quest_;
  std::vector<StonesQuest::Event> stones_events_;  // for TakeStonesEvents
  glm::vec3 stones_center_ =
      glm::vec3(0.0f);  // the flat stone in the middle of the circle
  float stones_radius_ = 0.0f;
  float marker_time_ =
      0.0f;  // the mark above the stone circle bobs up and down
  int splashes_ = 0;

  // Dynamic bodies and their start poses, which they are moved back to by
  // Restart()
  struct DynamicBody {
    physics::BodyHandle body;
    glm::vec3 position;
    glm::quat rotation;
  };
  std::vector<DynamicBody> dynamic_bodies_;

 public:
  Scene(GLFWwindow* window, Shader* shader,
        physics::PhysicsWorld* physics_world);
  ~Scene() override;

  /**
   * @param player What the player does: moves the bear (relative to the
   * camera), jumps and turns the camera
   */
  void Update(float time_delta, const input::PlayerInput& player = {});
  /**
   * @return false while the camera flies freely (L), then WASD moves the camera
   * instead of the bear
   */
  bool IsCameraLocked() { return camera_->IsLocked(); }
  Camera& GetCamera() { return *camera_; }
  Bear& GetBear() { return *bear_; }
  world::World& GetWorld() { return *world_; }
  const std::vector<Hedgehog>& GetHedgehogs() const { return hedgehogs_; }
  const SalmonPools& GetSalmon() const { return *salmon_; }
  /**
   * Starts the run again: the bear, the movable objects, the animals, the lap
   * and the score go back to the start (R key, game over menu)
   */
  void Restart();

  const RaceQuest& GetRaceQuest() const { return *race_quest_; }
  const Beaver& GetBeaver() const { return *beaver_; }
  const SalmonQuest& GetSalmonQuest() const { return *salmon_quest_; }
  const RestingBear& GetMishka() const { return *mishka_; }
  /**
   * @return The pool where Mishka lies (the largest one)
   */
  int GetMishkaPool() const { return mishka_pool_; }
  const StonesQuest& GetStonesQuest() const { return *stones_quest_; }
  const Raccoon& GetChonk() const { return *chonk_; }
  const RaccoonFans& GetFans() const { return *fans_; }
  /**
   * @return The flat stone in the middle of the stone circle (on the ground)
   */
  glm::vec3 GetStonesCenter() const { return stones_center_; }
  TalkPartner GetTalkPartner() const { return talk_partner_; }
  /**
   * @return true if the bear is in the water (swimming or wading deep), the
   * race is lost then
   */
  bool IsBearInWater();
  /**
   * @return What happened in the quest since the last call (e.g. for the
   * sounds)
   */
  std::vector<RaceQuest::Event> TakeEvents();
  std::vector<SalmonQuest::Event> TakeSalmonEvents();
  std::vector<StonesQuest::Event> TakeStonesEvents();
  /**
   * @return How often the raccoons chattered since the last call (Chonk while
   * it talks, the fans when they cheer)
   */
  int TakeRaccoonChatters();
  /**
   * @return How often the bear fell into the water since the last call
   */
  int TakeSplashes();

  // render::RenderableScene (the frame is rendered by render::Renderer)
  render::CameraView CameraView() override;
  void FillFrameUniforms(render::FrameUniforms& uniforms) override;
  void PrepareFrame() override;
  void DrawShadowCasters(const render::ShadowCascade& cascade) override;
  void DrawMain(const render::RenderSettings& settings) override;

  /**
   * Only the objects of the track with the given shader (e.g. their
   * silhouettes), the camera's view and projection
   */
  void RenderFromCamera(Shader* shader);
  /**
   * Size of the rendered image in pixels (e.g. after the window was resized)
   */
  void SetViewportSize(int width, int height);
  /**
   * Only the geometries that are (partially) inside of the view frustum of the
   * camera are drawn
   */
  void SetViewFrustumCullingEnabled(bool view_frustum_culling_enabled);
  bool IsViewFrustumCullingEnabled();
  /**
   * @return Number of geometries drawn by the last RenderFromCamera or DrawMain
   */
  int GetDrawnGeometryCount();
  /**
   * @return true while the bear tumbles after running into an obstacle or gets
   * up again
   */
  bool IsBearKnockedDown();
  /**
   * @return Number of geometries in the scene
   */
  int GetGeometryCount();
  /**
   * @return Points of the salmon quest (the salmon the bear ate on it, none
   * outside of the quest)
   */
  int GetScore() const { return salmon_quest_->GetScore(); }
  int GetSalmonEaten() const { return salmon_quest_->GetEatenCount(); }
  int GetPoolCount() const {
    return static_cast<int>(salmon_->GetPools().size());
  }
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_SCENE_HPP_

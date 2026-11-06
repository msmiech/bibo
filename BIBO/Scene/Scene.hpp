#ifndef BIBO_SCENE_SCENE_HPP_
#define BIBO_SCENE_SCENE_HPP_
#include <GL/glew.h>

#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Util/Input.hpp"
#include "../Util/Util.hpp"
#include "../World/World.hpp"
#include "Actors.hpp"
#include "Bear.hpp"
#include "Camera.hpp"
#include "Events.hpp"
#include "GameRules.hpp"
#include "Geometry.hpp"
#include "QuestDirector.hpp"
#include "RaceQuest.hpp"
#include "SalmonQuest.hpp"
#include "SceneRenderer.hpp"
#include "StonesQuest.hpp"

namespace bibo {
namespace scene {
/**
 * The island: the bear and the animals (Actors), the quests (QuestDirector),
 * the objects of the track (finish line, signs, ...) and the world around it
 * (World: the island with its forest, caves, sky, water and snow), drawn by the
 * SceneRenderer. The bear roams freely, swims in the water and catches and eats
 * salmon; the race around the track (RaceQuest), fishing in all pools
 * (SalmonQuest) and exploring the standing stones for Chonk (StonesQuest) are
 * quests, one at a time. The player talks to the closest animal. During the
 * conversations and the cutscene of the race the scene moves the bear and the
 * camera.
 */
class Scene {
 public:
  Scene(GLFWwindow* window, Shader* shader,
        physics::PhysicsWorld* physics_world);
  ~Scene();

  /**
   * @param player What the player does: moves the bear (relative to the
   * camera), jumps and turns the camera
   */
  void Update(float time_delta, const input::PlayerInput& player = {});
  /**
   * Starts the run again: the bear, the movable objects, the animals, the lap
   * and the score go back to the start (R key, game over menu)
   */
  void Restart();
  /**
   * @return What happened since the last call (e.g. for the sounds and the
   * messages)
   */
  std::vector<SceneEvent> TakeEvents() { return events_.Take(); }

  /**
   * @return false while the camera flies freely (L), then WASD moves the camera
   * instead of the bear
   */
  bool IsCameraLocked() { return camera_->IsLocked(); }
  Camera& GetCamera() { return *camera_; }
  /**
   * Size of the rendered image in pixels (e.g. after the window was resized)
   */
  void SetViewportSize(int width, int height);
  world::World& GetWorld() { return *world_; }
  const Actors& GetActors() const { return *actors_; }
  Bear& GetBear() { return actors_->GetBear(); }
  const Bear& GetBear() const { return actors_->GetBear(); }
  const QuestDirector& GetQuests() const { return *quests_; }
  const RaceQuest& GetRaceQuest() const { return *race_quest_; }
  const SalmonQuest& GetSalmonQuest() const { return *salmon_quest_; }
  const StonesQuest& GetStonesQuest() const { return *stones_quest_; }
  /**
   * @return Who the bear talks to, or would talk to when the player presses the
   * button, nullptr if nobody
   */
  const TalkPartner* GetTalkPartner() const;
  /**
   * Draws the scene (for render::Renderer)
   */
  SceneRenderer& GetRenderer() { return *renderer_; }

  /**
   * @return true if the bear is in the water (swimming or wading deep), the
   * race is lost then
   */
  bool IsBearInWater() const;
  /**
   * @return true while the bear tumbles after running into an obstacle or gets
   * up again
   */
  bool IsBearKnockedDown() const { return GetBear().IsKnockedDown(); }
  /**
   * @return Points of the salmon quest (the salmon the bear ate on it, none
   * outside of the quest)
   */
  int GetScore() const { return salmon_quest_->GetScore(); }
  int GetSalmonEaten() const { return salmon_quest_->GetEatenCount(); }
  int GetPoolCount() const {
    return static_cast<int>(actors_->GetSalmon().GetPools().size());
  }

 private:
  void InitPhysicsBodies();
  /**
   * @return Where the hedgehogs live: where the rocks of the track are
   */
  std::vector<glm::vec2> InitWorld(const aiScene& track, const Bear& bear);
  /**
   * @param board The finish line (geometry), the trigger goes along it across
   * the track
   */
  void InitFinishLine(Geometry* board);
  /**
   * The start of the bear behind the starting line
   * @return Where the beaver sits, next to the line
   */
  Actors::Homes InitStart(Bear& bear);
  /**
   * The race, fishing for Mishka and the standing stones for Chonk
   */
  void InitQuests();
  /**
   * @return true if the bear can talk now (standing, not swimming or eating)
   * and is within the distance of a point
   */
  bool CanTalkAt(glm::vec3 position, float distance) const;
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
   * The race starts at the line, looking along the track (also if the bear did
   * not get there in the cutscene, e.g. it was knocked over)
   */
  void PlaceAtStart();
  /**
   * The camera of the conversation and the cutscene (or the camera that follows
   * the bear)
   */
  void UpdateCamera(float time_delta);
  /**
   * The ground under a point (the height field of the world)
   */
  glm::vec3 GroundAt(glm::vec3 position) const;
  // What the camera follows: the bear
  Camera::FollowTarget CameraTarget() const;
  Geometry* AddGeometry(const aiScene* track, const char* node_name,
                        Texture* texture, Texture* normal_map = nullptr);

  Shader* main_shader_;
  physics::PhysicsWorld* physics_world_;
  std::unique_ptr<Camera> camera_;
  std::vector<std::unique_ptr<Texture>> textures_;
  // The objects of the track and its signs
  std::vector<std::unique_ptr<Geometry>> geometries_;
  std::vector<std::unique_ptr<Geometry>> arrows_;
  Geometry* finish_line_;
  Geometry* crate_;
  std::unique_ptr<world::World> world_;
  std::unique_ptr<Actors> actors_;
  std::unique_ptr<QuestDirector> quests_;
  RaceQuest* race_quest_ = nullptr;
  SalmonQuest* salmon_quest_ = nullptr;
  StonesQuest* stones_quest_ = nullptr;
  std::unique_ptr<SceneRenderer> renderer_;
  EventQueue events_;

  // The race around the track
  rules::FinishLine finish_line_trigger_;
  RaceQuest::Stage last_stage_ = RaceQuest::Stage::kIdle;
  bool at_start_ = false;  // the bear reached the starting line in the cutscene
  // after a conversation until the jump button is released (it may have
  // answered)
  bool jump_blocked_ = false;

  // Dynamic bodies and their start poses, which they are moved back to by
  // Restart()
  struct DynamicBody {
    physics::BodyHandle body;
    glm::vec3 position;
    glm::quat rotation;
  };
  std::vector<DynamicBody> dynamic_bodies_;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_SCENE_HPP_

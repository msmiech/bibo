#pragma once
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

namespace bibo {
namespace scene {
/**
 * A raccoon of the forest. Fans come out of the woods while the bear is on a
 * quest: one trots to a spot beside the way of the bear, rears up on its hind
 * legs, turns to the bear, waves its paws, hops and chatters, and trots back
 * into the woods after a while. Chonk sits at its place instead: upright on its
 * haunches, it washes its paws every now and then and looks at the bear when it
 * comes close; while it talks its mouth moves with every sound and it gestures
 * with its paws. Only the behavior, the scene draws it (FurryRenderer with the
 * RaccoonModel).
 */
class Raccoon {
 public:
  /**
   * Can a raccoon walk at a point of the ground plane?
   * @param height The height of the ground there
   */
  using Ground = std::function<bool(glm::vec2 position, float& height)>;

  enum class Activity {
    HIDDEN,    // in the woods, not drawn
    COMING,    // trots out of the woods to its spot
    CHEERING,  // stands up at its spot and cheers for the bear
    LEAVING,   // trots back into the woods
    SITTING    // sits at its place (Chonk)
  };

  // Size of the model (RaccoonModel) in the world
  static constexpr float SCALE = 2.1f;
  static constexpr float TROT_SPEED = 3.2f;
  // It notices the bear within this distance (sitting)
  static constexpr float ATTENTION_DISTANCE = 14.0f;

  /**
   * @param girth How round it is (RaccoonModel::build), its collision sphere is
   * larger
   */
  Raccoon(Ground ground, std::uint32_t seed, float girth = 1.0f);

  /**
   * It appears at a point in the woods and trots to the spot, where it cheers
   */
  void comeAndCheer(glm::vec2 from, glm::vec2 spot);
  /**
   * Trots back to where it came from and disappears there (nothing happens if
   * it is hidden or sitting)
   */
  void leave();
  /**
   * Sits at a place, looking in a direction (when the bear is not close)
   */
  void sitAt(glm::vec3 position, glm::vec3 direction);
  /**
   * @param bear Where the head of the bear is (it turns to it and looks at it)
   */
  void update(float timeDelta, glm::vec3 bear);
  /**
   * A big jump with both paws up (if it is out of the woods)
   */
  void cheer();
  /**
   * Talking (the dialog is open): it gestures with its paws and does not wash
   * them
   */
  void setTalking(bool talking);
  /**
   * One sound while talking: the mouth opens and closes, the head nods
   */
  void speak();
  /**
   * Back to how it was at the start (hidden, or at its place)
   */
  void reset();

  Activity getActivity() const { return activity; }
  bool isVisible() const { return activity != Activity::HIDDEN; }
  /**
   * Seconds since the activity changed
   */
  float getActivityTime() const { return activityTime; }
  /**
   * On the ground under its middle
   */
  glm::vec3 getPosition() const { return position; }
  glm::vec3 getDirection() const {
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
  }
  /**
   * 0 on all fours, 1 upright on its hind legs
   */
  float getUpright() const { return rear; }
  float getMouthOpen() const { return mouth; }
  bool isTalking() const { return talking; }
  /**
   * Between its eyes (world space)
   */
  glm::vec3 getFacePosition() const;
  /**
   * Where the exclamation mark floats (above its head), it bobs up and down; it
   * turns slowly
   */
  glm::vec3 getMarkerPosition() const;
  glm::quat getMarkerRotation() const;
  /**
   * Its collision sphere (the bear cannot walk through Chonk)
   */
  glm::vec3 getColliderCenter() const;
  float getColliderRadius() const;
  /**
   * @return The number of chatters since the last call (when it hops and
   * cheers)
   */
  int takeChatters();

  /**
   * Model matrices of the parts of the RaccoonModel (with the scale), indexed
   * by RaccoonModel::Part
   */
  std::vector<glm::mat4> getPose() const;

 private:
  float uniform(float from, float to);
  void setActivity(Activity next);
  // Walks towards the target, true when it is there (or cannot go on)
  bool walk(float timeDelta, float speed);
  void turnTo(float wantedYaw, float rate, float timeDelta);

  Ground ground;
  std::uint32_t seed;
  float girth;
  std::mt19937 random;

  Activity activity = Activity::HIDDEN;
  float activityTime = 0.0f;
  glm::vec3 home = glm::vec3(0.0f);  // where it sits
  glm::vec3 homeDirection = glm::vec3(0.0f, 0.0f, 1.0f);
  bool sits = false;  // it has a place (sitAt)
  glm::vec2 origin = glm::vec2(0.0f), target = glm::vec2(0.0f);

  glm::vec3 position = glm::vec3(0.0f);
  float yaw = 0.0f;
  float rear = 0.0f;  // 0 to 1: upright
  float gaitPhase = 0.0f, stride = 0.0f;
  float headYaw = 0.0f, headPitch = 0.0f;
  float time = 0.0f;
  bool talking = false;
  float talkBlend = 0.0f;
  float mouth = 0.0f, mouthTime = -1.0f;
  float nod = 0.0f;
  float hopTime = -1.0f, nextHop = 1.0f;  // small hops while cheering
  float jumpTime = -1.0f;                 // the big jump (cheer)
  float washTime = -1.0f, nextWash = 4.0f;
  int chatters = 0;
};

/**
 * The raccoons that come out of the woods to cheer for the bear while it is on
 * a quest: every few seconds one comes to a spot ahead of the bear beside its
 * way (at most a few at a time), watches for a while and goes back into the
 * woods when the bear is past or the quest is over. They all jump when
 * something good happens (celebrate).
 */
class RaccoonFans {
 public:
  static constexpr std::size_t COUNT = 4;
  static constexpr int MAX_WATCHING = 3;
  // A raccoon watches at most this long, and goes when the bear is this far
  // away
  static constexpr float WATCH_TIME = 9.0f, GONE_DISTANCE = 38.0f;
  // They keep this distance to each other
  static constexpr float SPACING = 3.5f;

  /**
   * Finds a spot for a raccoon ahead of the bear beside its way and where it
   * comes from (in the woods)
   * @param heading Where the bear goes (horizontal, length 1)
   * @return false if there is none
   */
  using Finder = std::function<bool(glm::vec3 bear, glm::vec3 heading,
                                    std::mt19937& random, glm::vec2& from,
                                    glm::vec2& spot)>;

  RaccoonFans(Raccoon::Ground ground, Finder finder, std::uint32_t seed = 515u);

  /**
   * @param cheering The bear is on a quest (the raccoons come), otherwise they
   * go back into the woods
   * @param bear Where the bear is, its head, where it goes (horizontal, length
   * 1)
   */
  void update(float timeDelta, bool cheering, glm::vec3 bear,
              glm::vec3 bearHead, glm::vec3 heading);
  /**
   * All raccoons that are out jump with their paws up
   */
  void celebrate();
  /**
   * All back in the woods
   */
  void reset();
  const std::vector<Raccoon>& getRaccoons() const { return raccoons; }
  int getWatchingCount() const;
  /**
   * @return The number of chatters of all raccoons since the last call
   */
  int takeChatters();

 private:
  Finder finder;
  std::uint32_t seed;
  std::mt19937 random;
  std::vector<Raccoon> raccoons;
  float nextArrival = 1.0f;
};
}  // namespace scene
}  // namespace bibo

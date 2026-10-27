#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <numbers>

#include "SceneObject.hpp"
#include "ViewFrustum.hpp"

using namespace glm;

namespace bibo {
namespace scene {
/**
 * Third person camera that orbits around the bear (locked, default): the mouse
 * or the right stick turn it around the bear, it follows the bear smoothly,
 * goes back behind it when the player does not turn it for a while and does not
 * go into the terrain or through rocks. Unlocked (L) it is a free flying camera
 * (WASD and the mouse).
 *
 * The camera looks along (cos(pitch) sin(yaw), -sin(pitch), cos(pitch)
 * cos(yaw)): yaw 0 looks along +z, a positive pitch looks down.
 */
class Camera : public SceneObject {
 public:
  /**
   * What the locked camera follows
   */
  struct FollowTarget {
    vec3 position = vec3(0.0f);
    vec3 direction =
        vec3(0.0f, 0.0f,
             1.0f);  // where the target looks (the camera goes behind it)
    vec3 velocity = vec3(0.0f);
    bool airborne =
        false;  // jumping or falling: the camera follows the height more slowly
  };
  /**
   * Moves a sphere of the given radius from one point to another, e.g. a sphere
   * cast into the physics world
   * @return How far the sphere gets without touching anything: 0 (not at all)
   * to 1 (all the way)
   */
  using CollisionProbe = std::function<float(vec3 from, vec3 to, float radius)>;

  // Distances from the point the camera orbits around (m)
  static constexpr float DEFAULT_DISTANCE = 7.0f, MIN_DISTANCE = 3.0f,
                         MAX_DISTANCE = 12.0f;
  // Radius of the sphere that keeps the camera (and its near plane) away from
  // the environment
  static constexpr float COLLISION_RADIUS = 0.35f;

  Camera(vec3 position, vec3 direction, vec3 up, GLFWwindow& window);
  ~Camera();

  void draw(Shader* shader) override {};
  /**
   * L locks and unlocks the camera, the unlocked camera flies with WASD
   */
  void update(float time_delta) override;
  vec3 getPosition() override { return position; };
  vec3 getDirection() override { return direction; };

  /**
   * Puts the camera somewhere and looks at the target (e.g. for screenshots of
   * the world)
   */
  void setView(vec3 eye, vec3 targetPosition);
  /**
   * Looks at the target from where the camera is
   */
  void setTarget(vec3 targetPosition);
  /**
   * 3 behind the target and 2.5 above it, looking at it
   */
  void setTarget(vec3 targetPosition, vec3 targetDirection);

  /**
   * Locked camera: moves with the target (call every frame)
   */
  void follow(const FollowTarget& target, float time_delta);
  /**
   * Locked camera: directly behind the target without moving there smoothly
   * (e.g. at the start)
   */
  void snapBehind(const FollowTarget& target);
  /**
   * A shot of a cutscene or a conversation: the camera goes smoothly to the eye
   * and looks at the target (it does not go into the terrain). Following the
   * target again (follow) blends smoothly from the shot back to the camera
   * behind it.
   * @param cut Jumps there at once
   */
  void shot(vec3 eye, vec3 target, float time_delta, bool cut = false);
  /**
   * @return true while the camera shows a shot (until it follows again)
   */
  bool isInShot() const { return inShot; }
  /**
   * Turns the camera (mouse, right stick). Locked it moves around the target,
   * unlocked it turns where it is.
   * @param right Radians to the right
   * @param down Radians down (the locked camera goes up)
   */
  void rotate(float right, float down);
  /**
   * Locked camera: closer to the target (positive) or further away (negative),
   * e.g. with the mouse wheel
   */
  void zoom(float steps);
  /**
   * Locked camera: turns back behind the target quickly
   */
  void recenter();
  /**
   * Keeps the locked camera out of the environment, without a probe it goes
   * through everything
   */
  void setCollisionProbe(CollisionProbe probe) {
    collisionProbe = std::move(probe);
  }

  /**
   * Horizontal direction the camera looks at (e.g. "forward" for moving the
   * bear relative to the camera)
   */
  vec3 getForward() const;
  /**
   * Horizontal direction to the right of the camera
   */
  vec3 getRight() const;
  float getYaw() const { return yaw; }
  float getPitch() const { return pitch; }
  /**
   * Locked camera: the point it orbits around (above the target)
   */
  vec3 getPivot() const { return pivot; }

  mat4 getViewMatrix();
  mat4 getProjectionMatrix();
  /**
   * The volume the camera sees with its view and projection matrix
   */
  ViewFrustum getViewFrustum();
  /**
   * Vertical field of view in degrees (it widens a bit at high speed)
   */
  float getFieldOfView() const { return aperture; }
  float getNearPlane() const { return zNear; }
  float getFarPlane() const { return zFar; }
  float getAspectRatio() const;
  /**
   * Size of the image in pixels, determines the aspect ratio of the projection
   */
  void setViewportSize(int width, int height);

  bool isLocked();

 private:
  /**
   * Points the camera along yaw and pitch
   */
  void applyAngles();
  /**
   * Sets yaw and pitch from a direction
   */
  void setDirection(vec3 newDirection);
  /**
   * Locked camera: places it around the pivot at its distance, closer if
   * something is in the way
   */
  void placeAroundPivot(float time_delta);
  vec3 pivotOf(const FollowTarget& target) const;

  int windowWidth, windowHeight;

  vec3 position, direction, up;
  float yaw = std::numbers::pi_v<float>;
  float pitch = 0.0f;
  float speed = 3.0f;      // unlocked
  float aperture = 60.0f;  // vertical field of view in degrees
  float zNear = 0.15f;
  float zFar = 1600.0f;  // the island and the sea to the horizon

  // Locked (third person)
  vec3 pivot = vec3(0.0f);
  bool hasPivot = false;
  float targetDistance = DEFAULT_DISTANCE;   // zoom
  float distance = DEFAULT_DISTANCE;         // without obstacles
  float currentDistance = DEFAULT_DISTANCE;  // with obstacles
  float timeSinceRotated = 0.0f;
  bool recentering = false;
  CollisionProbe collisionProbe;
  // Shots of cutscenes and conversations
  bool inShot = false;
  float shotBlend = 0.0f;  // 1 right after a shot ended: blends from the shot
                           // to the following camera
  vec3 shotPosition = vec3(0.0f), shotDirection = vec3(0.0f, 0.0f, 1.0f);

  bool locked = true;
  bool lockKeyWasPressed = false;
};
}  // namespace scene
}  // namespace bibo

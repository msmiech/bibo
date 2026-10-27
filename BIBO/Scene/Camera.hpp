#ifndef BIBO_SCENE_CAMERA_HPP_
#define BIBO_SCENE_CAMERA_HPP_
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <numbers>

#include "SceneObject.hpp"
#include "ViewFrustum.hpp"

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
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction =
        glm::vec3(0.0f, 0.0f,
                  1.0f);  // where the target looks (the camera goes behind it)
    glm::vec3 velocity = glm::vec3(0.0f);
    // jumping or falling: the camera follows the height more slowly
    bool airborne = false;
  };
  /**
   * Moves a sphere of the given radius from one point to another, e.g. a sphere
   * cast into the physics world
   * @return How far the sphere gets without touching anything: 0 (not at all)
   * to 1 (all the way)
   */
  using CollisionProbe =
      std::function<float(glm::vec3 from, glm::vec3 to, float radius)>;

  // Distances from the point the camera orbits around (m)
  static constexpr float kDefaultDistance = 7.0f, kMinDistance = 3.0f,
                         kMaxDistance = 12.0f;
  // Radius of the sphere that keeps the camera (and its near plane) away from
  // the environment
  static constexpr float kCollisionRadius = 0.35f;

  Camera(glm::vec3 position, glm::vec3 direction, glm::vec3 up,
         GLFWwindow& window);
  ~Camera();

  void Draw(Shader* shader) override {};
  /**
   * L locks and unlocks the camera, the unlocked camera flies with WASD
   */
  void Update(float time_delta) override;
  glm::vec3 GetPosition() override { return position_; };
  glm::vec3 GetDirection() override { return direction_; };

  /**
   * Puts the camera somewhere and looks at the target (e.g. for screenshots of
   * the world)
   */
  void SetView(glm::vec3 eye, glm::vec3 target_position);
  /**
   * Looks at the target from where the camera is
   */
  void SetTarget(glm::vec3 target_position);
  /**
   * 3 behind the target and 2.5 above it, looking at it
   */
  void SetTarget(glm::vec3 target_position, glm::vec3 target_direction);

  /**
   * Locked camera: moves with the target (call every frame)
   */
  void Follow(const FollowTarget& target, float time_delta);
  /**
   * Locked camera: directly behind the target without moving there smoothly
   * (e.g. at the start)
   */
  void SnapBehind(const FollowTarget& target);
  /**
   * A shot of a cutscene or a conversation: the camera goes smoothly to the eye
   * and looks at the target (it does not go into the terrain). Following the
   * target again (follow) blends smoothly from the shot back to the camera
   * behind it.
   * @param cut Jumps there at once
   */
  void Shot(glm::vec3 eye, glm::vec3 target, float time_delta,
            bool cut = false);
  /**
   * @return true while the camera shows a shot (until it follows again)
   */
  bool IsInShot() const { return in_shot_; }
  /**
   * Turns the camera (mouse, right stick). Locked it moves around the target,
   * unlocked it turns where it is.
   * @param right Radians to the right
   * @param down Radians down (the locked camera goes up)
   */
  void Rotate(float right, float down);
  /**
   * Locked camera: closer to the target (positive) or further away (negative),
   * e.g. with the mouse wheel
   */
  void Zoom(float steps);
  /**
   * Locked camera: turns back behind the target quickly
   */
  void Recenter();
  /**
   * Keeps the locked camera out of the environment, without a probe it goes
   * through everything
   */
  void SetCollisionProbe(CollisionProbe probe) {
    collision_probe_ = std::move(probe);
  }

  /**
   * Horizontal direction the camera looks at (e.g. "forward" for moving the
   * bear relative to the camera)
   */
  glm::vec3 GetForward() const;
  /**
   * Horizontal direction to the right of the camera
   */
  glm::vec3 GetRight() const;
  float GetYaw() const { return yaw_; }
  float GetPitch() const { return pitch_; }
  /**
   * Locked camera: the point it orbits around (above the target)
   */
  glm::vec3 GetPivot() const { return pivot_; }

  glm::mat4 GetViewMatrix();
  glm::mat4 GetProjectionMatrix();
  /**
   * The volume the camera sees with its view and projection matrix
   */
  ViewFrustum GetViewFrustum();
  /**
   * Vertical field of view in degrees (it widens a bit at high speed)
   */
  float GetFieldOfView() const { return aperture_; }
  float GetNearPlane() const { return z_near_; }
  float GetFarPlane() const { return z_far_; }
  float GetAspectRatio() const;
  /**
   * Size of the image in pixels, determines the aspect ratio of the projection
   */
  void SetViewportSize(int width, int height);

  bool IsLocked();

 private:
  /**
   * Points the camera along yaw and pitch
   */
  void ApplyAngles();
  /**
   * Sets yaw and pitch from a direction
   */
  void SetDirection(glm::vec3 new_direction);
  /**
   * Locked camera: places it around the pivot at its distance, closer if
   * something is in the way
   */
  void PlaceAroundPivot(float time_delta);
  glm::vec3 PivotOf(const FollowTarget& target) const;

  int window_width_, window_height_;

  glm::vec3 position_, direction_, up_;
  float yaw_ = std::numbers::pi_v<float>;
  float pitch_ = 0.0f;
  float speed_ = 3.0f;      // unlocked
  float aperture_ = 60.0f;  // vertical field of view in degrees
  float z_near_ = 0.15f;
  float z_far_ = 1600.0f;  // the island and the sea to the horizon

  // Locked (third person)
  glm::vec3 pivot_ = glm::vec3(0.0f);
  bool has_pivot_ = false;
  float target_distance_ = kDefaultDistance;   // zoom
  float distance_ = kDefaultDistance;          // without obstacles
  float current_distance_ = kDefaultDistance;  // with obstacles
  float time_since_rotated_ = 0.0f;
  bool recentering_ = false;
  CollisionProbe collision_probe_;
  // Shots of cutscenes and conversations
  bool in_shot_ = false;
  // 1 right after a shot ended: blends from the shot to the following camera
  float shot_blend_ = 0.0f;
  glm::vec3 shot_position_ = glm::vec3(0.0f),
            shot_direction_ = glm::vec3(0.0f, 0.0f, 1.0f);

  bool locked_ = true;
  bool lock_key_was_pressed_ = false;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_CAMERA_HPP_

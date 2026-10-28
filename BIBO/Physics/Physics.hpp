#ifndef BIBO_PHYSICS_PHYSICS_HPP_
#define BIBO_PHYSICS_PHYSICS_HPP_
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

namespace bibo {
namespace physics {
/**
 * Identifies a rigid body inside a PhysicsWorld.
 * Wraps a Jolt BodyID so that the Jolt headers stay out of the game code.
 */
using BodyHandle = std::uint32_t;
const BodyHandle kInvalidBody = 0xffffffff;

/**
 * Where a ray hits the first body
 */
struct RayHit {
  BodyHandle body = kInvalidBody;
  glm::vec3 position = glm::vec3(0.0f);
  // surface normal of the body at the hit
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
  float fraction = 1.0f;  // position = origin + fraction * ray
};

/**
 * Collision shape of a static body, built before the body is added to the world
 * (e.g. by a worker thread while the world is streamed in). The vertices of the
 * shapes are in world space, the body is placed at the origin.
 */
class CollisionShape {
 public:
  CollisionShape();
  ~CollisionShape();
  CollisionShape(const CollisionShape&) = delete;
  CollisionShape& operator=(const CollisionShape&) = delete;
  bool IsValid() const;

 private:
  friend class PhysicsWorld;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * A capsule or sphere of a compound collision shape (e.g. the trunks of the
 * trees in a part of the world)
 */
struct ShapePart {
  enum Type { kCapsule, kSphere };
  Type type = kSphere;
  glm::vec3 position = glm::vec3(0.0f);  // center
  // the capsule goes along the y axis
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  float half_height = 0.0f;  // capsule: half the length of its cylinder
  float radius = 0.5f;
};

/**
 * Physics simulation based on Jolt Physics
 * (https://github.com/jrouwe/JoltPhysics). Replaces PxFoundation, PxPhysics,
 * PxCooking and PxScene of the former Nvidia PhysX 3.3 implementation.
 */
class PhysicsWorld {
 public:
  /**
   * Triangle mesh for a static body (thread safe, but a PhysicsWorld must
   * exist)
   * @param indices 3 per triangle
   * @return nullptr if the mesh has no triangles
   */
  static std::shared_ptr<CollisionShape> BuildMeshShape(
      const std::vector<glm::vec3>& vertices,
      const std::vector<std::uint32_t>& indices);
  /**
   * Capsules and spheres as one shape for a static body (thread safe, but a
   * PhysicsWorld must exist)
   * @return nullptr if there are no parts
   */
  static std::shared_ptr<CollisionShape> BuildCompoundShape(
      const std::vector<ShapePart>& parts);

  PhysicsWorld();
  ~PhysicsWorld();
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  void Update(float time_delta);

  BodyHandle CreateStaticBox(glm::vec3 position, glm::vec3 half_extent);
  BodyHandle CreateDynamicBox(glm::vec3 position, glm::vec3 half_extent,
                              float density,
                              glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f,
                                                             0.0f));
  BodyHandle CreateStaticSphere(glm::vec3 position, float radius);
  /**
   * A sphere that the game moves (MoveKinematic), e.g. an animal walking
   * around: it pushes dynamic bodies and the character away, but nothing pushes
   * it. Ray and sphere casts with static_only do not find it.
   */
  BodyHandle CreateKinematicSphere(glm::vec3 position, float radius);
  /**
   * The kinematic body reaches the position and rotation at the end of the next
   * update (with the velocity that takes)
   * @param time_delta Of the next update, 0: moves it there right away (e.g.
   * when the game is restarted)
   */
  void MoveKinematic(BodyHandle body, glm::vec3 position, glm::quat rotation,
                     float time_delta);
  /**
   * Creates a static triangle mesh. The vertices are scaled first, then rotated
   * and translated.
   * @param vertices 3 floats per vertex
   * @param indices 3 indices per triangle
   */
  BodyHandle CreateStaticMesh(const float* vertices, unsigned int num_vertices,
                              const unsigned int* indices,
                              unsigned int num_triangles, float scale,
                              glm::vec3 position, glm::quat rotation);
  /**
   * Adds a static body with a shape built before (at the origin, the shape is
   * in world space)
   */
  BodyHandle AddStaticBody(const CollisionShape& shape);
  /**
   * Removes the body from the world and deletes it
   */
  void RemoveBody(BodyHandle body);
  /**
   * @return Number of bodies in the world
   */
  std::size_t GetBodyCount() const;
  /**
   * Should be called once after all static bodies were added (speeds up
   * collision detection)
   */
  void OptimizeBroadPhase();

  /**
   * @return Current position and rotation of the body as model matrix
   */
  glm::mat4 GetTransform(BodyHandle body) const;
  /**
   * Moves a dynamic body to the given position and rotation and stops it (e.g.
   * when the game is restarted)
   */
  void ResetBody(BodyHandle body, glm::vec3 position, glm::quat rotation);
  /**
   * Finds the first body on the way from origin to origin + ray (e.g. the
   * ground under the feet of the bear)
   * @param static_only Only static bodies (terrain, rocks), no dynamic bodies
   * (boxes, ragdolls)
   * @return false if the ray does not hit a body
   */
  bool CastRay(glm::vec3 origin, glm::vec3 ray, RayHit& hit,
               bool static_only = false) const;
  /**
   * Moves a sphere from origin to origin + ray and finds the first body it
   * touches (e.g. so that the camera does not go into the terrain)
   * @param hit position: where the sphere touches the body, fraction: how far
   * the sphere got (0 if it touches a body at the origin)
   * @param static_only Only static bodies (terrain, rocks), no dynamic bodies
   * (boxes, ragdolls)
   * @return false if the sphere does not touch a body on the way
   */
  bool CastSphere(glm::vec3 origin, glm::vec3 ray, float radius, RayHit& hit,
                  bool static_only = false) const;

 private:
  friend class CharacterController;
  friend class Ragdoll;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct CharacterControllerDesc {
  glm::vec3 position = glm::vec3(0.0f);  // center of the capsule
  float radius = 0.5f;                   // radius of the capsule
  // height of the cylinder between the two hemispheres of the capsule
  float height = 1.0f;
  float step_offset = 0.4f;    // max obstacle height the character can climb
  float slope_limit = 0.785f;  // max slope the character can walk (radians)
  // skin around the capsule within which contacts are generated
  float contact_offset = 0.02f;
};

/**
 * Kinematic capsule character that is moved by the game logic and collides with
 * the physics world. Replaces the PxCapsuleController of the former PhysX
 * implementation.
 */
class CharacterController {
 public:
  /**
   * Contact of the character with a body
   */
  struct Hit {
    BodyHandle body = kInvalidBody;
    // where the character touches the body
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(
        0.0f, 1.0f,
        0.0f);  // contact normal, pointing from the body towards the character
  };
  /**
   * Called whenever the character starts touching a body
   */
  using HitCallback = std::function<void(const Hit&)>;

  CharacterController(PhysicsWorld& world, const CharacterControllerDesc& desc,
                      HitCallback on_hit);
  ~CharacterController();
  CharacterController(const CharacterController&) = delete;
  CharacterController& operator=(const CharacterController&) = delete;

  /**
   * Moves the character by the given displacement while colliding with the
   * world (like PxController::move)
   * @return true if the character stands on the ground after the move
   */
  bool Move(glm::vec3 displacement, float time_delta);
  /**
   * @return true if the character stands on ground that is not too steep (see
   * CharacterControllerDesc::slopeLimit), false in the air or on a slope that
   * it slides down
   */
  bool IsOnWalkableGround() const;
  glm::vec3 GetPosition() const;
  /**
   * Moves the character to the given position without colliding with the world
   * on the way (e.g. when the game is restarted)
   */
  void SetPosition(glm::vec3 position);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Joint between a ragdoll part and its parent part, in the space of the bind
 * transforms of the parts
 */
struct RagdollJoint {
  enum Type {
    kSwingTwist,  // ball joint with a cone limit (hips, shoulders, spine, neck)
    kHinge        // rotates only around one axis (knees, elbows)
  };
  Type type = kSwingTwist;
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 twist_axis = glm::vec3(
      1.0f, 0.0f,
      0.0f);  // swing twist: along the child part; hinge: rotation axis
  // perpendicular to the twist axis
  glm::vec3 plane_axis = glm::vec3(0.0f, 1.0f, 0.0f);
  float swing_angle = 0.5f;  // swing twist: half angle of the cone (radians)
  float twist_angle = 0.3f;  // swing twist: twist limit in both directions
  float min_angle = -1.0f, max_angle = 1.0f;  // hinge: limits around the axis
  // torque that slows down the joint when it moves (N m)
  float friction = 1.0f;
};

/**
 * Rigid body of a ragdoll: a capsule that moves like a bone of a skeleton
 */
struct RagdollPart {
  // index of the parent part, -1 for the root part (parents come before their
  // children)
  int parent = -1;
  // transform of the part (its bone) when the joints are relaxed
  glm::mat4 bind_transform = glm::mat4(1.0f);
  glm::vec3 capsule_start = glm::vec3(0.0f),
            capsule_end =
                glm::vec3(0.0f, 1.0f,
                          0.0f);  // in the same space as bind_transform
  float radius = 0.1f;
  float mass = 1.0f;
  RagdollJoint joint;  // to the parent part
};

/**
 * Bodies connected by joints that fall and tumble like a limp body (e.g. the
 * bear after it ran into a rock). The bodies are only part of the physics world
 * while the ragdoll is active.
 */
class Ragdoll {
 public:
  Ragdoll(PhysicsWorld& world, const std::vector<RagdollPart>& parts);
  ~Ragdoll();
  Ragdoll(const Ragdoll&) = delete;
  Ragdoll& operator=(const Ragdoll&) = delete;

  /**
   * Adds the bodies to the physics world
   * @param transforms World transform of every part (rotation and translation)
   * @param velocities Linear velocity of every part
   * @param angular_velocity Spin of the whole ragdoll
   */
  void Activate(const std::vector<glm::mat4>& transforms,
                const std::vector<glm::vec3>& velocities,
                glm::vec3 angular_velocity);
  /**
   * Removes the bodies from the physics world
   */
  void Deactivate();
  bool IsActive() const;
  std::size_t GetPartCount() const;
  /**
   * @param transforms Current world transform of every part
   */
  void GetTransforms(std::vector<glm::mat4>& transforms) const;
  /**
   * @return The highest linear speed of all parts (m/s), for detecting when the
   * ragdoll comes to rest
   */
  float GetMaxLinearSpeed() const;
  /**
   * @return The highest angular speed of all parts (rad/s)
   */
  float GetMaxAngularSpeed() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace physics
}  // namespace bibo

#endif  // BIBO_PHYSICS_PHYSICS_HPP_

#include "Physics.hpp"

// Jolt.h must be included before any other Jolt header (not sorted with them)
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>

namespace bibo::physics {

namespace {
// Object layers: static bodies never need to be tested against other static
// bodies
namespace layers {
const JPH::ObjectLayer kNonMoving = 0;
const JPH::ObjectLayer kMoving = 1;
}  // namespace layers

namespace broad_phase_layers {
const JPH::BroadPhaseLayer kNonMoving(0);
const JPH::BroadPhaseLayer kMoving(1);
const JPH::uint kNumLayers = 2;
}  // namespace broad_phase_layers

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer object1,
                     JPH::ObjectLayer object2) const override {
    return object1 == layers::kMoving || object2 == layers::kMoving;
  }
};

class BroadPhaseLayerInterfaceImpl final
    : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override {
    return broad_phase_layers::kNumLayers;
  }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(
      JPH::ObjectLayer layer) const override {
    return layer == layers::kNonMoving ? broad_phase_layers::kNonMoving
                                       : broad_phase_layers::kMoving;
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(
      JPH::BroadPhaseLayer layer) const override {
    return layer == broad_phase_layers::kNonMoving ? "NON_MOVING" : "MOVING";
  }
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer layer1,
                     JPH::BroadPhaseLayer layer2) const override {
    return layer1 == layers::kMoving || layer2 == broad_phase_layers::kMoving;
  }
};

void TraceImpl(const char* format, ...) {
  char buffer[1024];
  va_list list;
  va_start(list, format);
  vsnprintf(buffer, sizeof(buffer), format, list);
  va_end(list);
  std::cout << "Jolt: " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char* expression, const char* message,
                      const char* file, JPH::uint line) {
  std::cerr << "Jolt assert failed: " << file << ":" << line << ": ("
            << expression << ") " << (message != nullptr ? message : "")
            << std::endl;
  return true;  // break into the debugger
}
#endif

JPH::Vec3 ToJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }

JPH::Quat ToJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

glm::vec3 ToGlm(JPH::Vec3Arg v) {
  return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

glm::quat ToGlm(JPH::QuatArg q) {
  return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

/**
 * Rotation and translation of a matrix without scale (Jolt bodies cannot be
 * scaled)
 */
JPH::Mat44 ToJoltRigid(const glm::mat4& matrix) {
  glm::vec3 x = glm::normalize(glm::vec3(matrix[0]));
  glm::vec3 y = glm::vec3(matrix[1]);
  y = glm::normalize(y - glm::dot(y, x) * x);
  glm::vec3 z = glm::cross(x, y);
  return JPH::Mat44(JPH::Vec4(x.x, x.y, x.z, 0.0f),
                    JPH::Vec4(y.x, y.y, y.z, 0.0f),
                    JPH::Vec4(z.x, z.y, z.z, 0.0f),
                    JPH::Vec4(matrix[3].x, matrix[3].y, matrix[3].z, 1.0f));
}

glm::mat4 ToGlm(const JPH::Mat44& matrix) {
  glm::mat4 result;
  for (int column = 0; column < 4; ++column) {
    JPH::Vec4 c = matrix.GetColumn4(column);
    result[column] = glm::vec4(c.GetX(), c.GetY(), c.GetZ(), c.GetW());
  }
  return result;
}

/**
 * Shortest rotation from one direction to another
 */
glm::quat RotationBetween(glm::vec3 from, glm::vec3 to) {
  from = glm::normalize(from);
  to = glm::normalize(to);
  float cosine = glm::dot(from, to);
  if (cosine < -0.9999f) {
    glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
    if (glm::dot(axis, axis) < 1e-6f) {
      axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), from);
    }
    return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
  }
  glm::vec3 axis = glm::cross(from, to);
  return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}
}  // namespace

struct PhysicsWorld::Impl {
  BroadPhaseLayerInterfaceImpl broad_phase_layer_interface;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter;
  ObjectLayerPairFilterImpl object_layer_pair_filter;
  JPH::TempAllocatorImpl temp_allocator{10 * 1024 * 1024};
  // The PhysX version also used a single worker thread
  // (PxDefaultCpuDispatcherCreate(1))
  JPH::JobSystemThreadPool job_system{JPH::cMaxPhysicsJobs,
                                      JPH::cMaxPhysicsBarriers, 1};
  JPH::PhysicsSystem system;

  BodyHandle AddBody(const JPH::ShapeSettings& shape_settings,
                     glm::vec3 position, glm::quat rotation,
                     JPH::EMotionType motion_type) {
    JPH::ShapeSettings::ShapeResult shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
      std::cerr << "ERROR: Could not create physics shape: "
                << shape_result.GetError().c_str() << std::endl;
      return kInvalidBody;
    }

    bool is_static = motion_type == JPH::EMotionType::Static;
    JPH::BodyCreationSettings body_settings(
        shape_result.Get(), JPH::RVec3(ToJolt(position)), ToJolt(rotation),
        motion_type, is_static ? layers::kNonMoving : layers::kMoving);
    // Same values as the material of the former PhysX implementation
    body_settings.mFriction = 0.5f;
    body_settings.mRestitution = 0.5f;

    JPH::BodyID body = system.GetBodyInterface().CreateAndAddBody(
        body_settings, is_static ? JPH::EActivation::DontActivate
                                 : JPH::EActivation::Activate);
    if (body.IsInvalid()) {
      std::cerr << "ERROR: Could not create physics body (too many bodies?)"
                << std::endl;
      return kInvalidBody;
    }
    return body.GetIndexAndSequenceNumber();
  }
};

namespace {
// The factory and the registered types of Jolt are global: they exist as long
// as any PhysicsWorld does
std::mutex jolt_mutex;
int jolt_users = 0;

void AcquireJolt() {
  std::lock_guard<std::mutex> lock(jolt_mutex);
  if (jolt_users++ == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
}

void ReleaseJolt() {
  std::lock_guard<std::mutex> lock(jolt_mutex);
  if (--jolt_users == 0) {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
}
}  // namespace

PhysicsWorld::PhysicsWorld() {
  AcquireJolt();
  impl_ = std::make_unique<Impl>();

  // The scene has a few dozen bodies, the streamed parts of the world (terrain,
  // trees) a few hundred more
  const JPH::uint max_bodies = 16384;
  const JPH::uint num_body_mutexes = 0;  // 0 = default
  const JPH::uint max_body_pairs = 1024;
  const JPH::uint max_contact_constraints = 1024;
  impl_->system.Init(max_bodies, num_body_mutexes, max_body_pairs,
                     max_contact_constraints,
                     impl_->broad_phase_layer_interface,
                     impl_->object_vs_broad_phase_layer_filter,
                     impl_->object_layer_pair_filter);
  impl_->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
  impl_.reset();
  ReleaseJolt();
}

void PhysicsWorld::Update(float time_delta) {
  if (time_delta <= 0.0f) {
    return;
  }
  // Jolt recommends one collision step per 1/60 s; capped so that long stalls
  // (e.g. dragging the window) don't freeze the game
  int collision_steps =
      std::min(8, std::max(1, static_cast<int>(std::ceil(time_delta * 60.0f))));
  impl_->system.Update(time_delta, collision_steps, &impl_->temp_allocator,
                       &impl_->job_system);
}

BodyHandle PhysicsWorld::CreateStaticBox(glm::vec3 position,
                                         glm::vec3 half_extent) {
  // Jolt requires the convex radius to be smaller than the smallest half extent
  // (e.g. for thin walls)
  float convex_radius =
      std::min(JPH::cDefaultConvexRadius,
               std::min({half_extent.x, half_extent.y, half_extent.z}));
  JPH::BoxShapeSettings box(ToJolt(half_extent), convex_radius);
  box.SetEmbedded();
  return impl_->AddBody(box, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::CreateDynamicBox(glm::vec3 position,
                                          glm::vec3 half_extent, float density,
                                          glm::quat rotation) {
  float convex_radius =
      std::min(JPH::cDefaultConvexRadius,
               std::min({half_extent.x, half_extent.y, half_extent.z}));
  JPH::BoxShapeSettings box(ToJolt(half_extent), convex_radius);
  box.SetDensity(density);
  box.SetEmbedded();
  return impl_->AddBody(box, position, glm::normalize(rotation),
                        JPH::EMotionType::Dynamic);
}

BodyHandle PhysicsWorld::CreateStaticSphere(glm::vec3 position, float radius) {
  JPH::SphereShapeSettings sphere(radius);
  sphere.SetEmbedded();
  return impl_->AddBody(sphere, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::CreateKinematicSphere(glm::vec3 position,
                                               float radius) {
  JPH::SphereShapeSettings sphere(radius);
  sphere.SetEmbedded();
  return impl_->AddBody(sphere, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        JPH::EMotionType::Kinematic);
}

void PhysicsWorld::MoveKinematic(BodyHandle body, glm::vec3 position,
                                 glm::quat rotation, float time_delta) {
  JPH::BodyInterface& body_interface = impl_->system.GetBodyInterface();
  JPH::BodyID body_id(body);
  if (time_delta <= 0.0f) {
    body_interface.SetPositionAndRotation(body_id, JPH::RVec3(ToJolt(position)),
                                          ToJolt(glm::normalize(rotation)),
                                          JPH::EActivation::Activate);
    body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(),
                                               JPH::Vec3::sZero());
    return;
  }
  body_interface.MoveKinematic(body_id, JPH::RVec3(ToJolt(position)),
                               ToJolt(glm::normalize(rotation)), time_delta);
}

BodyHandle PhysicsWorld::CreateStaticMesh(const float* vertices,
                                          unsigned int num_vertices,
                                          const unsigned int* indices,
                                          unsigned int num_triangles,
                                          float scale, glm::vec3 position,
                                          glm::quat rotation) {
  JPH::VertexList vertex_list;
  vertex_list.reserve(num_vertices);
  for (unsigned int i = 0; i < num_vertices; ++i) {
    vertex_list.push_back(JPH::Float3(vertices[i * 3] * scale,
                                      vertices[i * 3 + 1] * scale,
                                      vertices[i * 3 + 2] * scale));
  }

  JPH::IndexedTriangleList triangles;
  triangles.reserve(num_triangles);
  for (unsigned int i = 0; i < num_triangles; ++i) {
    triangles.push_back(JPH::IndexedTriangle(indices[i * 3], indices[i * 3 + 1],
                                             indices[i * 3 + 2], 0));
  }

  // Removes degenerate and duplicate triangles (like the welding options of the
  // former PhysX cooking)
  JPH::MeshShapeSettings mesh(std::move(vertex_list), std::move(triangles));
  mesh.SetEmbedded();
  return impl_->AddBody(mesh, position, rotation, JPH::EMotionType::Static);
}

struct CollisionShape::Impl {
  JPH::ShapeRefC shape;
};

CollisionShape::CollisionShape() : impl_(std::make_unique<Impl>()) {}

CollisionShape::~CollisionShape() = default;

bool CollisionShape::IsValid() const { return impl_->shape != nullptr; }

std::shared_ptr<CollisionShape> PhysicsWorld::BuildMeshShape(
    const std::vector<glm::vec3>& vertices,
    const std::vector<std::uint32_t>& indices) {
  if (indices.size() < 3) {
    return nullptr;
  }
  JPH::VertexList vertex_list;
  vertex_list.reserve(vertices.size());
  for (const glm::vec3& vertex : vertices) {
    vertex_list.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
  }
  JPH::IndexedTriangleList triangles;
  triangles.reserve(indices.size() / 3);
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    triangles.push_back(
        JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2], 0));
  }
  JPH::MeshShapeSettings settings(std::move(vertex_list), std::move(triangles));
  settings.SetEmbedded();
  JPH::ShapeSettings::ShapeResult result = settings.Create();
  if (result.HasError()) {
    return nullptr;  // e.g. only degenerate triangles
  }
  auto shape = std::make_shared<CollisionShape>();
  shape->impl_->shape = result.Get();
  return shape;
}

std::shared_ptr<CollisionShape> PhysicsWorld::BuildCompoundShape(
    const std::vector<ShapePart>& parts) {
  if (parts.empty()) {
    return nullptr;
  }
  JPH::StaticCompoundShapeSettings compound;
  compound.SetEmbedded();
  for (const ShapePart& part : parts) {
    JPH::Ref<JPH::ShapeSettings> settings;
    if (part.type == ShapePart::kCapsule && part.half_height > 0.0f) {
      settings = new JPH::CapsuleShapeSettings(part.half_height, part.radius);
    } else {
      settings = new JPH::SphereShapeSettings(part.radius);
    }
    compound.AddShape(ToJolt(part.position), ToJolt(part.rotation), settings);
  }
  JPH::ShapeSettings::ShapeResult result = compound.Create();
  if (result.HasError()) {
    return nullptr;
  }
  auto shape = std::make_shared<CollisionShape>();
  shape->impl_->shape = result.Get();
  return shape;
}

BodyHandle PhysicsWorld::AddStaticBody(const CollisionShape& shape) {
  if (!shape.IsValid()) {
    return kInvalidBody;
  }
  JPH::BodyCreationSettings body_settings(
      shape.impl_->shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, layers::kNonMoving);
  body_settings.mFriction = 0.5f;
  body_settings.mRestitution = 0.5f;
  JPH::BodyID body = impl_->system.GetBodyInterface().CreateAndAddBody(
      body_settings, JPH::EActivation::DontActivate);
  if (body.IsInvalid()) {
    std::cerr << "ERROR: Could not create physics body (too many bodies?)"
              << std::endl;
    return kInvalidBody;
  }
  return body.GetIndexAndSequenceNumber();
}

void PhysicsWorld::RemoveBody(BodyHandle body) {
  if (body == kInvalidBody) {
    return;
  }
  JPH::BodyInterface& body_interface = impl_->system.GetBodyInterface();
  JPH::BodyID body_id(body);
  body_interface.RemoveBody(body_id);
  body_interface.DestroyBody(body_id);
}

std::size_t PhysicsWorld::GetBodyCount() const {
  return impl_->system.GetNumBodies();
}

void PhysicsWorld::OptimizeBroadPhase() { impl_->system.OptimizeBroadPhase(); }

glm::mat4 PhysicsWorld::GetTransform(BodyHandle body) const {
  JPH::RVec3 position;
  JPH::Quat rotation;
  impl_->system.GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body),
                                                          position, rotation);

  glm::mat4 transform = glm::mat4_cast(ToGlm(rotation));
  transform[3] = glm::vec4(ToGlm(JPH::Vec3(position)), 1.0f);
  return transform;
}

void PhysicsWorld::ResetBody(BodyHandle body, glm::vec3 position,
                             glm::quat rotation) {
  JPH::BodyInterface& body_interface = impl_->system.GetBodyInterface();
  JPH::BodyID body_id(body);
  body_interface.SetPositionAndRotation(body_id, JPH::RVec3(ToJolt(position)),
                                        ToJolt(rotation),
                                        JPH::EActivation::Activate);
  body_interface.SetLinearAndAngularVelocity(body_id, JPH::Vec3::sZero(),
                                             JPH::Vec3::sZero());
}

bool PhysicsWorld::CastRay(glm::vec3 origin, glm::vec3 ray, RayHit& hit,
                           bool static_only) const {
  JPH::RRayCast cast(JPH::RVec3(ToJolt(origin)), ToJolt(ray));
  JPH::RayCastResult result;
  const JPH::NarrowPhaseQuery& query = impl_->system.GetNarrowPhaseQuery();
  bool found =
      static_only
          ? query.CastRay(cast, result,
                          JPH::SpecifiedBroadPhaseLayerFilter(
                              broad_phase_layers::kNonMoving),
                          JPH::SpecifiedObjectLayerFilter(layers::kNonMoving))
          : query.CastRay(cast, result);
  if (!found) {
    return false;
  }
  JPH::RVec3 point = cast.GetPointOnRay(result.mFraction);
  hit.body = result.mBodyID.GetIndexAndSequenceNumber();
  hit.fraction = result.mFraction;
  hit.position = ToGlm(JPH::Vec3(point));
  JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
  if (lock.Succeeded()) {
    hit.normal = ToGlm(
        lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
  }
  return true;
}

bool PhysicsWorld::CastSphere(glm::vec3 origin, glm::vec3 ray, float radius,
                              RayHit& hit, bool static_only) const {
  JPH::SphereShape sphere(radius);
  sphere.SetEmbedded();  // on the stack, not reference counted
  // The contact points of the result are relative to the base offset (the
  // origin)
  JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
      &sphere, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(ToJolt(origin))), ToJolt(ray));
  JPH::ShapeCastSettings settings;
  JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
  const JPH::NarrowPhaseQuery& query = impl_->system.GetNarrowPhaseQuery();
  JPH::RVec3 base(ToJolt(origin));
  if (static_only) {
    query.CastShape(
        cast, settings, base, collector,
        JPH::SpecifiedBroadPhaseLayerFilter(broad_phase_layers::kNonMoving),
        JPH::SpecifiedObjectLayerFilter(layers::kNonMoving));
  } else {
    query.CastShape(cast, settings, base, collector);
  }
  if (!collector.HadHit()) {
    return false;
  }
  const JPH::ShapeCastResult& result = collector.mHit;
  hit.body = result.mBodyID2.GetIndexAndSequenceNumber();
  hit.fraction = std::max(result.mFraction, 0.0f);
  hit.position = origin + ToGlm(result.mContactPointOn2);
  // The penetration axis points from the sphere into the body
  JPH::Vec3 axis = result.mPenetrationAxis;
  hit.normal = axis.LengthSq() > 1e-12f ? -ToGlm(axis.Normalized())
                                        : -glm::normalize(ray);
  return true;
}

struct CharacterController::Impl : public JPH::CharacterContactListener {
  Impl(PhysicsWorld::Impl& world, HitCallback on_hit, float step_offset)
      : world(world), on_hit(std::move(on_hit)), step_offset(step_offset) {}

  void OnContactAdded(const JPH::CharacterVirtual*,
                      const JPH::CharacterContact& contact,
                      JPH::CharacterContactSettings&) override {
    if (on_hit && !contact.mBodyB.IsInvalid()) {
      Hit hit;
      hit.body = contact.mBodyB.GetIndexAndSequenceNumber();
      hit.position = ToGlm(JPH::Vec3(contact.mPosition));
      hit.normal = ToGlm(contact.mContactNormal);
      on_hit(hit);
    }
  }

  PhysicsWorld::Impl& world;
  HitCallback on_hit;
  float step_offset;
  JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterController::CharacterController(PhysicsWorld& world,
                                         const CharacterControllerDesc& desc,
                                         HitCallback on_hit)
    : impl_(std::make_unique<Impl>(*world.impl_, std::move(on_hit),
                                   desc.step_offset)) {
  JPH::Ref<JPH::CharacterVirtualSettings> settings =
      new JPH::CharacterVirtualSettings();
  settings->mShape = new JPH::CapsuleShape(0.5f * desc.height, desc.radius);
  settings->mUp = JPH::Vec3::sAxisY();
  settings->mMaxSlopeAngle = desc.slope_limit;
  // The position is the center of the capsule (as with the PhysX controller):
  // contacts below the cylinder part count as ground
  settings->mSupportingVolume =
      JPH::Plane(JPH::Vec3::sAxisY(), 0.5f * desc.height);
  settings->mCharacterPadding = desc.contact_offset;
  // Smoother movement over the triangle mesh of the terrain
  settings->mEnhancedInternalEdgeRemoval = true;

  impl_->character = new JPH::CharacterVirtual(
      settings, JPH::RVec3(ToJolt(desc.position)), JPH::Quat::sIdentity(), 0,
      &impl_->world.system);
  impl_->character->SetListener(impl_.get());
}

CharacterController::~CharacterController() = default;

bool CharacterController::Move(glm::vec3 displacement, float time_delta) {
  if (time_delta <= 0.0f) {
    return impl_->character->IsSupported();
  }

  JPH::PhysicsSystem& system = impl_->world.system;
  JPH::Vec3 velocity = ToJolt(displacement / time_delta);
  // Like the PhysX controller: pushing down onto walkable ground (e.g. gravity)
  // does not make the character slide down the slope. On slopes that are too
  // steep the character still slides.
  if (impl_->character->GetGroundState() ==
          JPH::CharacterVirtual::EGroundState::OnGround &&
      velocity.GetY() < 0.0f) {
    velocity.SetY(0.0f);
  }
  impl_->character->SetLinearVelocity(velocity);

  JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
  update_settings.mWalkStairsStepUp = JPH::Vec3(0.0f, impl_->step_offset, 0.0f);
  impl_->character->ExtendedUpdate(
      time_delta, system.GetGravity(), update_settings,
      system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
      system.GetDefaultLayerFilter(layers::kMoving), {}, {},
      impl_->world.temp_allocator);

  return impl_->character->IsSupported();
}

bool CharacterController::IsOnWalkableGround() const {
  return impl_->character->GetGroundState() ==
         JPH::CharacterVirtual::EGroundState::OnGround;
}

glm::vec3 CharacterController::GetPosition() const {
  return ToGlm(JPH::Vec3(impl_->character->GetPosition()));
}

void CharacterController::SetPosition(glm::vec3 position) {
  impl_->character->SetPosition(JPH::RVec3(ToJolt(position)));
  impl_->character->SetLinearVelocity(JPH::Vec3::sZero());
  // Updates the ground state (e.g. on the ground after a fall) for the next
  // move
  JPH::PhysicsSystem& system = impl_->world.system;
  impl_->character->RefreshContacts(
      system.GetDefaultBroadPhaseLayerFilter(layers::kMoving),
      system.GetDefaultLayerFilter(layers::kMoving), {}, {},
      impl_->world.temp_allocator);
}

struct Ragdoll::Impl {
  explicit Impl(PhysicsWorld::Impl& world) : world(world) {}

  PhysicsWorld::Impl& world;
  JPH::Ref<JPH::RagdollSettings> settings;
  JPH::Ref<JPH::Ragdoll> ragdoll;
  bool active = false;
};

Ragdoll::Ragdoll(PhysicsWorld& world, const std::vector<RagdollPart>& parts)
    : impl_(std::make_unique<Impl>(*world.impl_)) {
  JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
  settings->mSkeleton = new JPH::Skeleton();
  settings->mParts.resize(parts.size());
  std::vector<JPH::Mat44> bind_matrices;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const RagdollPart& part = parts[i];
    settings->mSkeleton->AddJoint("part" + std::to_string(i), part.parent);

    // The body has the transform of its bone, so the capsule is placed in the
    // space of the bone
    JPH::Mat44 bind = ToJoltRigid(part.bind_transform);
    bind_matrices.push_back(bind);
    JPH::Mat44 to_part = bind.InversedRotationTranslation();
    JPH::Vec3 start = to_part * ToJolt(part.capsule_start);
    JPH::Vec3 end = to_part * ToJolt(part.capsule_end);
    float half_height = 0.5f * (end - start).Length();
    JPH::Ref<JPH::Shape> core =
        half_height > 1e-3f
            ? static_cast<JPH::Shape*>(
                  new JPH::CapsuleShape(half_height, part.radius))
            : static_cast<JPH::Shape*>(new JPH::SphereShape(part.radius));
    glm::quat along_capsule =
        half_height > 1e-3f
            ? RotationBetween(glm::vec3(0.0f, 1.0f, 0.0f), ToGlm(end - start))
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    JPH::RotatedTranslatedShapeSettings shape(0.5f * (start + end),
                                              ToJolt(along_capsule), core);
    JPH::ShapeSettings::ShapeResult shape_result = shape.Create();
    if (shape_result.HasError()) {
      std::cerr << "ERROR: Could not create ragdoll shape: "
                << shape_result.GetError().c_str() << std::endl;
      return;
    }

    JPH::RagdollSettings::Part& body = settings->mParts[i];
    body.SetShape(shape_result.Get());
    body.mPosition = JPH::RVec3(bind.GetTranslation());
    body.mRotation = bind.GetQuaternion();
    body.mMotionType = JPH::EMotionType::Dynamic;
    body.mObjectLayer = layers::kMoving;
    // does not tunnel through thin walls when it is hit hard
    body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    body.mOverrideMassProperties =
        JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = part.mass;
    body.mFriction = 0.8f;
    body.mRestitution = 0.15f;
    // A limp body tumbles and then settles quickly instead of rolling forever
    body.mLinearDamping = 0.2f;
    body.mAngularDamping = 0.9f;

    if (part.parent >= 0) {
      const RagdollJoint& joint = part.joint;
      glm::vec3 twist = glm::normalize(joint.twist_axis);
      glm::vec3 plane = glm::normalize(
          joint.plane_axis - glm::dot(joint.plane_axis, twist) * twist);
      if (joint.type == RagdollJoint::kHinge) {
        JPH::Ref<JPH::HingeConstraintSettings> hinge =
            new JPH::HingeConstraintSettings();
        hinge->mPoint1 = hinge->mPoint2 = JPH::RVec3(ToJolt(joint.position));
        hinge->mHingeAxis1 = hinge->mHingeAxis2 = ToJolt(twist);
        hinge->mNormalAxis1 = hinge->mNormalAxis2 = ToJolt(plane);
        hinge->mLimitsMin = joint.min_angle;
        hinge->mLimitsMax = joint.max_angle;
        hinge->mMaxFrictionTorque = joint.friction;
        body.mToParent = hinge;
      } else {
        JPH::Ref<JPH::SwingTwistConstraintSettings> swing_twist =
            new JPH::SwingTwistConstraintSettings();
        swing_twist->mPosition1 = swing_twist->mPosition2 =
            JPH::RVec3(ToJolt(joint.position));
        swing_twist->mTwistAxis1 = swing_twist->mTwistAxis2 = ToJolt(twist);
        swing_twist->mPlaneAxis1 = swing_twist->mPlaneAxis2 = ToJolt(plane);
        swing_twist->mNormalHalfConeAngle = joint.swing_angle;
        swing_twist->mPlaneHalfConeAngle = joint.swing_angle;
        swing_twist->mTwistMinAngle = -joint.twist_angle;
        swing_twist->mTwistMaxAngle = joint.twist_angle;
        swing_twist->mMaxFrictionTorque = joint.friction;
        body.mToParent = swing_twist;
      }
    }
  }

  settings->Stabilize();
  // Neighbouring parts overlap at the joints, they must not push each other
  // apart
  settings->DisableParentChildCollisions(bind_matrices.data(), 0.02f);
  settings->CalculateBodyIndexToConstraintIndex();
  settings->CalculateConstraintIndexToBodyIdxPair();
  impl_->settings = settings;
  impl_->ragdoll = settings->CreateRagdoll(0, 0, &impl_->world.system);
  if (impl_->ragdoll == nullptr) {
    std::cerr << "ERROR: Could not create the ragdoll (too many bodies?)"
              << std::endl;
  }
}

Ragdoll::~Ragdoll() {
  Deactivate();  // Jolt only destroys bodies that are not in the physics system
}

void Ragdoll::Activate(const std::vector<glm::mat4>& transforms,
                       const std::vector<glm::vec3>& velocities,
                       glm::vec3 angular_velocity) {
  if (!impl_->ragdoll || transforms.size() != GetPartCount()) {
    return;
  }
  if (!impl_->active) {
    impl_->ragdoll->AddToPhysicsSystem(JPH::EActivation::DontActivate);
    impl_->active = true;
  }
  std::vector<JPH::Mat44> matrices;
  for (const glm::mat4& transform : transforms) {
    matrices.push_back(ToJoltRigid(transform));
  }
  impl_->ragdoll->SetPose(JPH::RVec3::sZero(), matrices.data());
  impl_->ragdoll->ResetWarmStart();
  JPH::BodyInterface& body_interface = impl_->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < GetPartCount(); ++i) {
    glm::vec3 velocity =
        i < velocities.size() ? velocities[i] : glm::vec3(0.0f);
    body_interface.SetLinearAndAngularVelocity(
        impl_->ragdoll->GetBodyID(static_cast<int>(i)), ToJolt(velocity),
        ToJolt(angular_velocity));
  }
  impl_->ragdoll->Activate();
}

void Ragdoll::Deactivate() {
  if (impl_->ragdoll && impl_->active) {
    impl_->ragdoll->RemoveFromPhysicsSystem();
  }
  impl_->active = false;
}

bool Ragdoll::IsActive() const { return impl_->active; }

std::size_t Ragdoll::GetPartCount() const {
  return impl_->ragdoll ? impl_->ragdoll->GetBodyCount() : 0;
}

void Ragdoll::GetTransforms(std::vector<glm::mat4>& transforms) const {
  transforms.resize(GetPartCount());
  const JPH::BodyInterface& body_interface =
      impl_->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < transforms.size(); ++i) {
    transforms[i] = ToGlm(
        body_interface
            .GetWorldTransform(impl_->ragdoll->GetBodyID(static_cast<int>(i)))
            .ToMat44());
  }
}

float Ragdoll::GetMaxLinearSpeed() const {
  float speed = 0.0f;
  const JPH::BodyInterface& body_interface =
      impl_->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < GetPartCount(); ++i) {
    speed = std::max(
        speed,
        body_interface
            .GetLinearVelocity(impl_->ragdoll->GetBodyID(static_cast<int>(i)))
            .Length());
  }
  return speed;
}

float Ragdoll::GetMaxAngularSpeed() const {
  float speed = 0.0f;
  const JPH::BodyInterface& body_interface =
      impl_->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < GetPartCount(); ++i) {
    speed = std::max(
        speed,
        body_interface
            .GetAngularVelocity(impl_->ragdoll->GetBodyID(static_cast<int>(i)))
            .Length());
  }
  return speed;
}

}  // namespace bibo::physics

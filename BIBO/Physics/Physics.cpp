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

using namespace bibo::physics;

namespace {
// Object layers: static bodies never need to be tested against other static
// bodies
namespace Layers {
const JPH::ObjectLayer NON_MOVING = 0;
const JPH::ObjectLayer MOVING = 1;
}  // namespace Layers

namespace BroadPhaseLayers {
const JPH::BroadPhaseLayer NON_MOVING(0);
const JPH::BroadPhaseLayer MOVING(1);
const JPH::uint NUM_LAYERS = 2;
}  // namespace BroadPhaseLayers

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer object1,
                     JPH::ObjectLayer object2) const override {
    return object1 == Layers::MOVING || object2 == Layers::MOVING;
  }
};

class BroadPhaseLayerInterfaceImpl final
    : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(
      JPH::ObjectLayer layer) const override {
    return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING
                                       : BroadPhaseLayers::MOVING;
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(
      JPH::BroadPhaseLayer layer) const override {
    return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
  }
#endif
};

class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer layer1,
                     JPH::BroadPhaseLayer layer2) const override {
    return layer1 == Layers::MOVING || layer2 == BroadPhaseLayers::MOVING;
  }
};

void traceImpl(const char* format, ...) {
  char buffer[1024];
  va_list list;
  va_start(list, format);
  vsnprintf(buffer, sizeof(buffer), format, list);
  va_end(list);
  std::cout << "Jolt: " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char* expression, const char* message,
                      const char* file, JPH::uint line) {
  std::cerr << "Jolt assert failed: " << file << ":" << line << ": ("
            << expression << ") " << (message != nullptr ? message : "")
            << std::endl;
  return true;  // break into the debugger
}
#endif

JPH::Vec3 toJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }

JPH::Quat toJolt(glm::quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

glm::vec3 toGlm(JPH::Vec3Arg v) {
  return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

glm::quat toGlm(JPH::QuatArg q) {
  return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

/**
 * Rotation and translation of a matrix without scale (Jolt bodies cannot be
 * scaled)
 */
JPH::Mat44 toJoltRigid(const glm::mat4& matrix) {
  glm::vec3 x = glm::normalize(glm::vec3(matrix[0]));
  glm::vec3 y = glm::vec3(matrix[1]);
  y = glm::normalize(y - glm::dot(y, x) * x);
  glm::vec3 z = glm::cross(x, y);
  return JPH::Mat44(JPH::Vec4(x.x, x.y, x.z, 0.0f),
                    JPH::Vec4(y.x, y.y, y.z, 0.0f),
                    JPH::Vec4(z.x, z.y, z.z, 0.0f),
                    JPH::Vec4(matrix[3].x, matrix[3].y, matrix[3].z, 1.0f));
}

glm::mat4 toGlm(const JPH::Mat44& matrix) {
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
glm::quat rotationBetween(glm::vec3 from, glm::vec3 to) {
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
  BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
  ObjectLayerPairFilterImpl objectLayerPairFilter;
  JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024};
  // The PhysX version also used a single worker thread
  // (PxDefaultCpuDispatcherCreate(1))
  JPH::JobSystemThreadPool jobSystem{JPH::cMaxPhysicsJobs,
                                     JPH::cMaxPhysicsBarriers, 1};
  JPH::PhysicsSystem system;

  BodyHandle addBody(const JPH::ShapeSettings& shapeSettings,
                     glm::vec3 position, glm::quat rotation,
                     JPH::EMotionType motionType) {
    JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
      std::cerr << "ERROR: Could not create physics shape: "
                << shapeResult.GetError().c_str() << std::endl;
      return INVALID_BODY;
    }

    bool isStatic = motionType == JPH::EMotionType::Static;
    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(), JPH::RVec3(toJolt(position)), toJolt(rotation),
        motionType, isStatic ? Layers::NON_MOVING : Layers::MOVING);
    // Same values as the material of the former PhysX implementation
    bodySettings.mFriction = 0.5f;
    bodySettings.mRestitution = 0.5f;

    JPH::BodyID body = system.GetBodyInterface().CreateAndAddBody(
        bodySettings,
        isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (body.IsInvalid()) {
      std::cerr << "ERROR: Could not create physics body (too many bodies?)"
                << std::endl;
      return INVALID_BODY;
    }
    return body.GetIndexAndSequenceNumber();
  }
};

namespace {
// The factory and the registered types of Jolt are global: they exist as long
// as any PhysicsWorld does
std::mutex joltMutex;
int joltUsers = 0;

void acquireJolt() {
  std::lock_guard<std::mutex> lock(joltMutex);
  if (joltUsers++ == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = traceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailedImpl;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
}

void releaseJolt() {
  std::lock_guard<std::mutex> lock(joltMutex);
  if (--joltUsers == 0) {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
}
}  // namespace

PhysicsWorld::PhysicsWorld() {
  acquireJolt();
  impl = std::make_unique<Impl>();

  // The scene has a few dozen bodies, the streamed parts of the world (terrain,
  // trees) a few hundred more
  const JPH::uint maxBodies = 16384;
  const JPH::uint numBodyMutexes = 0;  // 0 = default
  const JPH::uint maxBodyPairs = 1024;
  const JPH::uint maxContactConstraints = 1024;
  impl->system.Init(maxBodies, numBodyMutexes, maxBodyPairs,
                    maxContactConstraints, impl->broadPhaseLayerInterface,
                    impl->objectVsBroadPhaseLayerFilter,
                    impl->objectLayerPairFilter);
  impl->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
  impl.reset();
  releaseJolt();
}

void PhysicsWorld::update(float time_delta) {
  if (time_delta <= 0.0f) {
    return;
  }
  // Jolt recommends one collision step per 1/60 s; capped so that long stalls
  // (e.g. dragging the window) don't freeze the game
  int collisionSteps =
      std::min(8, std::max(1, static_cast<int>(std::ceil(time_delta * 60.0f))));
  impl->system.Update(time_delta, collisionSteps, &impl->tempAllocator,
                      &impl->jobSystem);
}

BodyHandle PhysicsWorld::createStaticBox(glm::vec3 position,
                                         glm::vec3 halfExtent) {
  // Jolt requires the convex radius to be smaller than the smallest half extent
  // (e.g. for thin walls)
  float convexRadius =
      std::min(JPH::cDefaultConvexRadius,
               std::min({halfExtent.x, halfExtent.y, halfExtent.z}));
  JPH::BoxShapeSettings box(toJolt(halfExtent), convexRadius);
  box.SetEmbedded();
  return impl->addBody(box, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                       JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::createDynamicBox(glm::vec3 position,
                                          glm::vec3 halfExtent, float density,
                                          glm::quat rotation) {
  float convexRadius =
      std::min(JPH::cDefaultConvexRadius,
               std::min({halfExtent.x, halfExtent.y, halfExtent.z}));
  JPH::BoxShapeSettings box(toJolt(halfExtent), convexRadius);
  box.SetDensity(density);
  box.SetEmbedded();
  return impl->addBody(box, position, glm::normalize(rotation),
                       JPH::EMotionType::Dynamic);
}

BodyHandle PhysicsWorld::createStaticSphere(glm::vec3 position, float radius) {
  JPH::SphereShapeSettings sphere(radius);
  sphere.SetEmbedded();
  return impl->addBody(sphere, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                       JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::createKinematicSphere(glm::vec3 position,
                                               float radius) {
  JPH::SphereShapeSettings sphere(radius);
  sphere.SetEmbedded();
  return impl->addBody(sphere, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                       JPH::EMotionType::Kinematic);
}

void PhysicsWorld::moveKinematic(BodyHandle body, glm::vec3 position,
                                 glm::quat rotation, float time_delta) {
  JPH::BodyInterface& bodyInterface = impl->system.GetBodyInterface();
  JPH::BodyID bodyID(body);
  if (time_delta <= 0.0f) {
    bodyInterface.SetPositionAndRotation(bodyID, JPH::RVec3(toJolt(position)),
                                         toJolt(glm::normalize(rotation)),
                                         JPH::EActivation::Activate);
    bodyInterface.SetLinearAndAngularVelocity(bodyID, JPH::Vec3::sZero(),
                                              JPH::Vec3::sZero());
    return;
  }
  bodyInterface.MoveKinematic(bodyID, JPH::RVec3(toJolt(position)),
                              toJolt(glm::normalize(rotation)), time_delta);
}

BodyHandle PhysicsWorld::createStaticMesh(const float* vertices,
                                          unsigned int numVertices,
                                          const unsigned int* indices,
                                          unsigned int numTriangles,
                                          float scale, glm::vec3 position,
                                          glm::quat rotation) {
  JPH::VertexList vertexList;
  vertexList.reserve(numVertices);
  for (unsigned int i = 0; i < numVertices; ++i) {
    vertexList.push_back(JPH::Float3(vertices[i * 3] * scale,
                                     vertices[i * 3 + 1] * scale,
                                     vertices[i * 3 + 2] * scale));
  }

  JPH::IndexedTriangleList triangles;
  triangles.reserve(numTriangles);
  for (unsigned int i = 0; i < numTriangles; ++i) {
    triangles.push_back(JPH::IndexedTriangle(indices[i * 3], indices[i * 3 + 1],
                                             indices[i * 3 + 2], 0));
  }

  // Removes degenerate and duplicate triangles (like the welding options of the
  // former PhysX cooking)
  JPH::MeshShapeSettings mesh(std::move(vertexList), std::move(triangles));
  mesh.SetEmbedded();
  return impl->addBody(mesh, position, rotation, JPH::EMotionType::Static);
}

struct CollisionShape::Impl {
  JPH::ShapeRefC shape;
};

CollisionShape::CollisionShape() : impl(std::make_unique<Impl>()) {}

CollisionShape::~CollisionShape() = default;

bool CollisionShape::isValid() const { return impl->shape != nullptr; }

std::shared_ptr<CollisionShape> PhysicsWorld::buildMeshShape(
    const std::vector<glm::vec3>& vertices,
    const std::vector<std::uint32_t>& indices) {
  if (indices.size() < 3) {
    return nullptr;
  }
  JPH::VertexList vertexList;
  vertexList.reserve(vertices.size());
  for (const glm::vec3& vertex : vertices) {
    vertexList.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
  }
  JPH::IndexedTriangleList triangles;
  triangles.reserve(indices.size() / 3);
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    triangles.push_back(
        JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2], 0));
  }
  JPH::MeshShapeSettings settings(std::move(vertexList), std::move(triangles));
  settings.SetEmbedded();
  JPH::ShapeSettings::ShapeResult result = settings.Create();
  if (result.HasError()) {
    return nullptr;  // e.g. only degenerate triangles
  }
  auto shape = std::make_shared<CollisionShape>();
  shape->impl->shape = result.Get();
  return shape;
}

std::shared_ptr<CollisionShape> PhysicsWorld::buildCompoundShape(
    const std::vector<ShapePart>& parts) {
  if (parts.empty()) {
    return nullptr;
  }
  JPH::StaticCompoundShapeSettings compound;
  compound.SetEmbedded();
  for (const ShapePart& part : parts) {
    JPH::Ref<JPH::ShapeSettings> settings;
    if (part.type == ShapePart::CAPSULE && part.halfHeight > 0.0f) {
      settings = new JPH::CapsuleShapeSettings(part.halfHeight, part.radius);
    } else {
      settings = new JPH::SphereShapeSettings(part.radius);
    }
    compound.AddShape(toJolt(part.position), toJolt(part.rotation), settings);
  }
  JPH::ShapeSettings::ShapeResult result = compound.Create();
  if (result.HasError()) {
    return nullptr;
  }
  auto shape = std::make_shared<CollisionShape>();
  shape->impl->shape = result.Get();
  return shape;
}

BodyHandle PhysicsWorld::addStaticBody(const CollisionShape& shape) {
  if (!shape.isValid()) {
    return INVALID_BODY;
  }
  JPH::BodyCreationSettings bodySettings(
      shape.impl->shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
      JPH::EMotionType::Static, Layers::NON_MOVING);
  bodySettings.mFriction = 0.5f;
  bodySettings.mRestitution = 0.5f;
  JPH::BodyID body = impl->system.GetBodyInterface().CreateAndAddBody(
      bodySettings, JPH::EActivation::DontActivate);
  if (body.IsInvalid()) {
    std::cerr << "ERROR: Could not create physics body (too many bodies?)"
              << std::endl;
    return INVALID_BODY;
  }
  return body.GetIndexAndSequenceNumber();
}

void PhysicsWorld::removeBody(BodyHandle body) {
  if (body == INVALID_BODY) {
    return;
  }
  JPH::BodyInterface& bodyInterface = impl->system.GetBodyInterface();
  JPH::BodyID bodyID(body);
  bodyInterface.RemoveBody(bodyID);
  bodyInterface.DestroyBody(bodyID);
}

std::size_t PhysicsWorld::getBodyCount() const {
  return impl->system.GetNumBodies();
}

void PhysicsWorld::optimizeBroadPhase() { impl->system.OptimizeBroadPhase(); }

glm::mat4 PhysicsWorld::getTransform(BodyHandle body) const {
  JPH::RVec3 position;
  JPH::Quat rotation;
  impl->system.GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body),
                                                         position, rotation);

  glm::mat4 transform = glm::mat4_cast(toGlm(rotation));
  transform[3] = glm::vec4(toGlm(JPH::Vec3(position)), 1.0f);
  return transform;
}

void PhysicsWorld::resetBody(BodyHandle body, glm::vec3 position,
                             glm::quat rotation) {
  JPH::BodyInterface& bodyInterface = impl->system.GetBodyInterface();
  JPH::BodyID bodyID(body);
  bodyInterface.SetPositionAndRotation(bodyID, JPH::RVec3(toJolt(position)),
                                       toJolt(rotation),
                                       JPH::EActivation::Activate);
  bodyInterface.SetLinearAndAngularVelocity(bodyID, JPH::Vec3::sZero(),
                                            JPH::Vec3::sZero());
}

bool PhysicsWorld::castRay(glm::vec3 origin, glm::vec3 ray, RayHit& hit,
                           bool staticOnly) const {
  JPH::RRayCast cast(JPH::RVec3(toJolt(origin)), toJolt(ray));
  JPH::RayCastResult result;
  const JPH::NarrowPhaseQuery& query = impl->system.GetNarrowPhaseQuery();
  bool found =
      staticOnly
          ? query.CastRay(cast, result,
                          JPH::SpecifiedBroadPhaseLayerFilter(
                              BroadPhaseLayers::NON_MOVING),
                          JPH::SpecifiedObjectLayerFilter(Layers::NON_MOVING))
          : query.CastRay(cast, result);
  if (!found) {
    return false;
  }
  JPH::RVec3 point = cast.GetPointOnRay(result.mFraction);
  hit.body = result.mBodyID.GetIndexAndSequenceNumber();
  hit.fraction = result.mFraction;
  hit.position = toGlm(JPH::Vec3(point));
  JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(), result.mBodyID);
  if (lock.Succeeded()) {
    hit.normal = toGlm(
        lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
  }
  return true;
}

bool PhysicsWorld::castSphere(glm::vec3 origin, glm::vec3 ray, float radius,
                              RayHit& hit, bool staticOnly) const {
  JPH::SphereShape sphere(radius);
  sphere.SetEmbedded();  // on the stack, not reference counted
  // The contact points of the result are relative to the base offset (the
  // origin)
  JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
      &sphere, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(toJolt(origin))), toJolt(ray));
  JPH::ShapeCastSettings settings;
  JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
  const JPH::NarrowPhaseQuery& query = impl->system.GetNarrowPhaseQuery();
  JPH::RVec3 base(toJolt(origin));
  if (staticOnly) {
    query.CastShape(
        cast, settings, base, collector,
        JPH::SpecifiedBroadPhaseLayerFilter(BroadPhaseLayers::NON_MOVING),
        JPH::SpecifiedObjectLayerFilter(Layers::NON_MOVING));
  } else {
    query.CastShape(cast, settings, base, collector);
  }
  if (!collector.HadHit()) {
    return false;
  }
  const JPH::ShapeCastResult& result = collector.mHit;
  hit.body = result.mBodyID2.GetIndexAndSequenceNumber();
  hit.fraction = std::max(result.mFraction, 0.0f);
  hit.position = origin + toGlm(result.mContactPointOn2);
  // The penetration axis points from the sphere into the body
  JPH::Vec3 axis = result.mPenetrationAxis;
  hit.normal = axis.LengthSq() > 1e-12f ? -toGlm(axis.Normalized())
                                        : -glm::normalize(ray);
  return true;
}

struct CharacterController::Impl : public JPH::CharacterContactListener {
  Impl(PhysicsWorld::Impl& world, HitCallback onHit, float stepOffset)
      : world(world), onHit(std::move(onHit)), stepOffset(stepOffset) {}

  void OnContactAdded(const JPH::CharacterVirtual*,
                      const JPH::CharacterContact& contact,
                      JPH::CharacterContactSettings&) override {
    if (onHit && !contact.mBodyB.IsInvalid()) {
      Hit hit;
      hit.body = contact.mBodyB.GetIndexAndSequenceNumber();
      hit.position = toGlm(JPH::Vec3(contact.mPosition));
      hit.normal = toGlm(contact.mContactNormal);
      onHit(hit);
    }
  }

  PhysicsWorld::Impl& world;
  HitCallback onHit;
  float stepOffset;
  JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterController::CharacterController(PhysicsWorld& world,
                                         const CharacterControllerDesc& desc,
                                         HitCallback onHit)
    : impl(std::make_unique<Impl>(*world.impl, std::move(onHit),
                                  desc.stepOffset)) {
  JPH::Ref<JPH::CharacterVirtualSettings> settings =
      new JPH::CharacterVirtualSettings();
  settings->mShape = new JPH::CapsuleShape(0.5f * desc.height, desc.radius);
  settings->mUp = JPH::Vec3::sAxisY();
  settings->mMaxSlopeAngle = desc.slopeLimit;
  // The position is the center of the capsule (as with the PhysX controller):
  // contacts below the cylinder part count as ground
  settings->mSupportingVolume =
      JPH::Plane(JPH::Vec3::sAxisY(), 0.5f * desc.height);
  settings->mCharacterPadding = desc.contactOffset;
  // Smoother movement over the triangle mesh of the terrain
  settings->mEnhancedInternalEdgeRemoval = true;

  impl->character =
      new JPH::CharacterVirtual(settings, JPH::RVec3(toJolt(desc.position)),
                                JPH::Quat::sIdentity(), 0, &impl->world.system);
  impl->character->SetListener(impl.get());
}

CharacterController::~CharacterController() = default;

bool CharacterController::move(glm::vec3 displacement, float time_delta) {
  if (time_delta <= 0.0f) {
    return impl->character->IsSupported();
  }

  JPH::PhysicsSystem& system = impl->world.system;
  JPH::Vec3 velocity = toJolt(displacement / time_delta);
  // Like the PhysX controller: pushing down onto walkable ground (e.g. gravity)
  // does not make the character slide down the slope. On slopes that are too
  // steep the character still slides.
  if (impl->character->GetGroundState() ==
          JPH::CharacterVirtual::EGroundState::OnGround &&
      velocity.GetY() < 0.0f) {
    velocity.SetY(0.0f);
  }
  impl->character->SetLinearVelocity(velocity);

  JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
  updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, impl->stepOffset, 0.0f);
  impl->character->ExtendedUpdate(
      time_delta, system.GetGravity(), updateSettings,
      system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
      system.GetDefaultLayerFilter(Layers::MOVING), {}, {},
      impl->world.tempAllocator);

  return impl->character->IsSupported();
}

bool CharacterController::isOnWalkableGround() const {
  return impl->character->GetGroundState() ==
         JPH::CharacterVirtual::EGroundState::OnGround;
}

glm::vec3 CharacterController::getPosition() const {
  return toGlm(JPH::Vec3(impl->character->GetPosition()));
}

void CharacterController::setPosition(glm::vec3 position) {
  impl->character->SetPosition(JPH::RVec3(toJolt(position)));
  impl->character->SetLinearVelocity(JPH::Vec3::sZero());
  // Updates the ground state (e.g. on the ground after a fall) for the next
  // move
  JPH::PhysicsSystem& system = impl->world.system;
  impl->character->RefreshContacts(
      system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
      system.GetDefaultLayerFilter(Layers::MOVING), {}, {},
      impl->world.tempAllocator);
}

struct Ragdoll::Impl {
  explicit Impl(PhysicsWorld::Impl& world) : world(world) {}

  PhysicsWorld::Impl& world;
  JPH::Ref<JPH::RagdollSettings> settings;
  JPH::Ref<JPH::Ragdoll> ragdoll;
  bool active = false;
};

Ragdoll::Ragdoll(PhysicsWorld& world, const std::vector<RagdollPart>& parts)
    : impl(std::make_unique<Impl>(*world.impl)) {
  JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
  settings->mSkeleton = new JPH::Skeleton();
  settings->mParts.resize(parts.size());
  std::vector<JPH::Mat44> bindMatrices;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const RagdollPart& part = parts[i];
    settings->mSkeleton->AddJoint("part" + std::to_string(i), part.parent);

    // The body has the transform of its bone, so the capsule is placed in the
    // space of the bone
    JPH::Mat44 bind = toJoltRigid(part.bindTransform);
    bindMatrices.push_back(bind);
    JPH::Mat44 toPart = bind.InversedRotationTranslation();
    JPH::Vec3 start = toPart * toJolt(part.capsuleStart);
    JPH::Vec3 end = toPart * toJolt(part.capsuleEnd);
    float halfHeight = 0.5f * (end - start).Length();
    JPH::Ref<JPH::Shape> core =
        halfHeight > 1e-3f
            ? static_cast<JPH::Shape*>(
                  new JPH::CapsuleShape(halfHeight, part.radius))
            : static_cast<JPH::Shape*>(new JPH::SphereShape(part.radius));
    glm::quat alongCapsule =
        halfHeight > 1e-3f
            ? rotationBetween(glm::vec3(0.0f, 1.0f, 0.0f), toGlm(end - start))
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    JPH::RotatedTranslatedShapeSettings shape(0.5f * (start + end),
                                              toJolt(alongCapsule), core);
    JPH::ShapeSettings::ShapeResult shapeResult = shape.Create();
    if (shapeResult.HasError()) {
      std::cerr << "ERROR: Could not create ragdoll shape: "
                << shapeResult.GetError().c_str() << std::endl;
      return;
    }

    JPH::RagdollSettings::Part& body = settings->mParts[i];
    body.SetShape(shapeResult.Get());
    body.mPosition = JPH::RVec3(bind.GetTranslation());
    body.mRotation = bind.GetQuaternion();
    body.mMotionType = JPH::EMotionType::Dynamic;
    body.mObjectLayer = Layers::MOVING;
    body.mMotionQuality =
        JPH::EMotionQuality::LinearCast;  // does not tunnel through thin walls
                                          // when it is hit hard
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
      glm::vec3 twist = glm::normalize(joint.twistAxis);
      glm::vec3 plane = glm::normalize(
          joint.planeAxis - glm::dot(joint.planeAxis, twist) * twist);
      if (joint.type == RagdollJoint::HINGE) {
        JPH::Ref<JPH::HingeConstraintSettings> hinge =
            new JPH::HingeConstraintSettings();
        hinge->mPoint1 = hinge->mPoint2 = JPH::RVec3(toJolt(joint.position));
        hinge->mHingeAxis1 = hinge->mHingeAxis2 = toJolt(twist);
        hinge->mNormalAxis1 = hinge->mNormalAxis2 = toJolt(plane);
        hinge->mLimitsMin = joint.minAngle;
        hinge->mLimitsMax = joint.maxAngle;
        hinge->mMaxFrictionTorque = joint.friction;
        body.mToParent = hinge;
      } else {
        JPH::Ref<JPH::SwingTwistConstraintSettings> swingTwist =
            new JPH::SwingTwistConstraintSettings();
        swingTwist->mPosition1 = swingTwist->mPosition2 =
            JPH::RVec3(toJolt(joint.position));
        swingTwist->mTwistAxis1 = swingTwist->mTwistAxis2 = toJolt(twist);
        swingTwist->mPlaneAxis1 = swingTwist->mPlaneAxis2 = toJolt(plane);
        swingTwist->mNormalHalfConeAngle = joint.swingAngle;
        swingTwist->mPlaneHalfConeAngle = joint.swingAngle;
        swingTwist->mTwistMinAngle = -joint.twistAngle;
        swingTwist->mTwistMaxAngle = joint.twistAngle;
        swingTwist->mMaxFrictionTorque = joint.friction;
        body.mToParent = swingTwist;
      }
    }
  }

  settings->Stabilize();
  // Neighbouring parts overlap at the joints, they must not push each other
  // apart
  settings->DisableParentChildCollisions(bindMatrices.data(), 0.02f);
  settings->CalculateBodyIndexToConstraintIndex();
  settings->CalculateConstraintIndexToBodyIdxPair();
  impl->settings = settings;
  impl->ragdoll = settings->CreateRagdoll(0, 0, &impl->world.system);
  if (impl->ragdoll == nullptr) {
    std::cerr << "ERROR: Could not create the ragdoll (too many bodies?)"
              << std::endl;
  }
}

Ragdoll::~Ragdoll() {
  deactivate();  // Jolt only destroys bodies that are not in the physics system
}

void Ragdoll::activate(const std::vector<glm::mat4>& transforms,
                       const std::vector<glm::vec3>& velocities,
                       glm::vec3 angularVelocity) {
  if (!impl->ragdoll || transforms.size() != getPartCount()) {
    return;
  }
  if (!impl->active) {
    impl->ragdoll->AddToPhysicsSystem(JPH::EActivation::DontActivate);
    impl->active = true;
  }
  std::vector<JPH::Mat44> matrices;
  for (const glm::mat4& transform : transforms) {
    matrices.push_back(toJoltRigid(transform));
  }
  impl->ragdoll->SetPose(JPH::RVec3::sZero(), matrices.data());
  impl->ragdoll->ResetWarmStart();
  JPH::BodyInterface& bodyInterface = impl->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < getPartCount(); ++i) {
    glm::vec3 velocity =
        i < velocities.size() ? velocities[i] : glm::vec3(0.0f);
    bodyInterface.SetLinearAndAngularVelocity(
        impl->ragdoll->GetBodyID(static_cast<int>(i)), toJolt(velocity),
        toJolt(angularVelocity));
  }
  impl->ragdoll->Activate();
}

void Ragdoll::deactivate() {
  if (impl->ragdoll && impl->active) {
    impl->ragdoll->RemoveFromPhysicsSystem();
  }
  impl->active = false;
}

bool Ragdoll::isActive() const { return impl->active; }

std::size_t Ragdoll::getPartCount() const {
  return impl->ragdoll ? impl->ragdoll->GetBodyCount() : 0;
}

void Ragdoll::getTransforms(std::vector<glm::mat4>& transforms) const {
  transforms.resize(getPartCount());
  const JPH::BodyInterface& bodyInterface =
      impl->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < transforms.size(); ++i) {
    transforms[i] = toGlm(
        bodyInterface
            .GetWorldTransform(impl->ragdoll->GetBodyID(static_cast<int>(i)))
            .ToMat44());
  }
}

float Ragdoll::getMaxLinearSpeed() const {
  float speed = 0.0f;
  const JPH::BodyInterface& bodyInterface =
      impl->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < getPartCount(); ++i) {
    speed = std::max(
        speed,
        bodyInterface
            .GetLinearVelocity(impl->ragdoll->GetBodyID(static_cast<int>(i)))
            .Length());
  }
  return speed;
}

float Ragdoll::getMaxAngularSpeed() const {
  float speed = 0.0f;
  const JPH::BodyInterface& bodyInterface =
      impl->world.system.GetBodyInterface();
  for (std::size_t i = 0; i < getPartCount(); ++i) {
    speed = std::max(
        speed,
        bodyInterface
            .GetAngularVelocity(impl->ragdoll->GetBodyID(static_cast<int>(i)))
            .Length());
  }
  return speed;
}

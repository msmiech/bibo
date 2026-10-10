#include "Physics.hpp"

// Jolt.h must be included before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>

using namespace bibo::physics;

namespace
{
	//Object layers: static bodies never need to be tested against other static bodies
	namespace Layers
	{
		const JPH::ObjectLayer NON_MOVING = 0;
		const JPH::ObjectLayer MOVING = 1;
	}

	namespace BroadPhaseLayers
	{
		const JPH::BroadPhaseLayer NON_MOVING(0);
		const JPH::BroadPhaseLayer MOVING(1);
		const JPH::uint NUM_LAYERS = 2;
	}

	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override
		{
			return object1 == Layers::MOVING || object2 == Layers::MOVING;
		}
	};

	class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override
		{
			return layer1 == Layers::MOVING || layer2 == BroadPhaseLayers::MOVING;
		}
	};

	void traceImpl(const char* format, ...)
	{
		char buffer[1024];
		va_list list;
		va_start(list, format);
		vsnprintf(buffer, sizeof(buffer), format, list);
		va_end(list);
		std::cout << "Jolt: " << buffer << std::endl;
	}

#ifdef JPH_ENABLE_ASSERTS
	bool assertFailedImpl(const char* expression, const char* message, const char* file, JPH::uint line)
	{
		std::cerr << "Jolt assert failed: " << file << ":" << line << ": (" << expression << ") " << (message != nullptr ? message : "") << std::endl;
		return true; //break into the debugger
	}
#endif

	JPH::Vec3 toJolt(glm::vec3 v)
	{
		return JPH::Vec3(v.x, v.y, v.z);
	}

	JPH::Quat toJolt(glm::quat q)
	{
		return JPH::Quat(q.x, q.y, q.z, q.w);
	}

	glm::vec3 toGlm(JPH::Vec3Arg v)
	{
		return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
	}
}

struct PhysicsWorld::Impl
{
	BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
	ObjectLayerPairFilterImpl objectLayerPairFilter;
	JPH::TempAllocatorImpl tempAllocator{ 10 * 1024 * 1024 };
	//The PhysX version also used a single worker thread (PxDefaultCpuDispatcherCreate(1))
	JPH::JobSystemThreadPool jobSystem{ JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1 };
	JPH::PhysicsSystem system;

	BodyHandle addBody(const JPH::ShapeSettings& shapeSettings, glm::vec3 position, glm::quat rotation, JPH::EMotionType motionType)
	{
		JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
		if (shapeResult.HasError())
		{
			std::cerr << "ERROR: Could not create physics shape: " << shapeResult.GetError().c_str() << std::endl;
			return INVALID_BODY;
		}

		bool isStatic = motionType == JPH::EMotionType::Static;
		JPH::BodyCreationSettings bodySettings(shapeResult.Get(), JPH::RVec3(toJolt(position)), toJolt(rotation), motionType,
			isStatic ? Layers::NON_MOVING : Layers::MOVING);
		//Same values as the material of the former PhysX implementation
		bodySettings.mFriction = 0.5f;
		bodySettings.mRestitution = 0.5f;

		JPH::BodyID body = system.GetBodyInterface().CreateAndAddBody(bodySettings, isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		if (body.IsInvalid())
		{
			std::cerr << "ERROR: Could not create physics body (too many bodies?)" << std::endl;
			return INVALID_BODY;
		}
		return body.GetIndexAndSequenceNumber();
	}
};

PhysicsWorld::PhysicsWorld()
{
	JPH::RegisterDefaultAllocator();
	JPH::Trace = traceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailedImpl;)
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	impl = std::make_unique<Impl>();

	//The scene only contains a few dozen bodies
	const JPH::uint maxBodies = 1024;
	const JPH::uint numBodyMutexes = 0; //0 = default
	const JPH::uint maxBodyPairs = 1024;
	const JPH::uint maxContactConstraints = 1024;
	impl->system.Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
		impl->broadPhaseLayerInterface, impl->objectVsBroadPhaseLayerFilter, impl->objectLayerPairFilter);
	impl->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld()
{
	impl.reset();
	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
}

void PhysicsWorld::update(float time_delta)
{
	if (time_delta <= 0.0f)
	{
		return;
	}
	//Jolt recommends one collision step per 1/60 s; capped so that long stalls (e.g. dragging the window) don't freeze the game
	int collisionSteps = std::min(8, std::max(1, static_cast<int>(std::ceil(time_delta * 60.0f))));
	impl->system.Update(time_delta, collisionSteps, &impl->tempAllocator, &impl->jobSystem);
}

BodyHandle PhysicsWorld::createStaticBox(glm::vec3 position, glm::vec3 halfExtent)
{
	//Jolt requires the convex radius to be smaller than the smallest half extent (e.g. for thin walls)
	float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ halfExtent.x, halfExtent.y, halfExtent.z }));
	JPH::BoxShapeSettings box(toJolt(halfExtent), convexRadius);
	box.SetEmbedded();
	return impl->addBody(box, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::createDynamicBox(glm::vec3 position, glm::vec3 halfExtent, float density)
{
	float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ halfExtent.x, halfExtent.y, halfExtent.z }));
	JPH::BoxShapeSettings box(toJolt(halfExtent), convexRadius);
	box.SetDensity(density);
	box.SetEmbedded();
	return impl->addBody(box, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), JPH::EMotionType::Dynamic);
}

BodyHandle PhysicsWorld::createStaticSphere(glm::vec3 position, float radius)
{
	JPH::SphereShapeSettings sphere(radius);
	sphere.SetEmbedded();
	return impl->addBody(sphere, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), JPH::EMotionType::Static);
}

BodyHandle PhysicsWorld::createStaticMesh(const float* vertices, unsigned int numVertices, const unsigned int* indices, unsigned int numTriangles,
	float scale, glm::vec3 position, glm::quat rotation)
{
	JPH::VertexList vertexList;
	vertexList.reserve(numVertices);
	for (unsigned int i = 0; i < numVertices; ++i)
	{
		vertexList.push_back(JPH::Float3(vertices[i * 3] * scale, vertices[i * 3 + 1] * scale, vertices[i * 3 + 2] * scale));
	}

	JPH::IndexedTriangleList triangles;
	triangles.reserve(numTriangles);
	for (unsigned int i = 0; i < numTriangles; ++i)
	{
		triangles.push_back(JPH::IndexedTriangle(indices[i * 3], indices[i * 3 + 1], indices[i * 3 + 2], 0));
	}

	//Removes degenerate and duplicate triangles (like the welding options of the former PhysX cooking)
	JPH::MeshShapeSettings mesh(std::move(vertexList), std::move(triangles));
	mesh.SetEmbedded();
	return impl->addBody(mesh, position, rotation, JPH::EMotionType::Static);
}

void PhysicsWorld::optimizeBroadPhase()
{
	impl->system.OptimizeBroadPhase();
}

glm::mat4 PhysicsWorld::getTransform(BodyHandle body) const
{
	JPH::RVec3 position;
	JPH::Quat rotation;
	impl->system.GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body), position, rotation);

	glm::mat4 transform = glm::mat4_cast(glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()));
	transform[3] = glm::vec4(toGlm(JPH::Vec3(position)), 1.0f);
	return transform;
}

struct CharacterController::Impl : public JPH::CharacterContactListener
{
	Impl(PhysicsWorld::Impl& world, HitCallback onHit, float stepOffset) : world(world), onHit(std::move(onHit)), stepOffset(stepOffset)
	{
	}

	void OnContactAdded(const JPH::CharacterVirtual*, const JPH::CharacterContact& contact, JPH::CharacterContactSettings&) override
	{
		if (onHit && !contact.mBodyB.IsInvalid())
		{
			onHit(contact.mBodyB.GetIndexAndSequenceNumber());
		}
	}

	PhysicsWorld::Impl& world;
	HitCallback onHit;
	float stepOffset;
	JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterController::CharacterController(PhysicsWorld& world, const CharacterControllerDesc& desc, HitCallback onHit)
	: impl(std::make_unique<Impl>(*world.impl, std::move(onHit), desc.stepOffset))
{
	JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
	settings->mShape = new JPH::CapsuleShape(0.5f * desc.height, desc.radius);
	settings->mUp = JPH::Vec3::sAxisY();
	settings->mMaxSlopeAngle = desc.slopeLimit;
	//The position is the center of the capsule (as with the PhysX controller): contacts below the cylinder part count as ground
	settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), 0.5f * desc.height);
	settings->mCharacterPadding = desc.contactOffset;
	//Smoother movement over the triangle mesh of the terrain
	settings->mEnhancedInternalEdgeRemoval = true;

	impl->character = new JPH::CharacterVirtual(settings, JPH::RVec3(toJolt(desc.position)), JPH::Quat::sIdentity(), 0, &impl->world.system);
	impl->character->SetListener(impl.get());
}

CharacterController::~CharacterController() = default;

bool CharacterController::move(glm::vec3 displacement, float time_delta)
{
	if (time_delta <= 0.0f)
	{
		return impl->character->IsSupported();
	}

	JPH::PhysicsSystem& system = impl->world.system;
	JPH::Vec3 velocity = toJolt(displacement / time_delta);
	//Like the PhysX controller: pushing down onto walkable ground (e.g. gravity) does not make the character slide down the slope.
	//On slopes that are too steep the character still slides.
	if (impl->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && velocity.GetY() < 0.0f)
	{
		velocity.SetY(0.0f);
	}
	impl->character->SetLinearVelocity(velocity);

	JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
	updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, impl->stepOffset, 0.0f);
	impl->character->ExtendedUpdate(time_delta, system.GetGravity(), updateSettings,
		system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
		system.GetDefaultLayerFilter(Layers::MOVING),
		{}, {}, impl->world.tempAllocator);

	return impl->character->IsSupported();
}

glm::vec3 CharacterController::getPosition() const
{
	return toGlm(JPH::Vec3(impl->character->GetPosition()));
}

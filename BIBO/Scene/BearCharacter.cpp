#include "BearCharacter.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include "../Animation/Skeleton.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	//Movement (per second): the speeds of the earlier versions, reached smoothly
	const float FORWARD_SPEED = 10.0f, BACKWARD_SPEED = 7.5f;
	const float ACCELERATION = 40.0f, DECELERATION = 50.0f;
	const float TURN_RATE = glm::radians(90.0f);
	//The jump and the fall were tuned at 60 FPS: rising by 10 per second minus the gravity of 2 frames, falling by 0.981 per frame and second of falling
	const float MAX_JUMP_TIME = 0.5f, JUMP_SPEED = 10.0f - 2.0f * 0.981f, GRAVITY = -0.981f * 60.0f;
	//Ragdoll: it lies still when all parts are slower than this for a moment
	const float REST_SPEED = 0.35f, REST_ANGULAR_SPEED = 1.2f, REST_TIME = 0.3f;
	const float MIN_RAGDOLL_TIME = 0.8f, MAX_RAGDOLL_TIME = 4.0f;
	//After getting up the bear cannot be knocked down again right away
	const float PROTECTED_TIME = 1.0f;
	//Standing this long without input starts an idle animation
	const float IDLE_DELAY = 6.0f;
	//Wind in the fur (fur lengths / s^2)
	const glm::vec3 WIND(9.0f, 0.0f, 6.0f);
}

physics::CharacterControllerDesc BearCharacter::characterControllerDesc(glm::vec3 position)
{
	physics::CharacterControllerDesc desc; //A controller is a kinematic capsule, moved by the game logic
	desc.position = position; //initial position
	desc.contactOffset = 0.05f; //controller skin within which contacts generated
	desc.stepOffset = 1.0f; //max obstacle height the character can climb
	desc.slopeLimit = glm::radians(45.0f); // max slope the character can walk
	desc.radius = 1.0f; //radius of the capsule
	desc.height = 0.6f; //height of the controller
	return desc;
}

namespace
{
	//From the center of the capsule down to the ground it stands on
	float rootOffset()
	{
		physics::CharacterControllerDesc desc = BearCharacter::characterControllerDesc(glm::vec3(0.0f));
		return 0.5f * desc.height + desc.radius + desc.contactOffset;
	}
}

BearCharacter::BearCharacter(physics::PhysicsWorld& world, const resources::SkinnedModel& model, glm::vec3 startPosition, glm::vec3 startDirection,
	physics::CharacterController::HitCallback onHit)
	: world(world), model(model), startPosition(startPosition), startDirection(glm::normalize(startDirection)), onHit(std::move(onHit)), rig(model.skeleton),
	direction(this->startDirection)
{
	controller = std::make_unique<physics::CharacterController>(world, characterControllerDesc(startPosition), [this](const physics::CharacterController::Hit& hit) {
		if (this->onHit)
		{
			this->onHit(hit);
		}
	});
	ragdoll = std::make_unique<physics::Ragdoll>(world, rig.ragdollParts());
	//The feet and the fur only stand on static bodies (terrain, rocks), not on the ragdoll itself
	probe = [this](glm::vec3 from, glm::vec3 to, glm::vec3& point, glm::vec3& normal) {
		physics::RayHit hit;
		if (!this->world.castRay(from, to - from, hit, true))
		{
			return false;
		}
		point = hit.position;
		normal = hit.normal;
		return true;
	};

	bindCombDirections = fur::combDirections(model.normals, glm::vec3(0.0f, 0.0f, 1.0f), UP);
	furLengths.resize(model.positions.size());
	for (std::size_t i = 0; i < furLengths.size(); ++i)
	{
		bool hasFur = static_cast<int>(model.surfaces[i]) == resources::SkinnedModel::FUR;
		furLengths[i] = hasFur ? model.colors[i].a * fur::MAX_LENGTH : 0.0f;
	}
	reset();
}

BearCharacter::~BearCharacter() = default;

LocomotionInput BearCharacter::locomotionInput() const
{
	LocomotionInput input;
	input.rootPosition = controller->getPosition() - UP * rootOffset();
	input.direction = direction;
	input.velocity = velocity;
	input.yawRate = yawRate;
	input.grounded = standingOnGround;
	return input;
}

void BearCharacter::reset()
{
	ragdoll->deactivate();
	state = State::CONTROL;
	direction = startDirection;
	speed = yawRate = 0.0f;
	velocity = glm::vec3(0.0f);
	jumpTimeLeft = fallingTime = 0.0f;
	standingOnGround = false;
	knockDownPending = false;
	stateTime = restTime = protectedTime = idleWaitTime = 0.0f;
	positionOffset = glm::vec3(0.0f);
	controller->setPosition(startPosition);
	rig.reset(locomotionInput(), probe);
	updateSkin(0.0f);
	guideHairs.reset(positions);
}

glm::vec3 BearCharacter::rawPosition() const
{
	if (state == State::RAGDOLL)
	{
		//The center of the body is this much lower than the center of the capsule when the bear stands
		return rig.getCenter() + UP * 0.28f;
	}
	return controller->getPosition();
}

glm::vec3 BearCharacter::getPosition() const
{
	return rawPosition() + positionOffset;
}

void BearCharacter::setState(State newState)
{
	glm::vec3 before = getPosition();
	state = newState;
	stateTime = 0.0f;
	restTime = 0.0f;
	positionOffset = before - rawPosition();
}

void BearCharacter::knockDown(const physics::CharacterController::Hit& hit)
{
	if (state == State::CONTROL && !isProtected())
	{
		knockDownPending = true;
		pendingHit = hit;
	}
}

void BearCharacter::update(float timeDelta, const BearInput& input)
{
	if (timeDelta <= 0.0f)
	{
		return;
	}
	time += timeDelta;
	stateTime += timeDelta;
	protectedTime = std::max(0.0f, protectedTime - timeDelta);
	positionOffset *= 1.0f - animation::smoothingFactor(0.15f, timeDelta);

	switch (state)
	{
	case State::CONTROL:
		updateControl(timeDelta, input);
		break;
	case State::RAGDOLL:
		updateRagdoll(timeDelta);
		break;
	case State::GETTING_UP:
		updateGettingUp(timeDelta);
		break;
	}
	updateSkin(timeDelta);
}

void BearCharacter::updateControl(float timeDelta, const BearInput& input)
{
	yawRate = std::clamp(input.turn, -1.0f, 1.0f) * TURN_RATE;
	direction = glm::normalize(glm::rotateY(direction, yawRate * timeDelta));

	float forward = std::clamp(input.forward, -1.0f, 1.0f);
	float targetSpeed = forward >= 0.0f ? forward * FORWARD_SPEED : forward * BACKWARD_SPEED;
	bool speedingUp = std::abs(targetSpeed) > std::abs(speed) && targetSpeed * speed >= 0.0f;
	float change = (speedingUp ? ACCELERATION : DECELERATION) * timeDelta;
	speed += std::clamp(targetSpeed - speed, -change, change);
	glm::vec3 displacement = direction * speed * timeDelta;

	//The bear rises while SPACE is held (at most MAX_JUMP_TIME), the next jump is only possible after landing
	bool jumped = false;
	if (input.jump && jumpTimeLeft > 0.0f)
	{
		jumpTimeLeft -= timeDelta;
		fallingTime = 0.0f;
		displacement.y += JUMP_SPEED * timeDelta;
		jumped = true;
	}
	else
	{
		if (!standingOnGround)
		{
			jumpTimeLeft = 0.0f; //SPACE was released in the air
		}
		//The longer the bear falls, the faster it falls (on the ground the controller ignores the downward movement)
		fallingTime += timeDelta;
		displacement.y += GRAVITY * fallingTime * timeDelta;
	}

	glm::vec3 before = controller->getPosition();
	standingOnGround = controller->move(displacement, timeDelta); //may knock the bear down (onHit)
	velocity = (controller->getPosition() - before) / timeDelta;
	if (standingOnGround && !jumped)
	{
		jumpTimeLeft = MAX_JUMP_TIME;
		fallingTime = 1.0f; //falls quickly when walking over an edge
	}

	//Idle animations: after standing still for a while, until the player moves the bear again
	bool hasInput = forward != 0.0f || input.turn != 0.0f || input.jump;
	if (hasInput)
	{
		idleWaitTime = 0.0f;
		rig.stopIdle();
	}
	else if (!rig.isIdling() && standingOnGround && std::abs(speed) < 0.05f)
	{
		idleWaitTime += timeDelta;
		if (idleWaitTime > IDLE_DELAY)
		{
			startIdle(static_cast<BearRig::Idle>(nextIdle));
		}
	}

	rig.updateLocomotion(timeDelta, locomotionInput(), probe);
	if (knockDownPending)
	{
		startRagdoll();
	}
}

void BearCharacter::startIdle(BearRig::Idle idle)
{
	if (state != State::CONTROL)
	{
		return;
	}
	rig.startIdle(idle);
	idleWaitTime = 0.0f;
	nextIdle = (idle + 1) % BearRig::IDLE_COUNT;
}

void BearCharacter::startRagdoll()
{
	knockDownPending = false;
	std::vector<glm::mat4> transforms;
	std::vector<glm::vec3> velocities;
	rig.ragdollTransforms(transforms);
	rig.ragdollVelocities(velocities);

	//Bounces off the obstacle: the velocity towards it is reflected, the bear is thrown up a bit and tumbles head over heels
	glm::vec3 away(pendingHit.normal.x, 0.0f, pendingHit.normal.z);
	away = glm::length(away) > 1e-3f ? glm::normalize(away) : -direction;
	float towards = std::min(0.0f, glm::dot(velocity, away));
	glm::vec3 bounce = velocity - 1.25f * towards * away + away * 1.0f + UP * 3.0f;
	glm::vec3 spin = glm::cross(UP, away) * 3.0f + UP * (0.8f * std::sin(time * 7.0f));
	glm::vec3 center = rig.getCenter();
	for (std::size_t i = 0; i < transforms.size(); ++i)
	{
		glm::vec3 limbMotion = velocities[i] - velocity; //e.g. a swinging leg
		velocities[i] = bounce + 0.5f * limbMotion + glm::cross(spin, glm::vec3(transforms[i][3]) - center);
	}
	ragdoll->activate(transforms, velocities, spin);
	guideHairs.addImpulse(pendingHit.position, away * 6.0f + UP * 2.0f, 1.0f);
	speed = 0.0f;
	setState(State::RAGDOLL);
}

void BearCharacter::updateRagdoll(float timeDelta)
{
	std::vector<glm::mat4> transforms;
	ragdoll->getTransforms(transforms);
	rig.updateFromRagdoll(timeDelta, transforms);

	bool resting = ragdoll->getMaxLinearSpeed() < REST_SPEED && ragdoll->getMaxAngularSpeed() < REST_ANGULAR_SPEED;
	restTime = resting ? restTime + timeDelta : 0.0f;
	if ((stateTime > MIN_RAGDOLL_TIME && restTime > REST_TIME) || stateTime > MAX_RAGDOLL_TIME)
	{
		startGettingUp();
	}
}

void BearCharacter::startGettingUp()
{
	ragdoll->deactivate(); //the character controller must not collide with the ragdoll
	BearRig::GetUpStart start = rig.beginGetUp(probe, direction);
	glm::vec3 before = getPosition();
	direction = start.direction;
	controller->setPosition(start.rootPosition + UP * (rootOffset() + 0.02f));
	speed = yawRate = 0.0f;
	velocity = glm::vec3(0.0f);
	state = State::GETTING_UP;
	stateTime = 0.0f;
	positionOffset = before - rawPosition();
}

void BearCharacter::updateGettingUp(float timeDelta)
{
	//The capsule stays on the ground (it may also be pushed out of an obstacle), the animation follows it
	fallingTime += timeDelta;
	standingOnGround = controller->move(UP * (GRAVITY * std::min(fallingTime, 1.0f) * timeDelta), timeDelta);
	if (standingOnGround)
	{
		fallingTime = 0.0f;
	}
	if (rig.updateGetUp(timeDelta, probe, controller->getPosition() - UP * rootOffset()))
	{
		state = State::CONTROL;
		stateTime = 0.0f;
		protectedTime = PROTECTED_TIME;
		jumpTimeLeft = 0.0f;
	}
}

void BearCharacter::updateSkin(float timeDelta)
{
	rig.skinMatrices(skinMatrices);
	model.skin(skinMatrices, positions, normals, &bindCombDirections, &combDirections);
	++version;
	if (timeDelta <= 0.0f)
	{
		return;
	}

	fur::Environment environment;
	environment.time = time;
	environment.wind = WIND;
	glm::vec3 center = rig.getCenter();
	glm::vec3 ground, groundNormal;
	if (probe(center + UP * 0.3f, center - UP * 3.0f, ground, groundNormal))
	{
		environment.hasGround = true;
		environment.groundPoint = ground;
		environment.groundNormal = groundNormal;
	}
	for (const fur::SphereCollider& obstacle : obstacles)
	{
		if (glm::distance(obstacle.center, center) < obstacle.radius + getBoundsRadius())
		{
			environment.spheres.push_back(obstacle);
		}
	}
	guideHairs.update(timeDelta, positions, normals, furLengths, environment);
}

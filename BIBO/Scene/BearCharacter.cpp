#include "BearCharacter.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include "CreatureMeshes.hpp"
#include "../Animation/Skeleton.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	//Movement (per second): the speeds of the earlier versions, reached smoothly
	const float FORWARD_SPEED = BearCharacter::FORWARD_SPEED, BACKWARD_SPEED = 7.5f;
	const float ACCELERATION = 40.0f, DECELERATION = 50.0f;
	const float TURN_RATE = glm::radians(90.0f); //forward/turn input
	//Steering towards a direction (move input): quick on the spot, wider curves at full speed. The remaining angle is turned
	//within about STEER_RESPONSE seconds, so the bear eases out of a turn
	const float STEER_RATE_STANDING = glm::radians(420.0f), STEER_RATE_RUNNING = glm::radians(220.0f), STEER_RATE_AIR = glm::radians(150.0f);
	const float STEER_RESPONSE = 0.12f;
	//Jump: a ballistic arc. Rising with JUMP_GRAVITY reaches JUMP_HEIGHT after JUMP_RISE_TIME, releasing the button early
	//cuts the rise short (stronger gravity), falling is faster than rising (the fall of the earlier versions)
	const float JUMP_GRAVITY = 2.0f * BearCharacter::JUMP_HEIGHT / (BearCharacter::JUMP_RISE_TIME * BearCharacter::JUMP_RISE_TIME);
	const float JUMP_SPEED = JUMP_GRAVITY * BearCharacter::JUMP_RISE_TIME;
	const float JUMP_CUT_GRAVITY = 2.5f * JUMP_GRAVITY;
	const float FALL_GRAVITY = 0.981f * 60.0f, MAX_FALL_SPEED = 35.0f;
	//The bear can still jump shortly after it ran over an edge, and a jump pressed shortly before landing happens on landing
	const float COYOTE_TIME = 0.12f, JUMP_BUFFER_TIME = 0.15f;
	//In the air the bear keeps its momentum, the player can only change it a little
	const float AIR_ACCELERATION = 14.0f;
	//Swimming: slower to speed up, it glides on in the water; it turns slower than on the ground
	const float SWIM_ACCELERATION = 9.0f, SWIM_DECELERATION = 5.0f;
	const float STEER_RATE_SWIM = glm::radians(150.0f);
	//The water pulls the bear to its floating depth (1/s^2) and slows it down (1/s), falling in it loses most of its speed
	const float BUOYANCY = 24.0f, WATER_DRAG = 7.0f, ENTRY_DAMPING = 0.3f;
	//A jump at the surface (out onto the shore), it may start this far from the floating depth
	const float SWIM_JUMP_SPEED = 8.0f, SWIM_JUMP_TOLERANCE = 0.3f;
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
	tongueRoot = rig.getBone("tongue.1");
	tongueTip = rig.getBone("tongue.3");
	leftPaw = rig.getBone("hand.L");
	rightPaw = rig.getBone("hand.R");
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

LocomotionInput BearCharacter::locomotionInput(bool jumped) const
{
	LocomotionInput input;
	input.rootPosition = controller->getPosition() - UP * rootOffset();
	input.direction = direction;
	input.velocity = velocity;
	input.yawRate = yawRate;
	input.grounded = standingOnGround;
	input.jumped = jumped;
	input.swimming = swimming;
	return input;
}

void BearCharacter::reset()
{
	fish = Fish::NONE;
	eatenFish = 0;
	splashes = 0;
	jumpWasHeld = false;
	placeAt(startPosition, startDirection);
}

void BearCharacter::setStart(glm::vec3 position, glm::vec3 direction)
{
	startPosition = position;
	glm::vec3 horizontal(direction.x, 0.0f, direction.z);
	startDirection = glm::length(horizontal) > 1e-6f ? glm::normalize(horizontal) : startDirection;
}

float BearCharacter::groundOffset()
{
	return rootOffset();
}

void BearCharacter::placeAt(glm::vec3 position, glm::vec3 newDirection)
{
	ragdoll->deactivate();
	state = State::CONTROL;
	glm::vec3 horizontal(newDirection.x, 0.0f, newDirection.z);
	direction = glm::length(horizontal) > 1e-6f ? glm::normalize(horizontal) : startDirection;
	speed = yawRate = 0.0f;
	velocity = airVelocity = glm::vec3(0.0f);
	verticalSpeed = airTime = jumpBuffer = fallingTime = 0.0f;
	jumping = false;
	standingOnGround = false;
	swimming = false;
	knockDownPending = false;
	stateTime = restTime = protectedTime = idleWaitTime = 0.0f;
	positionOffset = glm::vec3(0.0f);
	rig.setCarrying(fish == Fish::CARRIED);
	controller->setPosition(position);
	rig.reset(locomotionInput(), probe);
	updateSkin(0.0f);
	guideHairs.reset(positions);
}

float BearCharacter::capsuleWaterDepth() const
{
	return waterLevel - (controller->getPosition().y - rootOffset());
}

float BearCharacter::getWaterDepth() const
{
	return waterLevel - (getPosition().y - rootOffset());
}

int BearCharacter::takeEatenFish()
{
	int result = eatenFish;
	eatenFish = 0;
	return result;
}

int BearCharacter::takeSplashes()
{
	int result = splashes;
	splashes = 0;
	return result;
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

float BearCharacter::steer(float timeDelta, const BearInput& input, bool& hasInput)
{
	glm::vec3 move(input.move.x, 0.0f, input.move.z);
	float amount = std::min(glm::length(move), 1.0f);
	if (amount > 1e-3f)
	{
		hasInput = true;
		glm::vec3 wanted = glm::normalize(move);
		//Positive: the wanted direction is to the left
		float angle = std::atan2(glm::cross(direction, wanted).y, glm::dot(direction, wanted));
		float maxRate = swimming ? STEER_RATE_SWIM : standingOnGround
			? glm::mix(STEER_RATE_STANDING, STEER_RATE_RUNNING, std::clamp(std::abs(speed) / FORWARD_SPEED, 0.0f, 1.0f))
			: STEER_RATE_AIR;
		float targetRate = std::clamp(angle / STEER_RESPONSE, -maxRate, maxRate);
		yawRate += (targetRate - yawRate) * animation::smoothingFactor(0.03f, timeDelta);
		if (std::abs(yawRate * timeDelta) > std::abs(angle))
		{
			yawRate = angle / timeDelta; //does not turn past the wanted direction
		}
		direction = glm::normalize(glm::rotateY(direction, yawRate * timeDelta));
		//Slows down while turning around (instead of running sideways)
		float alignment = glm::dot(direction, wanted);
		return amount * (swimming ? SWIM_SPEED : FORWARD_SPEED) * std::clamp(0.35f + 0.65f * alignment, 0.0f, 1.0f);
	}

	yawRate = std::clamp(input.turn, -1.0f, 1.0f) * TURN_RATE;
	direction = glm::normalize(glm::rotateY(direction, yawRate * timeDelta));
	float forward = std::clamp(input.forward, -1.0f, 1.0f);
	hasInput = forward != 0.0f || input.turn != 0.0f;
	if (swimming)
	{
		return forward * SWIM_SPEED * (forward >= 0.0f ? 1.0f : 0.5f);
	}
	return forward >= 0.0f ? forward * FORWARD_SPEED : forward * BACKWARD_SPEED;
}

void BearCharacter::updateControl(float timeDelta, const BearInput& playerInput)
{
	//A caught fish is eaten as soon as the bear stands on the ground, the player waits until the last bite
	BearInput input = playerInput;
	if (fish == Fish::CARRIED && standingOnGround && !jumping && !swimming)
	{
		rig.startIdle(BearRig::IDLE_EAT);
		fish = Fish::EATING;
		idleWaitTime = 0.0f;
	}
	if (fish == Fish::EATING)
	{
		if (rig.getIdle() != BearRig::IDLE_EAT)
		{
			fish = Fish::NONE; //finished (or the player moved on after the last bite)
		}
		else if (rig.getIdleTime() < BearRig::EAT_BITES[2])
		{
			input = BearInput();
			jumpWasHeld = playerInput.jump; //a jump needs a new press afterwards
		}
		else if (!lastBiteTaken)
		{
			lastBiteTaken = true;
			++eatenFish;
		}
	}

	//In deep water the bear swims (until it is shallow enough to walk), not while it rises from a jump out of the water
	float depth = capsuleWaterDepth();
	bool wasSwimming = swimming;
	swimming = depth > (swimming ? WADE_DEPTH : SWIM_DEPTH) && !(jumping && verticalSpeed > 0.0f);
	if (swimming && !wasSwimming)
	{
		++splashes;
		verticalSpeed *= ENTRY_DAMPING;
		jumping = false;
		rig.stopIdle();
	}

	bool hasInput = false;
	float targetSpeed = steer(timeDelta, input, hasInput);
	glm::vec3 horizontal;
	if (swimming)
	{
		bool speedingUp = std::abs(targetSpeed) > std::abs(speed) && targetSpeed * speed >= 0.0f;
		float change = (speedingUp ? SWIM_ACCELERATION : SWIM_DECELERATION) * timeDelta;
		speed += std::clamp(targetSpeed - speed, -change, change);
		horizontal = airVelocity = direction * speed;
	}
	else if (standingOnGround)
	{
		bool speedingUp = std::abs(targetSpeed) > std::abs(speed) && targetSpeed * speed >= 0.0f;
		float change = (speedingUp ? ACCELERATION : DECELERATION) * timeDelta;
		speed += std::clamp(targetSpeed - speed, -change, change);
		horizontal = airVelocity = direction * speed;
	}
	else
	{
		//Without input the bear flies on, with input it changes its course a little
		if (hasInput)
		{
			glm::vec3 change = direction * targetSpeed - airVelocity;
			float maxChange = AIR_ACCELERATION * timeDelta;
			if (glm::length(change) > maxChange)
			{
				change *= maxChange / glm::length(change);
			}
			airVelocity += change;
		}
		horizontal = airVelocity;
		speed = glm::dot(airVelocity, direction); //runs on with this speed after landing
	}

	//The jump starts when the button is pressed (or was pressed shortly before landing), also shortly after running over an edge
	bool jumpPressed = input.jump && !jumpWasHeld;
	jumpWasHeld = input.jump;
	jumpBuffer = jumpPressed ? JUMP_BUFFER_TIME : std::max(0.0f, jumpBuffer - timeDelta);
	airTime = standingOnGround || swimming ? 0.0f : airTime + timeDelta;
	bool jumped = false;
	if (swimming)
	{
		if (jumpBuffer > 0.0f && std::abs(depth - FLOAT_DEPTH) < SWIM_JUMP_TOLERANCE)
		{
			//Out of the water, e.g. onto the shore
			verticalSpeed = SWIM_JUMP_SPEED;
			jumping = jumped = true;
			jumpBuffer = 0.0f;
			swimming = false;
		}
		else
		{
			//Floats at its depth and bobs a little, the water slows it down
			float bob = 0.04f * std::sin(time * 2.6f);
			verticalSpeed += ((depth - FLOAT_DEPTH - bob) * BUOYANCY - verticalSpeed * WATER_DRAG) * timeDelta;
		}
	}
	else if (jumpBuffer > 0.0f && !jumping && airTime <= COYOTE_TIME)
	{
		verticalSpeed = JUMP_SPEED;
		jumping = jumped = true;
		jumpBuffer = 0.0f;
	}
	else if (controller->isOnWalkableGround() && !jumping)
	{
		verticalSpeed = 0.0f; //the controller keeps the bear on the ground when it walks down a slope
	}
	else
	{
		//Holding the button until the highest point jumps highest, falling is faster than rising
		float gravity = verticalSpeed <= 0.0f ? FALL_GRAVITY : (input.jump ? JUMP_GRAVITY : JUMP_CUT_GRAVITY);
		verticalSpeed = std::max(verticalSpeed - gravity * timeDelta, -MAX_FALL_SPEED);
	}

	glm::vec3 displacement = (horizontal + UP * verticalSpeed) * timeDelta;
	glm::vec3 before = controller->getPosition();
	standingOnGround = controller->move(displacement, timeDelta); //may knock the bear down (onHit)
	velocity = (controller->getPosition() - before) / timeDelta;
	if (verticalSpeed > 0.0f && velocity.y < 0.5f * verticalSpeed)
	{
		verticalSpeed = std::max(velocity.y, 0.0f); //hit something above
	}
	if (standingOnGround && verticalSpeed <= 0.0f)
	{
		jumping = false; //landed
	}

	//Idle animations: after standing still for a while, until the player moves the bear again
	hasInput = hasInput || input.jump;
	if (hasInput)
	{
		idleWaitTime = 0.0f;
		rig.stopIdle();
	}
	else if (rig.isLooking() && rig.isIdling() && rig.getIdle() < BearRig::RESTING_IDLE_COUNT)
	{
		rig.stopIdle(); //paying attention
		idleWaitTime = 0.0f;
	}
	else if (!rig.isIdling() && !rig.isLooking() && standingOnGround && !swimming && std::abs(speed) < 0.05f)
	{
		idleWaitTime += timeDelta;
		if (idleWaitTime > IDLE_DELAY)
		{
			startIdle(static_cast<BearRig::Idle>(nextIdle));
		}
	}

	rig.setCarrying(fish == Fish::CARRIED);
	rig.updateLocomotion(timeDelta, locomotionInput(jumped), probe);
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
	nextIdle = (idle + 1) % BearRig::RESTING_IDLE_COUNT;
}

bool BearCharacter::catchFish()
{
	if (!canCatchFish())
	{
		return false;
	}
	fish = Fish::CARRIED;
	lastBiteTaken = false;
	rig.stopIdle(); //e.g. it was stretching at the edge of the water
	return true;
}

glm::vec3 BearCharacter::getMouthPosition() const
{
	return rig.getBonePosition(tongueTip);
}

BearCharacter::HeldFish BearCharacter::carriedFish() const
{
	//Across the mouth, its back up
	glm::vec3 forward = rig.getBonePosition(tongueTip) - rig.getBonePosition(tongueRoot);
	forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : direction;
	glm::vec3 left = glm::cross(UP, forward);
	left = glm::length(left) > 1e-4f ? glm::normalize(left) : glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 up = glm::cross(forward, left);
	HeldFish held;
	held.center = getMouthPosition() + forward * 0.04f;
	held.rotation = glm::quat_cast(glm::mat3(-forward, up, left));
	held.wiggle = 0.45f * std::sin(time * 14.0f); //it flaps
	return held;
}

bool BearCharacter::getHeldFish(HeldFish& held) const
{
	if (fish == Fish::NONE)
	{
		return false;
	}
	held = carriedFish();
	if (fish == Fish::CARRIED)
	{
		return true;
	}
	//In the front paws, upright with its head towards the mouth: the end that is left is at the mouth when the bear bites
	float idleTime = rig.getIdleTime();
	int bites = 0;
	for (float bite : BearRig::EAT_BITES)
	{
		bites += idleTime >= bite ? 1 : 0;
	}
	if (bites >= 3)
	{
		return false; //eaten
	}
	glm::vec3 mouth = getMouthPosition();
	glm::vec3 paws = 0.5f * (rig.getBonePosition(leftPaw) + rig.getBonePosition(rightPaw));
	glm::vec3 axis = mouth - paws;
	axis = glm::length(axis) > 1e-4f ? glm::normalize(axis) : UP;
	glm::vec3 back = direction - axis * glm::dot(direction, axis);
	back = glm::length(back) > 1e-4f ? glm::normalize(back) : glm::vec3(0.0f, 0.0f, 1.0f);
	const float TOPS[3] = { 0.5f, SALMON_HEAD_JOINT, SALMON_TAIL_JOINT }; //of what is left after 0, 1 and 2 bites
	float nextBite = BearRig::EAT_BITES[bites];
	float gap = 0.04f + 0.14f * glm::smoothstep(0.0f, 0.4f, nextBite - idleTime); //closer to the mouth when it bites
	HeldFish eating;
	eating.center = mouth - axis * (TOPS[bites] + gap);
	eating.rotation = glm::quat_cast(glm::mat3(glm::cross(back, axis), back, axis));
	eating.bites = bites;
	eating.wiggle = 0.12f * std::sin(time * 6.0f);
	float take = glm::smoothstep(BearRig::EAT_TAKE_START, BearRig::EAT_TAKE_END, idleTime);
	eating.center = glm::mix(held.center, eating.center, take);
	eating.rotation = glm::slerp(held.rotation, eating.rotation, take);
	held = eating;
	return true;
}

void BearCharacter::startRagdoll()
{
	knockDownPending = false;
	swimming = false;
	fish = Fish::NONE; //drops it
	rig.setCarrying(false);
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
	speed = verticalSpeed = 0.0f;
	jumping = false;
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
	standingOnGround = controller->move(UP * (-FALL_GRAVITY * std::min(fallingTime, 1.0f) * timeDelta), timeDelta);
	if (standingOnGround)
	{
		fallingTime = 0.0f;
	}
	if (rig.updateGetUp(timeDelta, probe, controller->getPosition() - UP * rootOffset()))
	{
		state = State::CONTROL;
		stateTime = 0.0f;
		protectedTime = PROTECTED_TIME;
		airVelocity = glm::vec3(0.0f);
		verticalSpeed = airTime = jumpBuffer = 0.0f;
		jumping = false;
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

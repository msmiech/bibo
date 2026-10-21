#include "Beaver.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "BeaverModel.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace
{
	const float PI = glm::pi<float>();
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	const glm::vec3 LEFT(1.0f, 0.0f, 0.0f);
	const glm::vec3 FORWARD(0.0f, 0.0f, 1.0f);
	const float TURN_RATE = 2.5f; //radians per second
	const float SOUND_DURATION = 0.17f; //the mouth opens and closes once per sound
	const float NIBBLE_DURATION = 2.4f;
	const float CHEER_DURATION = 1.0f;
	const float SLAP_DURATION = 0.7f;
	//The arms hang a bit forward, raised this far the hands are at the mouth (radians around the shoulder)
	const float ARM_REST = 0.45f, ARM_AT_MOUTH = 1.95f;
	//Height of the marker above the head (world units)
	const float MARKER_HEIGHT = 0.6f;

	float wrapAngle(float angle)
	{
		return std::remainder(angle, 2.0f * PI);
	}

	float smooth(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	/**
	* 0 at the start and the end of an animation, 1 in between (blends in and out over fade seconds)
	*/
	float envelope(float time, float duration, float fade)
	{
		if (time < 0.0f || time > duration)
		{
			return 0.0f;
		}
		return smooth(time / fade) * smooth((duration - time) / fade);
	}

	float approach(float value, float target, float halfLife, float timeDelta)
	{
		return value + (target - value) * (1.0f - std::exp2(-timeDelta / halfLife));
	}

	glm::mat4 rotationX(float angle)
	{
		return glm::rotate(glm::mat4(1.0f), angle, LEFT);
	}
}

Beaver::Beaver(glm::vec3 position, glm::vec3 direction, std::uint32_t seed)
	: home(position), homeDirection(glm::length(glm::vec3(direction.x, 0.0f, direction.z)) > 1e-6f ? glm::normalize(glm::vec3(direction.x, 0.0f, direction.z)) : FORWARD),
	seed(seed)
{
	reset();
}

float Beaver::uniform(float from, float to)
{
	return from + (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}

void Beaver::reset()
{
	random.seed(seed);
	position = home;
	yaw = std::atan2(homeDirection.x, homeDirection.z);
	headYaw = headPitch = eyeYaw = eyePitch = 0.0f;
	time = 0.0f;
	talking = false;
	talkBlend = mouth = nod = 0.0f;
	mouthTime = nibbleTime = cheerTime = slapTime = -1.0f;
	nextNibble = uniform(2.0f, 5.0f);
	nextSlap = uniform(7.0f, 12.0f);
}

void Beaver::setTalking(bool talking)
{
	this->talking = talking;
	if (talking)
	{
		nibbleTime = -1.0f;
	}
}

void Beaver::speak()
{
	mouthTime = 0.0f;
}

void Beaver::cheer()
{
	cheerTime = 0.0f;
	slapTime = 0.25f; //the tail slaps when it lands
	nibbleTime = -1.0f;
}

void Beaver::update(float timeDelta, glm::vec3 bear)
{
	if (timeDelta <= 0.0f)
	{
		return;
	}
	time += timeDelta;

	//Turns to the bear when it is close and looks at it, otherwise it looks along the track and around a bit
	glm::vec3 toBear = bear - position;
	float horizontal = glm::length(glm::vec2(toBear.x, toBear.z));
	bool attentive = horizontal < ATTENTION_DISTANCE && horizontal > 0.1f;
	float wantedYaw = attentive ? std::atan2(toBear.x, toBear.z) : std::atan2(homeDirection.x, homeDirection.z);
	float turn = wrapAngle(wantedYaw - yaw);
	yaw = wrapAngle(yaw + std::clamp(turn, -TURN_RATE * timeDelta, TURN_RATE * timeDelta));
	float lookYaw, lookPitch;
	if (attentive)
	{
		glm::vec3 fromHead = bear - getFacePosition();
		lookYaw = wrapAngle(std::atan2(fromHead.x, fromHead.z) - yaw);
		lookPitch = std::atan2(fromHead.y, glm::length(glm::vec2(fromHead.x, fromHead.z)));
	}
	else
	{
		lookYaw = 0.5f * std::sin(time * 0.37f) * std::sin(time * 0.11f + 1.0f);
		lookPitch = 0.1f * std::sin(time * 0.23f);
	}
	headYaw = approach(headYaw, std::clamp(lookYaw, -0.9f, 0.9f), 0.15f, timeDelta);
	headPitch = approach(headPitch, std::clamp(lookPitch, -0.5f, 0.4f), 0.15f, timeDelta);
	//The eyes are quicker than the head: they look there first and take the rest of the turn the head cannot make
	eyeYaw = approach(eyeYaw, std::clamp(lookYaw - headYaw, -0.4f, 0.4f), 0.03f, timeDelta);
	eyePitch = approach(eyePitch, std::clamp(lookPitch - headPitch, -0.3f, 0.3f), 0.03f, timeDelta);
	talkBlend = approach(talkBlend, talking ? 1.0f : 0.0f, 0.2f, timeDelta);

	//The mouth opens and closes once per sound, the head nods with it
	if (mouthTime >= 0.0f)
	{
		mouthTime += timeDelta;
		if (mouthTime > SOUND_DURATION)
		{
			mouthTime = -1.0f;
		}
	}
	float sound = mouthTime >= 0.0f ? std::sin(PI * mouthTime / SOUND_DURATION) : 0.0f;
	nod = 0.07f * sound;

	//Between the conversations: nibbling (quick small bites, the hands at the mouth) and slapping the tail
	if (nibbleTime >= 0.0f)
	{
		nibbleTime += timeDelta;
		if (nibbleTime > NIBBLE_DURATION)
		{
			nibbleTime = -1.0f;
		}
	}
	else if (!talking && cheerTime < 0.0f)
	{
		nextNibble -= timeDelta;
		if (nextNibble <= 0.0f)
		{
			nibbleTime = 0.0f;
			nextNibble = uniform(5.0f, 10.0f);
		}
	}
	float nibble = envelope(nibbleTime, NIBBLE_DURATION, 0.35f);
	mouth = std::max(sound, nibble * 0.35f * std::abs(std::sin(time * 19.0f)));

	if (cheerTime >= 0.0f)
	{
		cheerTime += timeDelta;
		if (cheerTime > CHEER_DURATION)
		{
			cheerTime = -1.0f;
		}
	}
	if (slapTime >= 0.0f)
	{
		slapTime += timeDelta;
		if (slapTime > SLAP_DURATION)
		{
			slapTime = -1.0f;
		}
	}
	else if (!talking)
	{
		nextSlap -= timeDelta;
		if (nextSlap <= 0.0f)
		{
			slapTime = 0.0f;
			nextSlap = uniform(9.0f, 16.0f);
		}
	}
}

glm::vec3 Beaver::getMarkerPosition() const
{
	return position + UP * (BEAVER_HEIGHT * SCALE + MARKER_HEIGHT + 0.07f * std::sin(time * 2.2f));
}

glm::quat Beaver::getMarkerRotation() const
{
	return glm::angleAxis(time * 1.3f, UP);
}

glm::vec3 Beaver::getColliderCenter() const
{
	return position + UP * (0.42f * SCALE);
}

float Beaver::getColliderRadius() const
{
	return 0.42f * SCALE;
}

glm::vec3 Beaver::getFacePosition() const
{
	std::vector<glm::mat4> pose = getPose();
	return 0.5f * (glm::vec3(pose[BeaverModel::EYE_LEFT][3]) + glm::vec3(pose[BeaverModel::EYE_RIGHT][3]));
}

glm::vec3 Beaver::getHeadPosition() const
{
	glm::quat turn = glm::angleAxis(yaw, UP);
	return position + turn * ((BEAVER_NECK + glm::vec3(0.0f, 0.12f, 0.1f)) * SCALE);
}

std::vector<glm::mat4> Beaver::getPose() const
{
	std::vector<glm::mat4> parts(BeaverModel::PART_COUNT);
	const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(SCALE));

	//Cheering: a hop with the arms up
	float hop = 0.0f, hopPitch = 0.0f;
	float cheer = envelope(cheerTime, CHEER_DURATION, 0.15f);
	if (cheerTime >= 0.0f && cheerTime < 0.45f)
	{
		float t = cheerTime / 0.45f;
		hop = 4.0f * 0.12f * t * (1.0f - t) * SCALE;
		hopPitch = -0.12f * std::sin(PI * t);
	}
	float breathing = 1.0f + 0.012f * std::sin(time * 2.1f);
	float nibble = envelope(nibbleTime, NIBBLE_DURATION, 0.35f);
	//Leans towards the bear while talking and over its hands while nibbling
	float pitch = 0.05f * talkBlend + 0.08f * nibble + hopPitch + 0.015f * std::sin(time * 1.3f);

	glm::mat4 frame = glm::translate(glm::mat4(1.0f), position + UP * hop) * glm::rotate(glm::mat4(1.0f), yaw, UP) * rotationX(pitch);
	parts[BeaverModel::BODY] = frame * scale * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, breathing, 1.0f));

	glm::mat4 head = frame * glm::translate(glm::mat4(1.0f), BEAVER_NECK * SCALE * glm::vec3(1.0f, breathing, 1.0f))
		* glm::rotate(glm::mat4(1.0f), headYaw, UP) * rotationX(-headPitch - pitch + nod + 0.1f * nibble) //the body leans, the head stays level
		* glm::rotate(glm::mat4(1.0f), 0.08f * talkBlend * std::sin(time * 1.7f), FORWARD);
	parts[BeaverModel::HEAD] = head * scale;
	parts[BeaverModel::JAW] = head * glm::translate(glm::mat4(1.0f), BEAVER_JAW * SCALE) * rotationX(0.5f * mouth) * scale;
	//The eyes turn in their sockets (yaw left, pitch up), blinking is not needed with such small eyes
	for (bool left : { true, false })
	{
		parts[left ? BeaverModel::EYE_LEFT : BeaverModel::EYE_RIGHT] = head * glm::translate(glm::mat4(1.0f), BeaverModel::eyeCenter(left) * SCALE)
			* glm::rotate(glm::mat4(1.0f), eyeYaw, UP) * rotationX(-eyePitch) * scale;
	}

	//Arms: gestures while talking (one after the other), at the mouth while nibbling, up in the air when cheering
	for (int side = 0; side < 2; ++side)
	{
		float sign = side == 0 ? 1.0f : -1.0f;
		float phase = side == 0 ? 0.0f : 2.2f;
		float gesture = talkBlend * (0.45f + 0.4f * std::sin(time * 2.9f + phase));
		float raise = ARM_REST + gesture + (ARM_AT_MOUTH - ARM_REST) * nibble + 2.3f * cheer;
		float out = talkBlend * 0.25f * (0.5f + 0.5f * std::sin(time * 2.1f + phase)) - 0.3f * nibble + 0.45f * cheer;
		glm::vec3 shoulder = BEAVER_SHOULDER * glm::vec3(sign, 1.0f, 1.0f) * SCALE;
		parts[side == 0 ? BeaverModel::ARM_LEFT : BeaverModel::ARM_RIGHT] = frame * glm::translate(glm::mat4(1.0f), shoulder)
			* glm::rotate(glm::mat4(1.0f), sign * out, FORWARD) * rotationX(-raise) * scale;
	}

	//The feet stay on the ground (only turned with the body), they point a bit outwards
	glm::mat4 turn = glm::rotate(glm::mat4(1.0f), yaw, UP);
	for (int side = 0; side < 2; ++side)
	{
		float sign = side == 0 ? 1.0f : -1.0f;
		glm::vec3 foot = glm::vec3(turn * glm::vec4(BEAVER_FOOT * glm::vec3(sign, 1.0f, 1.0f) * SCALE, 0.0f));
		parts[side == 0 ? BeaverModel::FOOT_LEFT : BeaverModel::FOOT_RIGHT] = glm::translate(glm::mat4(1.0f), position + foot + UP * hop) * turn
			* glm::rotate(glm::mat4(1.0f), 0.3f * sign, UP) * scale;
	}

	//The tail lies on the ground and sways a little, it is lifted and slapped down (the body leaning forward lifts it)
	float lift = 0.0f;
	if (slapTime >= 0.0f)
	{
		float t = slapTime;
		lift = t < 0.35f ? 0.7f * smooth(t / 0.35f) : 0.7f * (1.0f - smooth((t - 0.35f) / 0.08f)) + 0.08f * std::sin(PI * std::clamp((t - 0.43f) / 0.2f, 0.0f, 1.0f));
	}
	parts[BeaverModel::TAIL] = frame * glm::translate(glm::mat4(1.0f), BEAVER_TAIL * SCALE) * glm::rotate(glm::mat4(1.0f), 0.12f * std::sin(time * 0.9f), UP)
		* rotationX(lift - pitch) * scale;
	return parts;
}

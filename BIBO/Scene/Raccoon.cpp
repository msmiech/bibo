#include "Raccoon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "RaccoonModel.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace
{
	const float PI = glm::pi<float>();
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	const glm::vec3 LEFT(1.0f, 0.0f, 0.0f);
	const glm::vec3 FORWARD(0.0f, 0.0f, 1.0f);
	const float TURN_RATE = 4.0f; //radians per second
	//Upright it leans back this far (radians), sitting a bit less; its hips are this much lower then (model units)
	const float REAR_ANGLE = 1.15f, SIT_UPRIGHT = 0.85f, HIP_DROP = 0.085f;
	//The hind legs are folded forward under it when it is upright
	const float LEG_FOLD = 0.8f;
	const float STRIDE_LENGTH = 0.32f * Raccoon::SCALE; //distance per step cycle
	const float SOUND_DURATION = 0.15f;
	const float HOP_DURATION = 0.35f, HOP_HEIGHT = 0.18f;
	const float JUMP_DURATION = 0.6f, JUMP_HEIGHT = 0.55f;
	const float WASH_DURATION = 2.6f;
	//Height of the marker above the face (world units)
	const float MARKER_HEIGHT = 1.0f;

	float wrapAngle(float angle)
	{
		return std::remainder(angle, 2.0f * PI);
	}

	float smooth(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

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

	glm::mat4 rotationY(float angle)
	{
		return glm::rotate(glm::mat4(1.0f), angle, UP);
	}

	glm::mat4 translation(glm::vec3 offset)
	{
		return glm::translate(glm::mat4(1.0f), offset);
	}

	//Advances a timer of an animation, -1 when it is over
	void advance(float& timer, float duration, float timeDelta)
	{
		if (timer >= 0.0f)
		{
			timer += timeDelta;
			if (timer > duration)
			{
				timer = -1.0f;
			}
		}
	}

	float arc(float timer, float duration, float height)
	{
		if (timer < 0.0f)
		{
			return 0.0f;
		}
		float t = timer / duration;
		return 4.0f * height * t * (1.0f - t);
	}
}

Raccoon::Raccoon(Ground ground, std::uint32_t seed, float girth) : ground(std::move(ground)), seed(seed), girth(girth)
{
	reset();
}

float Raccoon::uniform(float from, float to)
{
	return from + (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}

void Raccoon::reset()
{
	random.seed(seed);
	time = activityTime = 0.0f;
	talking = false;
	talkBlend = mouth = nod = stride = gaitPhase = headYaw = headPitch = 0.0f;
	mouthTime = hopTime = jumpTime = washTime = -1.0f;
	nextHop = uniform(0.3f, 1.0f);
	nextWash = uniform(3.0f, 6.0f);
	chatters = 0;
	if (sits)
	{
		position = home;
		yaw = std::atan2(homeDirection.x, homeDirection.z);
		rear = SIT_UPRIGHT;
		activity = Activity::SITTING;
	}
	else
	{
		rear = 0.0f;
		activity = Activity::HIDDEN;
	}
}

void Raccoon::setActivity(Activity next)
{
	activity = next;
	activityTime = 0.0f;
}

void Raccoon::comeAndCheer(glm::vec2 from, glm::vec2 spot)
{
	float height = 0.0f;
	ground(from, height);
	origin = from;
	target = spot;
	position = glm::vec3(from.x, height, from.y);
	yaw = std::atan2(spot.x - from.x, spot.y - from.y);
	rear = 0.0f;
	hopTime = jumpTime = washTime = -1.0f;
	setActivity(Activity::COMING);
}

void Raccoon::leave()
{
	if (activity == Activity::COMING || activity == Activity::CHEERING)
	{
		target = origin;
		setActivity(Activity::LEAVING);
	}
}

void Raccoon::sitAt(glm::vec3 position, glm::vec3 direction)
{
	home = position;
	glm::vec3 flat(direction.x, 0.0f, direction.z);
	homeDirection = glm::length(flat) > 1e-6f ? glm::normalize(flat) : FORWARD;
	sits = true;
	reset();
}

void Raccoon::cheer()
{
	if (isVisible() && jumpTime < 0.0f)
	{
		jumpTime = 0.0f;
		washTime = -1.0f;
		mouthTime = 0.0f;
		++chatters;
	}
}

void Raccoon::setTalking(bool talking)
{
	this->talking = talking;
	if (talking)
	{
		washTime = -1.0f;
	}
}

void Raccoon::speak()
{
	mouthTime = 0.0f;
}

int Raccoon::takeChatters()
{
	int result = chatters;
	chatters = 0;
	return result;
}

void Raccoon::turnTo(float wantedYaw, float rate, float timeDelta)
{
	float turn = wrapAngle(wantedYaw - yaw);
	yaw = wrapAngle(yaw + std::clamp(turn, -rate * timeDelta, rate * timeDelta));
}

bool Raccoon::walk(float timeDelta, float speed)
{
	glm::vec2 here(position.x, position.z);
	glm::vec2 toTarget = target - here;
	float distance = glm::length(toTarget);
	if (distance < 0.25f)
	{
		return true;
	}
	//Turns towards the target, slower while it does not look there; it slows down before it gets there
	float difference = wrapAngle(std::atan2(toTarget.x, toTarget.y) - yaw);
	turnTo(yaw + difference, TURN_RATE, timeDelta);
	float pace = speed * std::clamp(std::cos(difference), 0.0f, 1.0f) * std::clamp(0.4f + distance / 2.0f, 0.0f, 1.0f);
	glm::vec2 next = here + glm::vec2(std::sin(yaw), std::cos(yaw)) * std::min(pace * timeDelta, distance);
	float height;
	if (!ground(next, height))
	{
		return true; //e.g. the water: it stays where it is
	}
	position = glm::vec3(next.x, position.y + (height - position.y) * std::min(1.0f, timeDelta * 12.0f), next.y);
	gaitPhase += pace * timeDelta / STRIDE_LENGTH;
	return false;
}

void Raccoon::update(float timeDelta, glm::vec3 bear)
{
	if (timeDelta <= 0.0f || activity == Activity::HIDDEN)
	{
		return;
	}
	time += timeDelta;
	activityTime += timeDelta;
	glm::vec3 toBear = bear - position;
	float bearDistance = glm::length(glm::vec2(toBear.x, toBear.z));
	float bearYaw = std::atan2(toBear.x, toBear.z);

	bool walking = false;
	float upright = 0.0f;
	bool attentive = false;
	switch (activity)
	{
	case Activity::COMING:
		walking = !walk(timeDelta, TROT_SPEED);
		if (!walking)
		{
			setActivity(Activity::CHEERING);
		}
		break;
	case Activity::LEAVING:
		walking = !walk(timeDelta, 1.3f * TROT_SPEED);
		if (!walking)
		{
			setActivity(Activity::HIDDEN);
			return;
		}
		break;
	case Activity::CHEERING:
		//Upright, turned to the bear, it hops and chatters every now and then
		upright = 1.0f;
		attentive = true;
		turnTo(bearYaw, 0.6f * TURN_RATE, timeDelta);
		nextHop -= timeDelta;
		if (nextHop <= 0.0f && hopTime < 0.0f && jumpTime < 0.0f)
		{
			hopTime = 0.0f;
			mouthTime = 0.0f;
			++chatters;
			nextHop = uniform(0.6f, 1.6f);
		}
		break;
	case Activity::SITTING:
		upright = SIT_UPRIGHT;
		attentive = talking || bearDistance < ATTENTION_DISTANCE;
		turnTo(attentive && bearDistance > 0.1f ? bearYaw : std::atan2(homeDirection.x, homeDirection.z), 0.5f * TURN_RATE, timeDelta);
		//Washes its paws between the conversations
		if (washTime < 0.0f && !talking)
		{
			nextWash -= timeDelta;
			if (nextWash <= 0.0f)
			{
				washTime = 0.0f;
				nextWash = uniform(5.0f, 10.0f);
			}
		}
		break;
	case Activity::HIDDEN:
		break;
	}
	advance(washTime, WASH_DURATION, timeDelta);
	advance(hopTime, HOP_DURATION, timeDelta);
	advance(jumpTime, JUMP_DURATION, timeDelta);
	rear = approach(rear, upright, 0.12f, timeDelta);
	stride = approach(stride, walking ? 1.0f : 0.0f, 0.08f, timeDelta);
	gaitPhase -= std::floor(gaitPhase);
	talkBlend = approach(talkBlend, talking ? 1.0f : 0.0f, 0.2f, timeDelta);

	//The head looks at the bear, otherwise ahead and around a bit
	float lookYaw = 0.0f, lookPitch = 0.0f;
	if (attentive)
	{
		glm::vec3 fromFace = bear - getFacePosition();
		lookYaw = wrapAngle(std::atan2(fromFace.x, fromFace.z) - yaw);
		lookPitch = std::atan2(fromFace.y, glm::length(glm::vec2(fromFace.x, fromFace.z)));
	}
	else if (!walking)
	{
		lookYaw = 0.6f * std::sin(time * 0.43f) * std::sin(time * 0.17f + 2.0f);
		lookPitch = 0.1f * std::sin(time * 0.29f);
	}
	headYaw = approach(headYaw, std::clamp(lookYaw, -1.0f, 1.0f), 0.12f, timeDelta);
	headPitch = approach(headPitch, std::clamp(lookPitch, -0.5f, 0.6f), 0.12f, timeDelta);

	advance(mouthTime, SOUND_DURATION, timeDelta);
	float sound = mouthTime >= 0.0f ? std::sin(PI * mouthTime / SOUND_DURATION) : 0.0f;
	nod = 0.08f * sound;
	float washing = envelope(washTime, WASH_DURATION, 0.3f);
	mouth = std::max(sound, 0.15f * washing * std::abs(std::sin(time * 9.0f)));
}

glm::vec3 Raccoon::getFacePosition() const
{
	std::vector<glm::mat4> pose = getPose();
	return glm::vec3(pose[RaccoonModel::HEAD] * glm::vec4(0.0f, 0.075f, 0.15f, 1.0f));
}

glm::vec3 Raccoon::getMarkerPosition() const
{
	return getFacePosition() + UP * (MARKER_HEIGHT + 0.07f * std::sin(time * 2.2f));
}

glm::quat Raccoon::getMarkerRotation() const
{
	return glm::angleAxis(time * 1.3f, UP);
}

glm::vec3 Raccoon::getColliderCenter() const
{
	return position + UP * (0.3f * SCALE);
}

float Raccoon::getColliderRadius() const
{
	return 0.24f * SCALE * std::sqrt(girth);
}

std::vector<glm::mat4> Raccoon::getPose() const
{
	std::vector<glm::mat4> parts(RaccoonModel::PART_COUNT);
	const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(SCALE));
	const float upright = smooth(rear);
	const float cycle = 2.0f * PI * gaitPhase;
	const float jump = envelope(jumpTime, JUMP_DURATION, 0.12f);
	const float washing = envelope(washTime, WASH_DURATION, 0.3f);

	//Hops while cheering, the big jump; bobs with every step
	float lift = arc(hopTime, HOP_DURATION, HOP_HEIGHT) + arc(jumpTime, JUMP_DURATION, JUMP_HEIGHT) + stride * 0.025f * SCALE * std::abs(std::sin(cycle));
	float breathing = 0.012f * std::sin(time * 2.3f);
	float lean = REAR_ANGLE * upright + 0.12f * jump + breathing - stride * 0.04f * std::sin(2.0f * cycle);

	glm::mat4 frame = translation(position + UP * lift) * rotationY(yaw);
	//The body turns up around the hips, which come down when it sits on its haunches
	glm::mat4 body = frame * translation((RACCOON_HIPS - UP * (HIP_DROP * upright)) * SCALE) * rotationX(-lean) * translation(-RACCOON_HIPS * SCALE);
	parts[RaccoonModel::BODY] = body * scale;

	//The head stays about level and looks around (nods with its sounds, down to its paws while it washes them)
	glm::mat4 head = body * translation(RACCOON_NECK * SCALE) * rotationX(0.9f * lean) * rotationY(headYaw)
		* rotationX(-headPitch + nod + 0.35f * washing + 0.08f * talkBlend * std::sin(time * 1.9f));
	parts[RaccoonModel::HEAD] = head * scale;
	parts[RaccoonModel::JAW] = head * translation(RACCOON_JAW * SCALE) * rotationX(0.55f * mouth) * scale;

	//The tail hangs a bit when it trots, swings from side to side and lies on the ground behind it when it is upright
	float wag = std::sin(time * (2.0f + 4.0f * upright)) * (0.15f + 0.25f * upright + 0.1f * stride);
	parts[RaccoonModel::TAIL] = body * translation(RACCOON_TAIL * SCALE) * rotationY(wag) * rotationX(0.95f * lean - 0.25f + 0.1f * stride) * scale;

	for (int side = 0; side < 2; ++side)
	{
		float sign = side == 0 ? 1.0f : -1.0f;
		//Trot: diagonal pairs of legs step together
		float frontSwing = stride * 0.5f * std::sin(cycle + (side == 0 ? 0.0f : PI));
		float hindSwing = stride * 0.5f * std::sin(cycle + (side == 0 ? PI : 0.0f));

		//Front paws: hanging in front of the chest when it is upright, waving when it cheers, rubbed together when it washes them,
		//gestures when it talks
		float phase = side == 0 ? 0.0f : PI;
		float wave = rear > 0.9f && activity == Activity::CHEERING ? 0.8f + 0.5f * std::sin(time * 9.0f + phase) : 0.0f;
		float raise = upright * (0.25f + wave) + 1.7f * jump + 0.7f * washing + talkBlend * (0.5f + 0.35f * std::sin(time * 3.1f + phase));
		float inwards = 0.35f * washing * (1.0f + 0.4f * std::sin(time * 12.0f + phase)) - 0.3f * jump;
		glm::vec3 shoulder = RACCOON_SHOULDER * glm::vec3(sign, 1.0f, 1.0f) * SCALE;
		parts[side == 0 ? RaccoonModel::FRONT_LEFT : RaccoonModel::FRONT_RIGHT] = body * translation(shoulder)
			* glm::rotate(glm::mat4(1.0f), -sign * inwards, FORWARD) * rotationX(frontSwing + lean * 0.65f * upright - raise) * scale;

		//Hind legs: under the hips, turned with the body (not leaned with it), folded forward when it is upright
		glm::vec3 hip = (RACCOON_HIP * glm::vec3(sign, 1.0f, 1.0f) - UP * (HIP_DROP * upright)) * SCALE;
		parts[side == 0 ? RaccoonModel::HIND_LEFT : RaccoonModel::HIND_RIGHT] = frame * translation(hip) * rotationX(hindSwing - LEG_FOLD * upright) * scale;
	}
	return parts;
}

RaccoonFans::RaccoonFans(Raccoon::Ground ground, Finder finder, std::uint32_t seed) : finder(std::move(finder)), seed(seed)
{
	for (std::size_t i = 0; i < COUNT; ++i)
	{
		raccoons.emplace_back(ground, seed + 31u * static_cast<std::uint32_t>(i));
	}
	reset();
}

void RaccoonFans::reset()
{
	random.seed(seed);
	for (Raccoon& raccoon : raccoons)
	{
		raccoon.reset();
	}
	nextArrival = 1.0f;
}

int RaccoonFans::getWatchingCount() const
{
	int count = 0;
	for (const Raccoon& raccoon : raccoons)
	{
		count += raccoon.getActivity() == Raccoon::Activity::COMING || raccoon.getActivity() == Raccoon::Activity::CHEERING ? 1 : 0;
	}
	return count;
}

void RaccoonFans::update(float timeDelta, bool cheering, glm::vec3 bear, glm::vec3 bearHead, glm::vec3 heading)
{
	if (timeDelta <= 0.0f)
	{
		return;
	}
	for (Raccoon& raccoon : raccoons)
	{
		raccoon.update(timeDelta, bearHead);
		glm::vec3 offset = raccoon.getPosition() - bear;
		bool past = glm::length(glm::vec2(offset.x, offset.z)) > GONE_DISTANCE;
		bool tired = (raccoon.getActivity() == Raccoon::Activity::CHEERING && raccoon.getActivityTime() > WATCH_TIME)
			|| (raccoon.getActivity() == Raccoon::Activity::COMING && raccoon.getActivityTime() > 2.0f * WATCH_TIME);
		if (!cheering || past || tired)
		{
			raccoon.leave();
		}
	}
	if (!cheering)
	{
		nextArrival = std::min(nextArrival, 1.0f);
		return;
	}

	//Another one comes out of the woods every few seconds, not too close to the others
	nextArrival -= timeDelta;
	if (nextArrival > 0.0f || getWatchingCount() >= MAX_WATCHING)
	{
		return;
	}
	nextArrival = 0.4f; //tries again soon if there is no spot
	auto hidden = std::ranges::find_if(raccoons, [](const Raccoon& raccoon) { return !raccoon.isVisible(); });
	glm::vec2 from, spot;
	if (hidden == raccoons.end() || !finder(bear, heading, random, from, spot))
	{
		return;
	}
	for (const Raccoon& other : raccoons)
	{
		glm::vec3 there = other.getPosition();
		if (other.isVisible() && glm::distance(glm::vec2(there.x, there.z), spot) < SPACING)
		{
			return;
		}
	}
	hidden->comeAndCheer(from, spot);
	std::uniform_real_distribution<float> wait(2.5f, 5.0f);
	nextArrival = wait(random);
}

void RaccoonFans::celebrate()
{
	for (Raccoon& raccoon : raccoons)
	{
		if (raccoon.getActivity() == Raccoon::Activity::CHEERING)
		{
			raccoon.cheer();
		}
	}
}

int RaccoonFans::takeChatters()
{
	int result = 0;
	for (Raccoon& raccoon : raccoons)
	{
		result += raccoon.takeChatters();
	}
	return result;
}

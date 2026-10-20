#include "Salmon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include "../World/WorldGenerator.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace
{
	const float PI = glm::pi<float>();
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	//Water that is shallower is not a pool (e.g. a wet edge), a salmon needs this much
	const float POOL_DEPTH = 0.05f;
	const float SALMON_DEPTH = 0.6f;
	//The salmon come out of the water from a bit below the surface
	const float EMERGE_DEPTH = 0.3f;
	//At most this many salmon of a pool are in the air at the same time
	const int MAX_JUMPS_PER_POOL = 2;
}

std::vector<WaterPool> bibo::scene::findWaterPools(const world::WorldGenerator& generator)
{
	std::vector<WaterPool> pools;
	const float sea = generator.getSeaLevel();
	const std::vector<world::TrackPath::Sample>& samples = generator.getPath().getSamples();
	std::vector<glm::vec2> water, deep;
	float distanceSum = 0.0f;
	int wetSamples = 0, dryRun = 0;
	auto finish = [&]() {
		if (!water.empty())
		{
			WaterPool pool;
			glm::vec2 sum(0.0f);
			for (glm::vec2 point : water)
			{
				sum += point;
			}
			glm::vec2 center = sum / static_cast<float>(water.size());
			pool.center = glm::vec3(center.x, sea, center.y);
			for (glm::vec2 point : water)
			{
				pool.radius = std::max(pool.radius, glm::distance(point, center) + 0.5f);
			}
			pool.trackDistance = distanceSum / static_cast<float>(wetSamples);
			pool.spots = deep.empty() ? water : deep;
			pools.push_back(pool);
		}
		water.clear();
		deep.clear();
		distanceSum = 0.0f;
		wetSamples = 0;
	};
	for (const world::TrackPath::Sample& sample : samples)
	{
		glm::vec2 center(sample.position.x, sample.position.z);
		glm::vec2 right(-sample.tangent.y, sample.tangent.x);
		bool wet = false;
		for (float lateral = -sample.floorLeft - 1.0f; lateral <= sample.floorRight + 1.0f; lateral += 0.75f)
		{
			glm::vec2 point = center + right * lateral;
			float depth = sea - generator.height(point);
			if (depth > POOL_DEPTH)
			{
				wet = true;
				water.push_back(point);
				if (depth > SALMON_DEPTH)
				{
					deep.push_back(point);
				}
			}
		}
		if (wet)
		{
			distanceSum += sample.distance;
			++wetSamples;
			dryRun = 0;
		}
		else if (!water.empty() && ++dryRun > 2)
		{
			finish(); //a few meters of dry ground between two pools
		}
	}
	finish();
	return pools;
}

glm::vec3 SalmonPools::Jump::position() const
{
	return start + velocity * time - UP * (0.5f * GRAVITY * time * time);
}

glm::quat SalmonPools::Jump::rotation() const
{
	glm::vec2 horizontal(velocity.x, velocity.z);
	float speed = glm::length(horizontal);
	float yaw = speed > 1e-4f ? std::atan2(horizontal.x, horizontal.y) : 0.0f;
	//The nose follows the arc: up when it comes out of the water, down when it falls back
	float pitch = std::atan2(velocity.y - GRAVITY * time, std::max(speed, 0.6f));
	return glm::angleAxis(yaw, UP) * glm::angleAxis(-pitch, glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(spin * time, glm::vec3(0.0f, 0.0f, 1.0f));
}

SalmonPools::SalmonPools(std::vector<WaterPool> pools, std::uint32_t seed) : pools(std::move(pools)), seed(seed)
{
	reset();
}

float SalmonPools::uniform(float from, float to)
{
	return from + (to - from) * static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}

void SalmonPools::reset()
{
	random.seed(seed);
	caught.assign(pools.size(), false);
	nextJump.assign(pools.size(), 0.0f);
	jumps.clear();
	droplets.clear();
}

int SalmonPools::getCaughtCount() const
{
	return static_cast<int>(std::count(caught.begin(), caught.end(), true));
}

void SalmonPools::splash(glm::vec3 position, float surface, int count)
{
	for (int i = 0; i < count; ++i)
	{
		float angle = uniform(0.0f, 2.0f * PI);
		float outwards = uniform(0.6f, 2.0f);
		Droplet droplet;
		droplet.position = glm::vec3(position.x, surface, position.z) + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * uniform(0.05f, 0.3f);
		droplet.velocity = glm::vec3(std::cos(angle) * outwards, uniform(2.0f, 4.5f), std::sin(angle) * outwards);
		droplet.size = uniform(0.05f, 0.11f);
		droplet.surface = surface - 0.05f;
		droplets.push_back(droplet);
	}
}

void SalmonPools::startJump(int pool, glm::vec3 bearCenter)
{
	const WaterPool& water = pools[pool];
	if (water.spots.empty())
	{
		return;
	}
	//Rather close to the bear (where it jumps over the water), so it has a chance to catch one
	glm::vec2 bear(bearCenter.x, bearCenter.z);
	glm::vec2 spot = water.spots[random() % water.spots.size()];
	for (int candidate = 0; candidate < 3; ++candidate)
	{
		glm::vec2 other = water.spots[random() % water.spots.size()];
		if (glm::distance(other, bear) < glm::distance(spot, bear))
		{
			spot = other;
		}
	}
	Jump jump;
	jump.pool = pool;
	jump.start = glm::vec3(spot.x, water.center.y - EMERGE_DEPTH, spot.y);
	float height = uniform(2.2f, 3.8f) + EMERGE_DEPTH;
	float angle = uniform(0.0f, 2.0f * PI);
	float speed = uniform(0.5f, 1.8f);
	jump.velocity = glm::vec3(std::cos(angle) * speed, std::sqrt(2.0f * GRAVITY * height), std::sin(angle) * speed);
	jump.duration = 2.0f * jump.velocity.y / GRAVITY;
	jump.spin = uniform(-3.0f, 3.0f);
	jump.wiggle = uniform(0.0f, 2.0f * PI);
	jumps.push_back(jump);
	splash(jump.start, water.center.y, 8);
}

int SalmonPools::update(float timeDelta, glm::vec3 bearCenter, glm::vec3 bearMouth, bool canCatch)
{
	if (timeDelta <= 0.0f)
	{
		return -1;
	}
	//Flying salmon splash back into the water
	for (std::size_t i = 0; i < jumps.size();)
	{
		Jump& jump = jumps[i];
		jump.time += timeDelta;
		if (jump.time >= jump.duration)
		{
			splash(jump.position(), pools[jump.pool].center.y, 10);
			jumps.erase(jumps.begin() + static_cast<std::ptrdiff_t>(i));
		}
		else
		{
			++i;
		}
	}
	for (std::size_t i = 0; i < droplets.size();)
	{
		Droplet& droplet = droplets[i];
		droplet.velocity.y -= GRAVITY * timeDelta;
		droplet.position += droplet.velocity * timeDelta;
		if (droplet.position.y < droplet.surface && droplet.velocity.y < 0.0f)
		{
			droplets.erase(droplets.begin() + static_cast<std::ptrdiff_t>(i));
		}
		else
		{
			++i;
		}
	}

	//New salmon jump while the bear is close to a pool whose salmon it has not caught yet
	for (std::size_t p = 0; p < pools.size(); ++p)
	{
		const WaterPool& pool = pools[p];
		float distance = glm::distance(glm::vec2(bearCenter.x, bearCenter.z), glm::vec2(pool.center.x, pool.center.z));
		if (caught[p] || distance > APPROACH_DISTANCE + pool.radius)
		{
			nextJump[p] = std::min(nextJump[p], 0.3f); //the first one soon after the bear came close
			continue;
		}
		nextJump[p] -= timeDelta;
		int inAir = static_cast<int>(std::count_if(jumps.begin(), jumps.end(), [p](const Jump& jump) { return jump.pool == static_cast<int>(p); }));
		if (nextJump[p] <= 0.0f && inAir < MAX_JUMPS_PER_POOL)
		{
			startJump(static_cast<int>(p), bearCenter);
			nextJump[p] = uniform(0.6f, 1.6f);
		}
	}

	if (!canCatch)
	{
		return -1;
	}
	for (std::size_t i = 0; i < jumps.size(); ++i)
	{
		const Jump& jump = jumps[i];
		glm::vec3 position = jump.position();
		if (!caught[jump.pool] && (glm::distance(position, bearMouth) < MOUTH_REACH || glm::distance(position, bearCenter) < BODY_REACH))
		{
			int pool = jump.pool;
			caught[pool] = true;
			jumps.erase(jumps.begin() + static_cast<std::ptrdiff_t>(i));
			return pool;
		}
	}
	return -1;
}

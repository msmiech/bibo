#include "Salmon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
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
	//The points of the water along a sample of the track are this far apart (meters across the track)
	const float WATER_STEP = 0.75f;
	//A place on the shore: this high above the water, this flat (height difference within SHORE_RADIUS), this far from it
	const float SHORE_MIN_HEIGHT = 0.15f, SHORE_MAX_HEIGHT = 4.0f, SHORE_FLATNESS = 0.35f, SHORE_RADIUS = 1.6f;
	const float SHORE_MIN_WATER_DISTANCE = 1.6f, SHORE_MAX_WATER_DISTANCE = 8.0f;
	//It may also lie a bit beyond the floor of the track (at the foot of the walls), and keeps this far from the route of runners
	const float SHORE_BEYOND_FLOOR = 4.0f, SHORE_ROUTE_CLEARANCE = 6.0f;
	//Steps across the floor when looking for a way around the water
	const float ROUTE_STEP = 0.5f;
	//The route is smoothed over this many points before and after (about meters), runners cut corners
	const std::size_t ROUTE_SMOOTHING = 4;

	bool isClosed(const std::vector<bool>& closed, std::size_t pool)
	{
		return pool < closed.size() && closed[pool];
	}
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
			pool.area = static_cast<float>(water.size()) * WATER_STEP * world::TrackPath::SAMPLE_SPACING;
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
		for (float lateral = -sample.floorLeft - 1.0f; lateral <= sample.floorRight + 1.0f; lateral += WATER_STEP)
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

int bibo::scene::largestPool(const std::vector<WaterPool>& pools)
{
	int largest = -1;
	for (std::size_t i = 0; i < pools.size(); ++i)
	{
		if (largest < 0 || pools[i].area > pools[largest].area)
		{
			largest = static_cast<int>(i);
		}
	}
	return largest;
}

std::vector<glm::vec2> bibo::scene::runningRoute(const world::WorldGenerator& generator, float from, float to)
{
	const float sea = generator.getSeaLevel();
	std::vector<glm::vec2> route;
	for (const world::TrackPath::Sample& sample : generator.getPath().getSamples())
	{
		if (sample.distance < from || sample.distance > to)
		{
			continue;
		}
		glm::vec2 middle(sample.position.x, sample.position.z);
		glm::vec2 right(-sample.tangent.y, sample.tangent.x);
		glm::vec2 trail = middle + right * sample.trailOffset;
		if (generator.height(trail) >= sea)
		{
			route.push_back(trail);
			continue;
		}
		//The trail is under water here: around it through the widest dry part of the floor
		float bestWidth = 0.0f, bestMiddle = 0.0f, start = 0.0f;
		bool dry = false;
		for (float lateral = -sample.floorLeft; lateral <= sample.floorRight + ROUTE_STEP; lateral += ROUTE_STEP)
		{
			bool here = lateral <= sample.floorRight && generator.height(middle + right * lateral) >= sea;
			if (here && !dry)
			{
				start = lateral;
			}
			else if (!here && dry && lateral - start > bestWidth)
			{
				bestWidth = lateral - start;
				bestMiddle = 0.5f * (start + lateral - ROUTE_STEP);
			}
			dry = here;
		}
		if (bestWidth > 0.0f)
		{
			route.push_back(middle + right * bestMiddle);
		}
	}
	//Runners cut the corners (e.g. from the trail towards the way around the water): the average of the points around
	std::vector<glm::vec2> smooth(route.size());
	for (std::size_t i = 0; i < route.size(); ++i)
	{
		std::size_t first = i > ROUTE_SMOOTHING ? i - ROUTE_SMOOTHING : 0, last = std::min(i + ROUTE_SMOOTHING, route.size() - 1);
		glm::vec2 sum(0.0f);
		for (std::size_t k = first; k <= last; ++k)
		{
			sum += route[k];
		}
		smooth[i] = sum / static_cast<float>(last - first + 1);
	}
	return smooth;
}

bool bibo::scene::findShoreSpot(const world::WorldGenerator& generator, const WaterPool& pool, glm::vec3& position, glm::vec3& direction)
{
	const float sea = generator.getSeaLevel();
	const world::TrackPath& path = generator.getPath();
	//The water around the pool (to measure how far a place is from it)
	std::vector<glm::vec2> water;
	const glm::vec2 center(pool.center.x, pool.center.z);
	const float reach = pool.radius + SHORE_MAX_WATER_DISTANCE + 1.0f;
	for (float x = -reach; x <= reach; x += 0.5f)
	{
		for (float z = -reach; z <= reach; z += 0.5f)
		{
			glm::vec2 point = center + glm::vec2(x, z);
			if (generator.height(point) < sea)
			{
				water.push_back(point);
			}
		}
	}
	//Where runners go past the pool (e.g. in the race): the animal must not lie in their way
	const float span = pool.radius + SHORE_MAX_WATER_DISTANCE + 10.0f;
	std::vector<glm::vec2> route = runningRoute(generator, pool.trackDistance - span, pool.trackDistance + span);
	auto distanceTo = [](const std::vector<glm::vec2>& points, glm::vec2 point) {
		float nearest = std::numeric_limits<float>::max();
		for (glm::vec2 other : points)
		{
			nearest = std::min(nearest, glm::distance(other, point));
		}
		return nearest;
	};
	//The route is a line (runners cut across from one point to the next, e.g. back onto the trail behind the water)
	auto distanceToRoute = [&route](glm::vec2 point) {
		float nearest = std::numeric_limits<float>::max();
		for (std::size_t i = 0; i + 1 < route.size(); ++i)
		{
			glm::vec2 segment = route[i + 1] - route[i];
			float along = glm::dot(point - route[i], segment) / std::max(glm::dot(segment, segment), 1e-6f);
			nearest = std::min(nearest, glm::distance(point, route[i] + segment * std::clamp(along, 0.0f, 1.0f)));
		}
		return nearest;
	};

	//On the floor of the track (or at the foot of its walls) beside the water: as far from the route as needed,
	//then close to the middle of the pool
	float bestScore = -std::numeric_limits<float>::max();
	for (const world::TrackPath::Sample& sample : path.getSamples())
	{
		if (std::abs(sample.distance - pool.trackDistance) > pool.radius + SHORE_MAX_WATER_DISTANCE)
		{
			continue;
		}
		glm::vec2 middle(sample.position.x, sample.position.z);
		glm::vec2 right(-sample.tangent.y, sample.tangent.x);
		for (float lateral = -sample.floorLeft - SHORE_BEYOND_FLOOR; lateral <= sample.floorRight + SHORE_BEYOND_FLOOR; lateral += 0.5f)
		{
			glm::vec2 point = middle + right * lateral;
			float height = generator.height(point);
			if (height < sea + SHORE_MIN_HEIGHT || height > sea + SHORE_MAX_HEIGHT)
			{
				continue;
			}
			bool flat = true;
			for (int i = 0; i < 8 && flat; ++i)
			{
				float angle = static_cast<float>(i) * 0.25f * PI;
				flat = std::abs(generator.height(point + SHORE_RADIUS * glm::vec2(std::cos(angle), std::sin(angle))) - height) < SHORE_FLATNESS;
			}
			float waterDistance = distanceTo(water, point);
			if (!flat || waterDistance < SHORE_MIN_WATER_DISTANCE || waterDistance > SHORE_MAX_WATER_DISTANCE)
			{
				continue;
			}
			float clearance = std::min(distanceToRoute(point), SHORE_ROUTE_CLEARANCE);
			float score = 10.0f * clearance - 0.3f * glm::distance(point, center);
			if (score > bestScore)
			{
				bestScore = score;
				position = glm::vec3(point.x, height, point.y);
				glm::vec2 look = center - point;
				direction = glm::normalize(glm::vec3(look.x, 0.0f, look.y));
			}
		}
	}
	return bestScore > -std::numeric_limits<float>::max();
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
	nextJump.assign(pools.size(), 0.0f);
	jumps.clear();
	droplets.clear();
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

int SalmonPools::update(float timeDelta, glm::vec3 bearCenter, glm::vec3 bearMouth, bool canCatch, const std::vector<bool>& closed)
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

	//New salmon jump while the bear is close to an open pool
	for (std::size_t p = 0; p < pools.size(); ++p)
	{
		const WaterPool& pool = pools[p];
		float distance = glm::distance(glm::vec2(bearCenter.x, bearCenter.z), glm::vec2(pool.center.x, pool.center.z));
		if (isClosed(closed, p) || distance > APPROACH_DISTANCE + pool.radius)
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
		if (!isClosed(closed, static_cast<std::size_t>(jump.pool)) && (glm::distance(position, bearMouth) < MOUTH_REACH || glm::distance(position, bearCenter) < BODY_REACH))
		{
			int pool = jump.pool;
			jumps.erase(jumps.begin() + static_cast<std::ptrdiff_t>(i));
			return pool;
		}
	}
	return -1;
}

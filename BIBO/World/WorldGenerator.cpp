#include "WorldGenerator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include "Noise.hpp"

using namespace bibo::world;

namespace
{
	//The coast is this far from the middle of the track, plus or minus its noise
	const float COAST_RADIUS = 262.0f;
	const float COAST_VARIATION = 34.0f;
	//The track terrain turns into its blurred version in this band inside of its border (the floor of the track is further inside),
	//the procedural land takes over in this band outside of it
	const float TRACK_BLEND_INSIDE = 8.0f, TRACK_BLEND_OUTSIDE = 55.0f;
	//Mountains around the track (direction from the middle in degrees, distance, height, radius)
	const struct Mountain { float angle, distance, height, radius; } MOUNTAINS[] = {
		{ 128.0f, 190.0f, 44.0f, 48.0f },
		{ 18.0f, 196.0f, 34.0f, 40.0f },
		{ 252.0f, 182.0f, 30.0f, 42.0f },
		{ 310.0f, 200.0f, 22.0f, 34.0f } };
	//Caves
	const float CAVE_LENGTHS[] = { 52.0f, 42.0f };
	const float CAVE_CEILING = 7.5f; //above the floor of the track (the bear jumps about 1.5 high, the camera needs room)
	const float CAVE_MIN_WALLS = 10.0f;
	const float CAVE_START_CLEARANCE = 45.0f; //no cave at the start and finish
	const float CAVE_SEPARATION = 60.0f; //between the ends of two caves

	float smoothstep(float edge0, float edge1, float x)
	{
		float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	//Polynomial smooth maximum: like max, but rounded where the two are close (k: width of the rounding)
	float smoothMax(float a, float b, float k)
	{
		float h = std::max(k - std::abs(a - b), 0.0f) / k;
		return std::max(a, b) + h * h * k * 0.25f;
	}

	/**
	* Directions to look for the sky (sky visibility): spread over the upper hemisphere, more of them steep (cosine weighted)
	*/
	struct Hemisphere
	{
		glm::vec3 directions[10];
		Hemisphere()
		{
			const float GOLDEN_ANGLE = 2.39996323f;
			for (int i = 0; i < 10; ++i)
			{
				float radius = std::sqrt((static_cast<float>(i) + 0.5f) / 10.0f);
				float angle = static_cast<float>(i) * GOLDEN_ANGLE;
				glm::vec2 disk = radius * glm::vec2(std::cos(angle), std::sin(angle));
				directions[i] = glm::vec3(disk.x, std::sqrt(std::max(0.0f, 1.0f - glm::dot(disk, disk))), disk.y);
			}
		}
	};
}

WorldGenerator::WorldGenerator(Description description)
	: track(std::move(description.track)), keepClear(std::move(description.keepClear)), seaLevel(description.seaLevel)
{
	center = track->getCenter();
	path = std::make_unique<TrackPath>(*track, description.start, description.startDirection, description.checkpoints);
	chooseCaves();

	float reach = COAST_RADIUS + COAST_VARIATION + 50.0f;
	bounds.minimum = glm::vec3(center.x - reach, SEA_FLOOR - 8.0f, center.y - reach);
	bounds.maximum = glm::vec3(center.x + reach, 80.0f, center.y + reach);
	computeColumnRanges();
}

WorldGenerator::~WorldGenerator() = default;

float WorldGenerator::coastRadius(glm::vec2 direction) const
{
	return COAST_RADIUS + COAST_VARIATION * noise::fbm(direction * 1.3f + glm::vec2(5.1f, 7.3f), 3, SEED + 11u)
		+ 0.3f * COAST_VARIATION * noise::fbm(direction * 5.0f + glm::vec2(-2.7f, 1.9f), 2, SEED + 12u);
}

void WorldGenerator::coast(glm::vec2 position, float& inland, float& cliff) const
{
	glm::vec2 offset = position - center;
	float distance = glm::length(offset);
	glm::vec2 direction = distance > 1e-3f ? offset / distance : glm::vec2(1.0f, 0.0f);
	inland = coastRadius(direction) - distance;
	cliff = smoothstep(-0.1f, 0.22f, noise::fbm(direction * 2.2f + glm::vec2(31.7f, -4.2f), 2, SEED + 13u));
}

float WorldGenerator::proceduralHeight(glm::vec2 position) const
{
	float inland, cliff;
	coast(position, inland, cliff);

	//Forest hills, with a few higher ridges
	float land = 11.5f + 11.0f * noise::fbm(position * 0.0042f, 4, SEED + 1u)
		+ 11.0f * noise::ridged(position * 0.0105f, 4, SEED + 2u)
		+ 2.0f * noise::fbm(position * 0.045f, 2, SEED + 3u);
	for (const Mountain& mountain : MOUNTAINS)
	{
		float angle = glm::radians(mountain.angle);
		glm::vec2 peak = center + mountain.distance * glm::vec2(std::cos(angle), std::sin(angle));
		float falloff = glm::distance(position, peak) / mountain.radius;
		if (falloff < 3.0f)
		{
			float rough = 0.75f + 0.25f * noise::ridged(position * 0.03f, 3, SEED + 8u);
			land += mountain.height * std::exp(-falloff * falloff) * rough;
		}
	}
	land = std::max(land, 4.5f + 1.5f * noise::fbm(position * 0.02f, 2, SEED + 4u)); //no lakes inland

	//Beach: a gentle slope out of the sea, then the forest
	float beach = inland < 0.0f ? seaLevel + 0.15f * inland - 0.0022f * inland * inland : seaLevel + 0.10f * inland;
	float beachLand = glm::mix(beach, std::max(beach, land), smoothstep(12.0f, 55.0f, inland));

	//Cliff: a plateau that drops steeply into the sea
	float cliffTop = std::max(land, 17.0f + 5.0f * noise::fbm(position * 0.03f, 2, SEED + 5u));
	float cliffFoot = seaLevel - 9.0f + 0.12f * std::min(inland, 0.0f);
	float cliffHeight = glm::mix(cliffFoot, cliffTop, smoothstep(-6.0f, 4.0f, inland));

	float height = glm::mix(beachLand, cliffHeight, cliff);
	float seaFloor = SEA_FLOOR + 2.0f * noise::fbm(position * 0.02f, 2, SEED + 6u);
	return smoothMax(height, seaFloor, 4.0f);
}

float WorldGenerator::height(glm::vec2 position) const
{
	//Inside: the track terrain as it is. At its border it turns into its blurred version, which goes on outside of the
	//border (the closest blurred height inside) and turns into the procedural land further out.
	float inside = track->distanceInside(position);
	if (inside >= TRACK_BLEND_INSIDE)
	{
		return track->height(position);
	}
	if (inside >= 0.0f)
	{
		return glm::mix(track->smoothHeight(position), track->height(position), smoothstep(0.0f, TRACK_BLEND_INSIDE, inside));
	}
	float continued = track->smoothHeight(track->clampInside(position));
	float weight = smoothstep(-TRACK_BLEND_OUTSIDE, 0.0f, inside);
	if (weight <= 0.0f)
	{
		return proceduralHeight(position);
	}
	//Some unevenness, so that the continued heights do not form straight ridges
	float detail = 9.0f * (noise::ridged(position * 0.0105f, 4, SEED + 2u) - 0.35f) + 3.0f * noise::fbm(position * 0.03f, 3, SEED + 7u);
	continued += detail * (1.0f - smoothstep(-10.0f, 0.0f, inside));
	return glm::mix(proceduralHeight(position), continued, weight);
}

float WorldGenerator::cliffNoise(glm::vec3 position, float inland, float cliff) const
{
	float band = 1.0f - smoothstep(5.0f, 18.0f, std::abs(inland));
	//Only on the face of the cliff, not on top of it (no floating rocks)
	float face = smoothstep(seaLevel - 15.0f, seaLevel - 7.0f, position.y) * (1.0f - smoothstep(11.0f, 16.5f, position.y));
	float weight = cliff * band * face;
	if (weight <= 0.0f)
	{
		return 0.0f;
	}
	return weight * 7.0f * noise::fbm(position * glm::vec3(0.07f, 0.11f, 0.07f), 3, SEED + 20u);
}

WorldGenerator::Column WorldGenerator::column(glm::vec2 position) const
{
	Column result;
	result.height = height(position);
	coast(position, result.inland, result.cliff);
	result.rugged = result.cliff >= 0.02f && std::abs(result.inland) <= 18.0f;
	for (const Cave& cave : caves)
	{
		if (position.x < cave.bounds.minimum.x || position.x > cave.bounds.maximum.x || position.y < cave.bounds.minimum.z || position.y > cave.bounds.maximum.z
			|| result.roofCount == 4)
		{
			continue;
		}
		TrackPath::Nearest nearest = path->nearest(position);
		if (!nearest.found || nearest.distance < cave.start - 6.0f || nearest.distance > cave.end + 6.0f)
		{
			continue;
		}
		TrackPath::Sample sample = path->at(nearest.distance);
		float halfSpan = std::max(sample.floorLeft, sample.floorRight) + 3.5f;
		float floorHeight = std::max(sample.position.y, cave.floorHeight - 1.5f);
		float lateral = std::abs(nearest.lateral);
		float arch = 1.0f - std::min(1.0f, (lateral / halfSpan) * (lateral / halfSpan));
		Column::Roof& roof = result.roofs[result.roofCount++];
		roof.ceiling = floorHeight + cave.ceilingHeight + 1.6f * arch;
		roof.top = floorHeight + cave.roofHeight - 0.7f * std::max(0.0f, lateral - halfSpan) + 1.5f * noise::fbm(position * 0.06f, 2, SEED + 31u);
		roof.side = halfSpan + 12.0f - lateral;
		roof.ends = std::min(nearest.distance - cave.start, cave.end - nearest.distance);
		roof.bottom = cave.bounds.minimum.y;
		roof.upper = cave.bounds.maximum.y;
	}
	return result;
}

float WorldGenerator::densityIn(const Column& column, glm::vec3 position) const
{
	float result = column.height - position.y;
	if (column.rugged)
	{
		result += cliffNoise(position, column.inland, column.cliff);
	}
	for (int i = 0; i < column.roofCount; ++i)
	{
		const Column::Roof& roof = column.roofs[i];
		if (position.y < roof.bottom || position.y > roof.upper)
		{
			continue;
		}
		//The noise moves the ceiling by less than 1.5 and the ends by less than 1.8: if the roof cannot come close to the
		//ground density, the smooth maximum is the ground density (exactly, so skipping the noise changes nothing)
		float upperBound = std::min(std::min(roof.top - position.y, position.y - roof.ceiling + 1.5f), std::min(roof.side, roof.ends + 1.8f));
		if (upperBound < result - 2.0f)
		{
			continue;
		}
		float ceiling = roof.ceiling + 1.0f * noise::fbm(position * 0.13f, 2, SEED + 30u);
		float ends = roof.ends + 1.2f * noise::fbm(position * 0.17f, 2, SEED + 32u);
		float density = std::min(std::min(roof.top - position.y, position.y - ceiling), std::min(roof.side, ends));
		result = smoothMax(result, density, 2.0f);
	}
	return result;
}

float WorldGenerator::density(glm::vec3 position) const
{
	return densityIn(column(glm::vec2(position.x, position.z)), position);
}

void WorldGenerator::densityColumn(float x, float z, int firstIndex, float step, int count, float* densities) const
{
	Column info = column(glm::vec2(x, z));
	for (int i = 0; i < count; ++i)
	{
		densities[i] = densityIn(info, glm::vec3(x, static_cast<float>(firstIndex + i) * step, z));
	}
}

glm::vec3 WorldGenerator::normal(glm::vec3 position) const
{
	const float E = 0.2f;
	glm::vec3 gradient(
		density(position + glm::vec3(E, 0, 0)) - density(position - glm::vec3(E, 0, 0)),
		density(position + glm::vec3(0, E, 0)) - density(position - glm::vec3(0, E, 0)),
		density(position + glm::vec3(0, 0, E)) - density(position - glm::vec3(0, 0, E)));
	float length = glm::length(gradient);
	//The density decreases towards the air
	return length > 1e-9f ? -gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

bool WorldGenerator::topSurface(glm::vec2 position, glm::vec3& surface, glm::vec3& surfaceNormal) const
{
	float y = std::max(height(position), caveRoofTop(position)) + 9.0f;
	float lowest = SEA_FLOOR - 8.0f;
	if (density(glm::vec3(position.x, y, position.y)) > 0.0f)
	{
		y += 10.0f; //very rough cliff
	}
	while (y > lowest && density(glm::vec3(position.x, y - 1.0f, position.y)) <= 0.0f)
	{
		y -= 1.0f;
	}
	if (y <= lowest)
	{
		return false;
	}
	float above = y, below = y - 1.0f;
	for (int i = 0; i < 10; ++i)
	{
		float middle = 0.5f * (above + below);
		(density(glm::vec3(position.x, middle, position.y)) > 0.0f ? below : above) = middle;
	}
	surface = glm::vec3(position.x, 0.5f * (above + below), position.y);
	surfaceNormal = normal(surface);
	return true;
}

bool WorldGenerator::surface(glm::vec2 position, glm::vec3& point, glm::vec3& surfaceNormal) const
{
	Column info = column(position);
	if (info.roofCount > 0 || info.rugged)
	{
		return topSurface(position, point, surfaceNormal);
	}
	//density = height - y: the normal is (-dh/dx, 1, -dh/dz) normalized
	const float E = 0.25f;
	float dx = height(position + glm::vec2(E, 0.0f)) - height(position - glm::vec2(E, 0.0f));
	float dz = height(position + glm::vec2(0.0f, E)) - height(position - glm::vec2(0.0f, E));
	point = glm::vec3(position.x, info.height, position.y);
	surfaceNormal = glm::normalize(glm::vec3(-dx / (2.0f * E), 1.0f, -dz / (2.0f * E)));
	return true;
}

float WorldGenerator::snowCover(glm::vec3 position, glm::vec3 surfaceNormal, float visibility, const SurfaceMaterial& surface) const
{
	if (surface.cave > 0.5f)
	{
		return 0.0f;
	}
	glm::vec2 ground(position.x, position.z);
	float patches = noise::fbm(ground * 0.06f, 4, SEED + 50u);
	float altitude = smoothstep(18.0f, 45.0f, position.y);
	float cover = smoothstep(0.12f, 0.32f, patches + 0.8f * altitude - 0.08f);
	cover *= smoothstep(0.68f, 0.9f, surfaceNormal.y);
	cover *= smoothstep(0.2f, 0.6f, visibility);
	cover *= 1.0f - 0.85f * surface.trail;
	cover *= 1.0f - surface.sand;
	return std::clamp(cover, 0.0f, 1.0f);
}

float WorldGenerator::caveRoofTop(glm::vec2 position) const
{
	for (const Cave& cave : caves)
	{
		if (position.x < cave.bounds.minimum.x || position.x > cave.bounds.maximum.x || position.y < cave.bounds.minimum.z || position.y > cave.bounds.maximum.z)
		{
			continue;
		}
		TrackPath::Nearest nearest = path->nearest(position);
		if (!nearest.found || nearest.distance < cave.start - 1.0f || nearest.distance > cave.end + 1.0f)
		{
			continue;
		}
		TrackPath::Sample sample = path->at(nearest.distance);
		float halfSpan = std::max(sample.floorLeft, sample.floorRight) + 3.5f;
		if (std::abs(nearest.lateral) < halfSpan + 1.0f)
		{
			return std::max(sample.position.y, cave.floorHeight - 1.5f) + cave.roofHeight - 2.0f;
		}
	}
	return -1e6f;
}

bool WorldGenerator::isInCave(glm::vec3 position) const
{
	for (const Cave& cave : caves)
	{
		if (!cave.bounds.contains(position))
		{
			continue;
		}
		TrackPath::Nearest nearest = path->nearest(glm::vec2(position.x, position.z));
		if (!nearest.found || nearest.distance < cave.start || nearest.distance > cave.end)
		{
			continue;
		}
		TrackPath::Sample sample = path->at(nearest.distance);
		float halfSpan = std::max(sample.floorLeft, sample.floorRight) + 3.5f;
		float floorHeight = std::max(sample.position.y, cave.floorHeight - 1.5f);
		if (std::abs(nearest.lateral) < halfSpan + 3.0f && position.y > floorHeight - 3.0f && position.y < floorHeight + cave.ceilingHeight + 3.0f)
		{
			return true;
		}
	}
	return false;
}

SurfaceMaterial WorldGenerator::material(glm::vec3 position, glm::vec3 surfaceNormal) const
{
	SurfaceMaterial result;
	glm::vec2 ground(position.x, position.z);
	float inland, cliff;
	coast(ground, inland, cliff);
	float beach = 1.0f - smoothstep(10.0f, 24.0f, inland);
	result.sand = beach * (1.0f - smoothstep(0.35f, 0.75f, cliff)) * (1.0f - smoothstep(seaLevel + 3.0f, seaLevel + 5.5f, position.y));
	if (position.y < seaLevel - 0.6f)
	{
		result.sand = std::max(result.sand, 1.0f - 0.7f * cliff); //sea floor
	}
	result.cliff = cliff * (1.0f - smoothstep(16.0f, 32.0f, inland));

	TrackPath::Nearest nearest = path->nearest(ground);
	if (nearest.found && position.y < nearest.floorHeight + 2.5f)
	{
		float edge = TrackPath::TRAIL_HALF_WIDTH + 0.5f * noise::fbm(glm::vec2(nearest.distance * 0.15f, 1.7f), 2, SEED + 40u);
		result.trail = 1.0f - smoothstep(edge - 0.8f, edge + 0.7f, std::abs(nearest.trailLateral));
	}
	if (isInCave(position) || position.y < caveRoofTop(ground) - 1.0f)
	{
		result.cave = 1.0f;
	}
	return result;
}

float WorldGenerator::skyVisibility(glm::vec3 position, glm::vec3 surfaceNormal) const
{
	static const Hemisphere hemisphere;
	const float RADII[] = { 1.5f, 4.0f, 10.0f };
	const float WEIGHTS[] = { 1.0f, 0.8f, 0.6f };
	//Basis around the normal
	glm::vec3 up = surfaceNormal;
	glm::vec3 tangent = glm::normalize(std::abs(up.y) < 0.99f ? glm::cross(up, glm::vec3(0.0f, 1.0f, 0.0f)) : glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f)));
	glm::vec3 bitangent = glm::cross(up, tangent);
	glm::vec3 origin = position + 0.35f * surfaceNormal;
	float occluded = 0.0f, total = 0.0f;
	for (const glm::vec3& local : hemisphere.directions)
	{
		glm::vec3 direction = local.x * tangent + local.y * up + local.z * bitangent;
		for (int r = 0; r < 3; ++r)
		{
			total += WEIGHTS[r];
			if (density(origin + direction * RADII[r]) > 0.0f)
			{
				occluded += WEIGHTS[r];
				break; //the farther samples are hidden behind this one
			}
		}
	}
	//Every direction that is blocked close by also blocks the samples behind it
	return std::clamp(1.0f - occluded / total * 2.2f, 0.0f, 1.0f);
}

void WorldGenerator::chooseCaves()
{
	const std::vector<TrackPath::Sample>& samples = path->getSamples();
	if (samples.size() < 10)
	{
		return;
	}
	const float length = path->getLength();
	auto blocked = [&](const TrackPath::Sample& sample) {
		//Something of the track that is higher than the floor would be inside of the roof
		for (const KeepClear& object : keepClear)
		{
			float horizontal = glm::distance(glm::vec2(object.center.x, object.center.z), glm::vec2(sample.position.x, sample.position.z));
			//Rocks on the floor fit under a roof, signs higher up would be inside of it
			bool aboveFloor = object.center.y + object.radius > sample.position.y + 4.5f;
			if (aboveFloor && horizontal < std::max(sample.floorLeft, sample.floorRight) + 16.0f + object.radius)
			{
				return true;
			}
		}
		return std::min(sample.wallLeft, sample.wallRight) < CAVE_MIN_WALLS;
	};
	std::vector<bool> usable(samples.size());
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		usable[i] = samples[i].distance > CAVE_START_CLEARANCE && samples[i].distance < length - CAVE_START_CLEARANCE && !blocked(samples[i]);
	}

	for (float caveLength : CAVE_LENGTHS)
	{
		int window = static_cast<int>(caveLength / TrackPath::SAMPLE_SPACING);
		float bestScore = -1.0f;
		int bestStart = -1;
		for (int first = 0; first + window < static_cast<int>(samples.size()); ++first)
		{
			float score = std::numeric_limits<float>::max();
			for (int i = first; i <= first + window && score > bestScore; ++i)
			{
				if (!usable[i])
				{
					score = -1.0f;
					break;
				}
				score = std::min(score, std::min(samples[i].wallLeft, samples[i].wallRight));
			}
			float middle = samples[first + window / 2].distance;
			for (const Cave& other : caves)
			{
				//Apart from the other caves (also around the end of the lap)
				float apart = std::abs(middle - 0.5f * (other.start + other.end));
				if (std::min(apart, length - apart) < 0.5f * (other.end - other.start + caveLength) + CAVE_SEPARATION)
				{
					score = -1.0f;
				}
			}
			if (score > bestScore)
			{
				bestScore = score;
				bestStart = first;
			}
		}
		if (bestStart < 0)
		{
			continue;
		}
		Cave cave;
		cave.start = samples[bestStart].distance;
		cave.end = samples[bestStart + window].distance;
		float floorSum = 0.0f, wallSum = 0.0f;
		glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
		for (int i = bestStart - 8; i <= bestStart + window + 8; ++i)
		{
			const TrackPath::Sample& sample = samples[(i + samples.size()) % samples.size()];
			float reach = std::max(sample.floorLeft, sample.floorRight) + 3.5f + 14.0f;
			low = glm::min(low, sample.position - glm::vec3(reach, 0.0f, reach));
			high = glm::max(high, sample.position + glm::vec3(reach, 0.0f, reach));
			if (i >= bestStart && i <= bestStart + window)
			{
				floorSum += sample.position.y;
				wallSum += std::min(sample.wallLeft, sample.wallRight);
			}
		}
		cave.floorHeight = floorSum / static_cast<float>(window + 1);
		cave.ceilingHeight = CAVE_CEILING;
		cave.roofHeight = std::clamp(wallSum / static_cast<float>(window + 1) + 0.5f, 14.0f, 20.0f);
		cave.bounds.minimum = glm::vec3(low.x, low.y - 4.0f, low.z);
		cave.bounds.maximum = glm::vec3(high.x, high.y + cave.roofHeight + 6.0f, high.z);
		caves.push_back(cave);
	}
	std::sort(caves.begin(), caves.end(), [](const Cave& a, const Cave& b) { return a.start < b.start; });
}

void WorldGenerator::computeColumnRanges()
{
	glm::vec2 low(bounds.minimum.x, bounds.minimum.z);
	rangeColumns = static_cast<int>(std::ceil((bounds.maximum.x - bounds.minimum.x) / RANGE_CELL));
	rangeRows = static_cast<int>(std::ceil((bounds.maximum.z - bounds.minimum.z) / RANGE_CELL));
	columnRanges.assign(static_cast<std::size_t>(rangeColumns) * rangeRows, glm::vec2(0.0f));
	for (int j = 0; j < rangeRows; ++j)
	{
		for (int i = 0; i < rangeColumns; ++i)
		{
			float lowest = std::numeric_limits<float>::max(), highest = std::numeric_limits<float>::lowest();
			float steepest = 0.0f;
			const int STEPS = 8;
			for (int sj = 0; sj <= STEPS; ++sj)
			{
				for (int si = 0; si <= STEPS; ++si)
				{
					glm::vec2 position = low + RANGE_CELL * glm::vec2(i + si / static_cast<float>(STEPS), j + sj / static_cast<float>(STEPS));
					float h = height(position);
					lowest = std::min(lowest, h);
					highest = std::max(highest, h);
					float inland, cliff;
					coast(position, inland, cliff);
					if (cliff > 0.02f && std::abs(inland) < 20.0f)
					{
						steepest = 8.0f; //the noise of the cliff moves the surface
					}
					highest = std::max(highest, caveRoofTop(position) + 6.0f);
				}
			}
			//Between the samples the height changes, the margin covers the slope over a sample distance
			float margin = 3.0f + steepest + 0.25f * (highest - lowest);
			columnRanges[static_cast<std::size_t>(j) * rangeColumns + i] = glm::vec2(lowest - margin, highest + margin);
		}
	}
}

bool WorldGenerator::mayContainSurface(const Box& box) const
{
	int i0 = std::max(0, static_cast<int>(std::floor((box.minimum.x - bounds.minimum.x) / RANGE_CELL)));
	int i1 = std::min(rangeColumns - 1, static_cast<int>(std::floor((box.maximum.x - bounds.minimum.x) / RANGE_CELL)));
	int j0 = std::max(0, static_cast<int>(std::floor((box.minimum.z - bounds.minimum.z) / RANGE_CELL)));
	int j1 = std::min(rangeRows - 1, static_cast<int>(std::floor((box.maximum.z - bounds.minimum.z) / RANGE_CELL)));
	if (i0 > i1 || j0 > j1)
	{
		return false;
	}
	float lowest = std::numeric_limits<float>::max(), highest = std::numeric_limits<float>::lowest();
	for (int j = j0; j <= j1; ++j)
	{
		for (int i = i0; i <= i1; ++i)
		{
			const glm::vec2& range = columnRanges[static_cast<std::size_t>(j) * rangeColumns + i];
			lowest = std::min(lowest, range.x);
			highest = std::max(highest, range.y);
		}
	}
	return box.maximum.y >= lowest && box.minimum.y <= highest;
}

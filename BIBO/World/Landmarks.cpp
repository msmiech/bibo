#include "Landmarks.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>

#include "Noise.hpp"
#include "WorldGenerator.hpp"

using namespace bibo::world;

namespace
{
	const float PI = 3.14159265f;
	//The paths are searched on a grid with cells of this size
	const float ROUTE_CELL = 2.0f;
	//A cell that a path must not go through (sea, caves, the objects of the track)
	const float FORBIDDEN = 1e4f;
	//Further from the middle of a path the ground is not changed (the widest cut with its slopes)
	const float REACH = 12.0f;
	//Parts of a path that are further apart along it than this are different legs (e.g. the two sides of a hairpin bend)
	const int LEG_LENGTH = 30;
	const int PATH_STRIDE = 1000000; //index of a point of a path: path * PATH_STRIDE + point
	//No path joins the track close to the start and finish
	const float START_CLEARANCE = 30.0f;
	//Paths leave the track at least this far apart. A path branches off from another one if that is much cheaper than the
	//way from the track.
	const float TRAILHEAD_SPACING = 25.0f, BRANCH_RATIO = 0.45f;
	//Beside the floor of the track (on the side of its canyon) a step costs this much more per unit
	const float CANYON_BAND = 6.0f, CANYON_PENALTY = 10.0f;
	//Along a path the search prefers to climb at most this much per unit (the paths may go up to MAX_GRADE)
	const float PREFERRED_GRADE = 0.3f;
	//The ground around the summit is blurred over this distance, the crags fade out within this radius
	const float SOFTENING_BLUR = 3.0f, SUMMIT_SOFTENING = 34.0f;
	//Ground steeper than this to the cells around a cell (up to STEEP_CELLS away) makes a path through it more expensive
	const float STEEP_SLOPE = 0.7f;
	const int STEEP_CELLS = 2;

	float smoothstep(float edge0, float edge1, float x)
	{
		float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float angleOf(glm::vec2 offset)
	{
		return std::atan2(offset.y, offset.x);
	}

	float angleBetween(float a, float b)
	{
		float difference = std::fmod(std::abs(a - b), 2.0f * PI);
		return std::min(difference, 2.0f * PI - difference);
	}

	glm::vec2 ground(glm::vec3 position)
	{
		return glm::vec2(position.x, position.z);
	}

	std::vector<glm::vec2> resampleLine(const std::vector<glm::vec2>& line, float spacing)
	{
		std::vector<float> distances(line.size(), 0.0f);
		for (std::size_t i = 1; i < line.size(); ++i)
		{
			distances[i] = distances[i - 1] + glm::distance(line[i - 1], line[i]);
		}
		float total = distances.back();
		int count = std::max(1, static_cast<int>(std::round(total / spacing)));
		std::vector<glm::vec2> result;
		std::size_t segment = 0;
		for (int k = 0; k <= count; ++k)
		{
			float d = total * static_cast<float>(k) / static_cast<float>(count);
			while (segment + 2 < line.size() && distances[segment + 1] < d)
			{
				++segment;
			}
			float length = std::max(distances[segment + 1] - distances[segment], 1e-6f);
			result.push_back(glm::mix(line[segment], line[segment + 1], std::clamp((d - distances[segment]) / length, 0.0f, 1.0f)));
		}
		return result;
	}

	/**
	* Moving average of the values (radius in points), the first and the last value stay
	*/
	void smoothValues(std::vector<float>& values, int radius, int passes)
	{
		std::vector<float> next(values.size());
		for (int pass = 0; pass < passes; ++pass)
		{
			for (std::size_t i = 0; i < values.size(); ++i)
			{
				if (i == 0 || i + 1 == values.size())
				{
					next[i] = values[i];
					continue;
				}
				int r = std::min({ radius, static_cast<int>(i), static_cast<int>(values.size() - 1 - i) });
				float sum = 0.0f;
				for (int k = -r; k <= r; ++k)
				{
					sum += values[i + k];
				}
				next[i] = sum / static_cast<float>(2 * r + 1);
			}
			values.swap(next);
		}
	}
}

/**
* The island for the search of the paths: the height of the ground, what a step through a cell costs extra (per unit) and the
* network the paths join (the floor of the race track and the paths that are already there)
*/
struct Landmarks::Grid
{
	enum Network : std::uint8_t { NONE = 0, TRACK = 1, PATH = 2 };
	glm::vec2 origin = glm::vec2(0.0f);
	int columns = 0, rows = 0;
	std::vector<float> height, penalty;
	std::vector<std::uint8_t> network;

	glm::vec2 position(int cell) const
	{
		return origin + ROUTE_CELL * glm::vec2(static_cast<float>(cell % columns), static_cast<float>(cell / columns));
	}

	int cellOf(glm::vec2 point) const
	{
		int i = std::clamp(static_cast<int>(std::round((point.x - origin.x) / ROUTE_CELL)), 0, columns - 1);
		int j = std::clamp(static_cast<int>(std::round((point.y - origin.y) / ROUTE_CELL)), 0, rows - 1);
		return j * columns + i;
	}

	/**
	* Cheapest way from the cell to the network (Dijkstra, 16 neighbors so that the paths can go in more directions).
	* A step costs more the steeper it goes up or down: the paths wind up the hills.
	* @param joins The parts of the network that count (TRACK, PATH or both)
	* @param cost Gets the cost of the way
	* @return The cells from the network to the cell, empty if there is no way
	*/
	std::vector<int> route(int from, std::uint8_t joins, float& cost) const
	{
		static const int DI[16] = { 1, -1, 0, 0, 1, 1, -1, -1, 2, 2, -2, -2, 1, -1, 1, -1 };
		static const int DJ[16] = { 0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 2, 2, -2, -2 };
		std::vector<float> best(height.size(), std::numeric_limits<float>::max());
		std::vector<int> previous(height.size(), -1);
		using Entry = std::pair<float, int>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
		best[from] = 0.0f;
		open.push({ 0.0f, from });
		int reached = -1;
		while (!open.empty())
		{
			auto [total, cell] = open.top();
			open.pop();
			if (total > best[cell])
			{
				continue;
			}
			if ((network[cell] & joins) != 0)
			{
				reached = cell;
				cost = total;
				break;
			}
			int i = cell % columns, j = cell / columns;
			for (int n = 0; n < 16; ++n)
			{
				int ni = i + DI[n], nj = j + DJ[n];
				if (ni < 0 || nj < 0 || ni >= columns || nj >= rows)
				{
					continue;
				}
				int neighbor = nj * columns + ni;
				if (penalty[neighbor] >= FORBIDDEN)
				{
					continue;
				}
				float length = ROUTE_CELL * std::sqrt(static_cast<float>(DI[n] * DI[n] + DJ[n] * DJ[n]));
				float grade = std::abs(height[neighbor] - height[cell]) / length;
				//Steeper than the paths may go it is very expensive: a longer way in bends up the hill is cheaper
				float factor = 1.0f + 30.0f * grade * grade + (grade > PREFERRED_GRADE ? 400.0f * (grade - PREFERRED_GRADE) : 0.0f);
				float step = length * (factor + 0.5f * (penalty[cell] + penalty[neighbor]));
				if (total + step < best[neighbor])
				{
					best[neighbor] = total + step;
					previous[neighbor] = cell;
					open.push({ best[neighbor], neighbor });
				}
			}
		}
		std::vector<int> cells;
		for (int cell = reached; cell != -1; cell = previous[cell])
		{
			cells.push_back(cell);
		}
		return cells; //from the network (reached) back to the start
	}
};

Landmarks::Landmarks(const WorldGenerator& world)
{
	choosePoints(world);
	findPaths(world);
	rasterize(world);
	for (PointOfInterest& point : points)
	{
		glm::vec2 at = ground(point.position);
		point.position.y = height(at, world.naturalHeight(at));
	}
}

void Landmarks::soften(const WorldGenerator& world, glm::vec2 center, float radius)
{
	//The natural ground on a grid (with a margin for the blur), blurred with a box filter a few times (about a Gaussian)
	Softened area;
	area.center = center;
	area.radius = radius;
	const int blur = static_cast<int>(SOFTENING_BLUR / RASTER_CELL);
	const float half = radius + static_cast<float>(blur) * RASTER_CELL * 3.0f;
	area.origin = center - glm::vec2(half);
	area.columns = static_cast<int>(2.0f * half / RASTER_CELL) + 1;
	const int n = area.columns;
	area.heights.resize(static_cast<std::size_t>(n) * n);
	for (int j = 0; j < n; ++j)
	{
		for (int i = 0; i < n; ++i)
		{
			area.heights[static_cast<std::size_t>(j) * n + i] = world.naturalHeight(area.origin + RASTER_CELL * glm::vec2(static_cast<float>(i), static_cast<float>(j)));
		}
	}
	std::vector<float> next(area.heights.size());
	for (int pass = 0; pass < 3; ++pass)
	{
		for (int axis = 0; axis < 2; ++axis)
		{
			for (int j = 0; j < n; ++j)
			{
				for (int i = 0; i < n; ++i)
				{
					float sum = 0.0f;
					for (int k = -blur; k <= blur; ++k)
					{
						int si = axis == 0 ? std::clamp(i + k, 0, n - 1) : i, sj = axis == 1 ? std::clamp(j + k, 0, n - 1) : j;
						sum += area.heights[static_cast<std::size_t>(sj) * n + si];
					}
					next[static_cast<std::size_t>(j) * n + i] = sum / static_cast<float>(2 * blur + 1);
				}
			}
			area.heights.swap(next);
		}
	}
	softened.push_back(std::move(area));
}

float Landmarks::applySoftening(glm::vec2 position, float natural) const
{
	for (const Softened& area : softened)
	{
		float distance = glm::distance(position, area.center);
		if (distance >= area.radius)
		{
			continue;
		}
		glm::vec2 cell = (position - area.origin) / RASTER_CELL;
		int i = std::clamp(static_cast<int>(cell.x), 0, area.columns - 2), j = std::clamp(static_cast<int>(cell.y), 0, area.columns - 2);
		float fx = cell.x - static_cast<float>(i), fz = cell.y - static_cast<float>(j);
		auto at = [&](int di, int dj) { return area.heights[static_cast<std::size_t>(j + dj) * area.columns + i + di]; };
		float smooth = glm::mix(glm::mix(at(0, 0), at(1, 0), fx), glm::mix(at(0, 1), at(1, 1), fx), fz);
		natural = glm::mix(natural, smooth, 1.0f - smoothstep(0.55f * area.radius, area.radius, distance));
	}
	return natural;
}

float Landmarks::naturalGround(const WorldGenerator& world, glm::vec2 position) const
{
	return applySoftening(position, world.naturalHeight(position));
}

float Landmarks::applyLeveling(glm::vec2 position, float height) const
{
	for (const PointOfInterest& point : points)
	{
		if (!point.level)
		{
			continue;
		}
		glm::vec2 offset = position - ground(point.position);
		float outer = 1.3f * point.radius;
		if (glm::dot(offset, offset) < outer * outer)
		{
			height = glm::mix(height, point.position.y, 1.0f - smoothstep(0.55f * point.radius, outer, glm::length(offset)));
		}
	}
	return height;
}

float Landmarks::leveled(const WorldGenerator& world, glm::vec2 position) const
{
	return applyLeveling(position, naturalGround(world, position));
}

void Landmarks::choosePoints(const WorldGenerator& world)
{
	const glm::vec2 center = world.getCenter();
	const float sea = world.getSeaLevel();
	std::vector<float> angles; //of the chosen points, seen from the middle of the island
	auto add = [&](PointOfInterest point) {
		glm::vec2 at = ground(point.position);
		point.position.y = naturalGround(world, at);
		points.push_back(point);
		angles.push_back(angleOf(at - center));
	};
	auto separation = [&](float angle) {
		float smallest = 2.0f * PI;
		for (float other : angles)
		{
			smallest = std::min(smallest, angleBetween(angle, other));
		}
		return smallest;
	};

	//1. A lookout on the highest summit
	glm::vec2 summit = center;
	float highest = std::numeric_limits<float>::lowest();
	for (glm::vec2 peak : world.getMountainPeaks())
	{
		for (float x = -24.0f; x <= 24.0f; x += 2.0f)
		{
			for (float z = -24.0f; z <= 24.0f; z += 2.0f)
			{
				float h = world.naturalHeight(peak + glm::vec2(x, z));
				if (h > highest)
				{
					highest = h;
					summit = peak + glm::vec2(x, z);
				}
			}
		}
	}
	//The crags around it are worn down a bit (the path winds up between rocks, not along walls), its top is a small plateau
	soften(world, summit, SUMMIT_SOFTENING);
	PointOfInterest lookout;
	lookout.kind = PointOfInterest::Kind::SUMMIT;
	lookout.position = glm::vec3(summit.x, 0.0f, summit.y);
	lookout.facing = glm::normalize(summit - center);
	lookout.radius = 7.0f;
	lookout.level = true;
	add(lookout);

	//2. The coast in stretches of directions with beaches or cliffs
	const int DIRECTIONS = 720;
	std::vector<float> cliffs(DIRECTIONS), coastDistance(DIRECTIONS);
	for (int i = 0; i < DIRECTIONS; ++i)
	{
		float angle = 2.0f * PI * static_cast<float>(i) / DIRECTIONS;
		glm::vec2 direction(std::cos(angle), std::sin(angle));
		float inland, cliff;
		world.coast(center + direction * 200.0f, inland, cliff);
		cliffs[i] = cliff;
		coastDistance[i] = 200.0f + inland;
	}
	struct Stretch
	{
		int middle;
		float span;
	};
	auto stretches = [&](const std::function<bool(int)>& belongs) {
		std::vector<Stretch> result;
		int start = 0;
		while (start < DIRECTIONS && belongs(start))
		{
			++start;
		}
		if (start == DIRECTIONS)
		{
			return result; //all around: no stretch
		}
		int runStart = -1;
		for (int k = 1; k <= DIRECTIONS; ++k)
		{
			bool inside = k < DIRECTIONS && belongs((start + k) % DIRECTIONS);
			if (inside && runStart < 0)
			{
				runStart = k;
			}
			else if (!inside && runStart >= 0)
			{
				result.push_back({ (start + (runStart + k - 1) / 2) % DIRECTIONS, 2.0f * PI * static_cast<float>(k - runStart) / DIRECTIONS });
				runStart = -1;
			}
		}
		std::sort(result.begin(), result.end(), [](const Stretch& a, const Stretch& b) { return a.span > b.span; });
		return result;
	};
	auto directionOf = [&](int index) {
		float angle = 2.0f * PI * static_cast<float>(index) / DIRECTIONS;
		return glm::vec2(std::cos(angle), std::sin(angle));
	};

	//Beaches on different sides of the island, on the dry sand above the water line
	std::vector<float> beachAngles;
	for (const Stretch& stretch : stretches([&](int i) { return cliffs[i] < 0.12f; }))
	{
		float angle = 2.0f * PI * static_cast<float>(stretch.middle) / DIRECTIONS;
		bool apart = true;
		for (float other : beachAngles)
		{
			apart = apart && angleBetween(angle, other) > glm::radians(55.0f);
		}
		if (stretch.span < glm::radians(10.0f) || !apart || beachAngles.size() >= 4)
		{
			continue;
		}
		glm::vec2 direction = directionOf(stretch.middle);
		float distance = coastDistance[stretch.middle] - 9.0f;
		for (int step = 0; step < 15 && naturalGround(world, center + direction * distance) < sea + 0.8f; ++step)
		{
			distance -= 1.0f;
		}
		PointOfInterest beach;
		beach.kind = PointOfInterest::Kind::BEACH;
		glm::vec2 at = center + direction * distance;
		beach.position = glm::vec3(at.x, 0.0f, at.y);
		beach.facing = direction;
		beach.radius = 8.0f;
		add(beach);
		beachAngles.push_back(angle);
	}

	//A view from the top of the cliffs, away from the other places
	float bestSeparation = -1.0f;
	PointOfInterest view;
	for (const Stretch& stretch : stretches([&](int i) { return cliffs[i] > 0.85f; }))
	{
		float angle = 2.0f * PI * static_cast<float>(stretch.middle) / DIRECTIONS;
		if (stretch.span < glm::radians(10.0f) || separation(angle) <= bestSeparation)
		{
			continue;
		}
		glm::vec2 direction = directionOf(stretch.middle);
		float distance = coastDistance[stretch.middle] - 13.0f;
		for (int step = 0; step < 10 && naturalGround(world, center + direction * distance) < sea + 10.0f; ++step)
		{
			distance -= 2.0f;
		}
		bestSeparation = separation(angle);
		glm::vec2 at = center + direction * distance;
		view.kind = PointOfInterest::Kind::CLIFF_VIEW;
		view.position = glm::vec3(at.x, 0.0f, at.y);
		view.facing = direction;
		view.radius = 6.0f;
	}
	if (bestSeparation >= 0.0f)
	{
		add(view);
	}

	//3. A camp on flat ground in the forest and a circle of standing stones on a hill, between the track and the coast
	const TrackPath& track = world.getPath();
	auto roughness = [&](glm::vec2 at, float radius) {
		float middle = naturalGround(world, at), most = 0.0f;
		for (int k = 0; k < 8; ++k)
		{
			float angle = 2.0f * PI * static_cast<float>(k) / 8.0f;
			for (float r : { 0.5f * radius, radius })
			{
				most = std::max(most, std::abs(naturalGround(world, at + r * glm::vec2(std::cos(angle), std::sin(angle))) - middle));
			}
		}
		return most;
	};
	auto inForest = [&](glm::vec2 at) {
		float inland, cliff;
		world.coast(at, inland, cliff);
		float h = naturalGround(world, at);
		TrackPath::Nearest nearest = track.nearest(at);
		return inland > 45.0f && h > sea + 5.0f && h < sea + 26.0f && (!nearest.found || std::abs(nearest.lateral) > nearest.floorHalfWidth + 25.0f)
			&& world.caveRoofTop(at) < -1000.0f;
	};
	for (PointOfInterest::Kind kind : { PointOfInterest::Kind::CAMP, PointOfInterest::Kind::STANDING_STONES })
	{
		float bestScore = -1.0f;
		glm::vec2 best = center;
		for (float x = -220.0f; x <= 220.0f; x += 6.0f)
		{
			for (float z = -220.0f; z <= 220.0f; z += 6.0f)
			{
				glm::vec2 at = center + glm::vec2(x, z);
				float distance = glm::length(at - center);
				if (distance < 110.0f || distance > 210.0f || !inForest(at))
				{
					continue;
				}
				float score = separation(angleOf(at - center));
				if (kind == PointOfInterest::Kind::CAMP)
				{
					float rough = roughness(at, 9.0f);
					if (rough > 2.0f)
					{
						continue;
					}
					score *= 1.0f - 0.25f * rough;
				}
				else
				{
					//The top of a hill: higher than the ground around it
					float h = naturalGround(world, at);
					bool top = roughness(at, 7.0f) < 2.5f;
					for (int k = 0; k < 8 && top; ++k)
					{
						float angle = 2.0f * PI * static_cast<float>(k) / 8.0f;
						top = naturalGround(world, at + 14.0f * glm::vec2(std::cos(angle), std::sin(angle))) < h - 0.8f;
					}
					if (!top)
					{
						continue;
					}
				}
				if (score > bestScore)
				{
					bestScore = score;
					best = at;
				}
			}
		}
		if (bestScore < 0.0f)
		{
			continue;
		}
		PointOfInterest place;
		place.kind = kind;
		place.position = glm::vec3(best.x, 0.0f, best.y);
		place.facing = glm::normalize(best - center);
		place.radius = kind == PointOfInterest::Kind::CAMP ? 11.0f : 10.0f;
		place.level = true;
		add(place);
	}
}

void Landmarks::findPaths(const WorldGenerator& world)
{
	const float sea = world.getSeaLevel();
	const TrackPath& track = world.getPath();
	Box bounds = world.getBounds();
	Grid grid;
	grid.origin = glm::vec2(bounds.minimum.x, bounds.minimum.z);
	grid.columns = static_cast<int>((bounds.maximum.x - bounds.minimum.x) / ROUTE_CELL) + 1;
	grid.rows = static_cast<int>((bounds.maximum.z - bounds.minimum.z) / ROUTE_CELL) + 1;
	std::size_t cellCount = static_cast<std::size_t>(grid.columns) * grid.rows;
	grid.height.resize(cellCount);
	grid.penalty.assign(cellCount, 0.0f);
	grid.network.assign(cellCount, Grid::NONE);
	std::vector<float> naturals(cellCount);
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		naturals[c] = naturalGround(world, grid.position(static_cast<int>(c)));
	}
	for (std::size_t c = 0; c < cellCount; ++c)
	{
		glm::vec2 at = grid.position(static_cast<int>(c));
		float natural = naturals[c];
		grid.height[c] = applyLeveling(at, natural);
		float inland, cliff;
		world.coast(at, inland, cliff);
		if (natural < sea + 0.6f)
		{
			grid.penalty[c] = FORBIDDEN; //the sea and the pools
			continue;
		}
		if (cliff > 0.3f && inland < 22.0f)
		{
			grid.penalty[c] = 30.0f * cliff; //the edge of the cliffs
		}
		//Steep ground around the cell (also across the way): a path there would be a ledge under a crag or above a drop.
		//To the neighbors in 8 directions, one and two cells away.
		float steepest = 0.0f;
		const int i = static_cast<int>(c) % grid.columns, j = static_cast<int>(c) / grid.columns;
		for (int di = -1; di <= 1; ++di)
		{
			for (int dj = -1; dj <= 1; ++dj)
			{
				for (int reach = 1; reach <= STEEP_CELLS && (di != 0 || dj != 0); ++reach)
				{
					int ni = i + di * reach, nj = j + dj * reach;
					if (ni >= 0 && nj >= 0 && ni < grid.columns && nj < grid.rows)
					{
						float distance = ROUTE_CELL * static_cast<float>(reach) * std::sqrt(static_cast<float>(di * di + dj * dj));
						steepest = std::max(steepest, std::abs(naturals[static_cast<std::size_t>(nj) * grid.columns + ni] - natural) / distance);
					}
				}
			}
		}
		if (steepest > STEEP_SLOPE)
		{
			float excess = steepest - STEEP_SLOPE;
			grid.penalty[c] += 40.0f * excess * excess + 8.0f * excess;
		}
		for (const KeepClear& object : world.getKeepClear())
		{
			if (glm::distance(at, ground(object.center)) < object.radius + 4.0f)
			{
				grid.penalty[c] = FORBIDDEN;
			}
		}
		if (world.caveRoofTop(at) > -1000.0f)
		{
			grid.penalty[c] = FORBIDDEN;
		}
		TrackPath::Nearest nearest = track.nearest(at);
		if (grid.penalty[c] < FORBIDDEN && steepest < STEEP_SLOPE && nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth - 1.0f && natural < nearest.floorHeight + 1.0f
			&& nearest.distance > START_CLEARANCE && nearest.distance < track.getLength() - START_CLEARANCE)
		{
			grid.network[c] = Grid::TRACK;
		}
		else if (grid.penalty[c] < FORBIDDEN && nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth + CANYON_BAND)
		{
			//A path climbs out of the canyon of the track, it does not go along its side next to the floor
			grid.penalty[c] += CANYON_PENALTY;
		}
	}

	//No path joins the track right at a pool on it
	const int SHORE_CELLS = 4;
	std::vector<std::uint8_t> network = grid.network;
	for (int j = 0; j < grid.rows; ++j)
	{
		for (int i = 0; i < grid.columns; ++i)
		{
			std::size_t c = static_cast<std::size_t>(j) * grid.columns + i;
			for (int dj = -SHORE_CELLS; dj <= SHORE_CELLS && network[c] == Grid::TRACK; ++dj)
			{
				for (int di = -SHORE_CELLS; di <= SHORE_CELLS; ++di)
				{
					int ni = std::clamp(i + di, 0, grid.columns - 1), nj = std::clamp(j + dj, 0, grid.rows - 1);
					if (grid.height[static_cast<std::size_t>(nj) * grid.columns + ni] < sea + 0.6f)
					{
						grid.network[c] = Grid::NONE;
					}
				}
			}
		}
	}

	//The closest places first, the paths to the far ones can branch off from them
	std::vector<int> order(points.size());
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		order[i] = static_cast<int>(i);
	}
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return glm::distance(ground(points[a].position), world.getCenter()) < glm::distance(ground(points[b].position), world.getCenter());
	});
	for (int index : order)
	{
		const PointOfInterest& point = points[index];
		glm::vec2 target = ground(point.position);
		//From the track, unless a path that is already there is much closer (then it branches off from that one)
		float trackCost = std::numeric_limits<float>::max(), pathCost = std::numeric_limits<float>::max();
		std::vector<int> cells = grid.route(grid.cellOf(target), Grid::TRACK, trackCost);
		std::vector<int> branch = paths.empty() ? std::vector<int>() : grid.route(grid.cellOf(target), Grid::PATH, pathCost);
		if (branch.size() >= 2 && (cells.size() < 2 || pathCost < BRANCH_RATIO * trackCost))
		{
			cells.swap(branch);
		}
		if (cells.size() < 2)
		{
			continue;
		}
		FootPath path;
		path.pointOfInterest = index;
		path.fromTrack = grid.network[cells.front()] == Grid::TRACK;
		std::vector<glm::vec2> line;
		for (int cell : cells)
		{
			line.push_back(grid.position(cell));
		}
		line.back() = target;
		//A branch starts exactly on the path it branches off from
		float startHeight = naturalGround(world, line.front());
		if (!path.fromTrack)
		{
			float closest = std::numeric_limits<float>::max();
			const glm::vec2 reached = line.front();
			for (const FootPath& other : paths)
			{
				for (const glm::vec3& p : other.points)
				{
					if (glm::distance(ground(p), reached) < closest)
					{
						closest = glm::distance(ground(p), reached);
						line.front() = ground(p);
						startHeight = p.y;
					}
				}
			}
		}
		//Round off the steps of the grid
		for (int iteration = 0; iteration < 10; ++iteration)
		{
			std::vector<glm::vec2> next = line;
			for (std::size_t i = 1; i + 1 < line.size(); ++i)
			{
				next[i] = 0.5f * line[i] + 0.25f * (line[i - 1] + line[i + 1]);
			}
			line.swap(next);
		}
		line = resampleLine(line, 1.0f);

		//The height along the path: the ground smoothed, cut down where it would be steeper than the maximum grade
		std::vector<float> heights(line.size());
		for (std::size_t i = 0; i < line.size(); ++i)
		{
			heights[i] = leveled(world, line[i]);
		}
		heights.front() = startHeight;
		smoothValues(heights, 4, 4);
		//On the floor of the track (which the path does not change) it is as high as the floor, so that it does not start with a
		//step where it leaves the floor (smoothed, the side of the canyon would lift it)
		for (std::size_t i = 0; i < line.size(); ++i)
		{
			TrackPath::Nearest nearest = track.nearest(line[i]);
			if (nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth + 0.5f)
			{
				heights[i] = leveled(world, line[i]);
			}
		}
		for (std::size_t i = 1; i < heights.size(); ++i)
		{
			heights[i] = std::min(heights[i], heights[i - 1] + MAX_GRADE);
		}
		for (std::size_t i = heights.size() - 1; i-- > 0;)
		{
			heights[i] = std::min(heights[i], heights[i + 1] + MAX_GRADE);
		}
		for (float& h : heights)
		{
			h = std::max(h, sea + 0.3f);
		}
		heights.front() = startHeight;
		smoothValues(heights, 1, 2);
		for (std::size_t i = 0; i < line.size(); ++i)
		{
			path.points.push_back(glm::vec3(line[i].x, heights[i], line[i].y));
		}

		//A second path from the same place of the track would go next to this one: the later paths leave the track elsewhere
		//(or branch off from this one)
		if (path.fromTrack)
		{
			for (std::size_t c = 0; c < cellCount; ++c)
			{
				if (grid.network[c] == Grid::TRACK && glm::distance(grid.position(static_cast<int>(c)), line.front()) < TRAILHEAD_SPACING)
				{
					grid.network[c] = Grid::NONE;
				}
			}
		}
		//The later paths may branch off from this one
		for (const glm::vec2& p : line)
		{
			int cell = grid.cellOf(p);
			int i = cell % grid.columns, j = cell / grid.columns;
			for (int dj = -1; dj <= 1; ++dj)
			{
				for (int di = -1; di <= 1; ++di)
				{
					int ni = std::clamp(i + di, 0, grid.columns - 1), nj = std::clamp(j + dj, 0, grid.rows - 1);
					std::uint8_t& network = grid.network[static_cast<std::size_t>(nj) * grid.columns + ni];
					network = network == Grid::NONE ? Grid::PATH : network;
				}
			}
		}

		//A cairn where the path begins: next to it where it leaves the floor of the track, or a bit after the fork
		std::size_t marked = std::min<std::size_t>(3, line.size() - 2);
		if (path.fromTrack)
		{
			while (marked + 2 < line.size())
			{
				TrackPath::Nearest nearest = track.nearest(line[marked]);
				if (!nearest.found || std::abs(nearest.lateral) > nearest.floorHalfWidth + 1.5f)
				{
					break;
				}
				++marked;
			}
		}
		glm::vec2 along = glm::normalize(line[marked + 1] - line[marked - 1]);
		glm::vec2 side = glm::vec2(-along.y, along.x) * (PATH_HALF_WIDTH + 1.1f);
		markers.push_back(glm::vec3(line[marked].x + side.x, 0.0f, line[marked].y + side.y));
		paths.push_back(std::move(path));
	}
}

void Landmarks::rasterize(const WorldGenerator& world)
{
	if (paths.empty())
	{
		return;
	}
	glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
	for (const FootPath& path : paths)
	{
		for (const glm::vec3& p : path.points)
		{
			low = glm::min(low, ground(p));
			high = glm::max(high, ground(p));
		}
	}
	rasterOrigin = glm::floor(low) - glm::vec2(REACH + 2.0f);
	rasterColumns = static_cast<int>((high.x - rasterOrigin.x + REACH + 2.0f) / RASTER_CELL) + 1;
	rasterRows = static_cast<int>((high.y - rasterOrigin.y + REACH + 2.0f) / RASTER_CELL) + 1;
	raster.assign(static_cast<std::size_t>(rasterColumns) * rasterRows, Cell());
	std::vector<int> firstLeg(raster.size(), -1), secondLeg(raster.size(), -1);
	auto sameLeg = [](int a, int b) { return b >= 0 && a / PATH_STRIDE == b / PATH_STRIDE && std::abs(a - b) <= LEG_LENGTH; };

	//The two closest legs of the paths for every cell
	for (std::size_t p = 0; p < paths.size(); ++p)
	{
		const std::vector<glm::vec3>& line = paths[p].points;
		for (std::size_t s = 0; s + 1 < line.size(); ++s)
		{
			glm::vec2 a = ground(line[s]), b = ground(line[s + 1]);
			int i0 = std::max(0, static_cast<int>(std::floor((std::min(a.x, b.x) - REACH - rasterOrigin.x) / RASTER_CELL)));
			int i1 = std::min(rasterColumns - 1, static_cast<int>(std::ceil((std::max(a.x, b.x) + REACH - rasterOrigin.x) / RASTER_CELL)));
			int j0 = std::max(0, static_cast<int>(std::floor((std::min(a.y, b.y) - REACH - rasterOrigin.y) / RASTER_CELL)));
			int j1 = std::min(rasterRows - 1, static_cast<int>(std::ceil((std::max(a.y, b.y) + REACH - rasterOrigin.y) / RASTER_CELL)));
			int leg = static_cast<int>(p) * PATH_STRIDE + static_cast<int>(s);
			for (int j = j0; j <= j1; ++j)
			{
				for (int i = i0; i <= i1; ++i)
				{
					glm::vec2 at = rasterOrigin + RASTER_CELL * glm::vec2(static_cast<float>(i), static_cast<float>(j));
					glm::vec2 ab = b - a;
					float t = std::clamp(glm::dot(at - a, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f, 1.0f);
					float distance = glm::distance(at, a + t * ab);
					if (distance >= REACH)
					{
						continue;
					}
					float h = glm::mix(line[s].y, line[s + 1].y, t);
					std::size_t c = static_cast<std::size_t>(j) * rasterColumns + i;
					Cell& cell = raster[c];
					if (sameLeg(leg, firstLeg[c]))
					{
						if (distance < cell.distance)
						{
							cell.distance = distance;
							cell.height = h;
							firstLeg[c] = leg;
						}
					}
					else if (sameLeg(leg, secondLeg[c]))
					{
						if (distance < cell.secondDistance)
						{
							cell.secondDistance = distance;
							cell.secondHeight = h;
							secondLeg[c] = leg;
						}
						if (cell.secondDistance < cell.distance)
						{
							std::swap(cell.distance, cell.secondDistance);
							std::swap(cell.height, cell.secondHeight);
							std::swap(firstLeg[c], secondLeg[c]);
						}
					}
					else if (distance < cell.distance)
					{
						cell.secondDistance = cell.distance;
						cell.secondHeight = cell.height;
						secondLeg[c] = firstLeg[c];
						cell.distance = distance;
						cell.height = h;
						firstLeg[c] = leg;
					}
					else if (distance < cell.secondDistance)
					{
						cell.secondDistance = distance;
						cell.secondHeight = h;
						secondLeg[c] = leg;
					}
				}
			}
		}
	}

	//Cells without a leg get the ground (they change nothing when they are interpolated with the others).
	//The paths do not cut into the floor of the race track and into the caves.
	const TrackPath& track = world.getPath();
	for (std::size_t c = 0; c < raster.size(); ++c)
	{
		Cell& cell = raster[c];
		glm::vec2 at = rasterOrigin + RASTER_CELL * glm::vec2(static_cast<float>(c % rasterColumns), static_cast<float>(c / rasterColumns));
		if (cell.distance >= REACH)
		{
			cell.distance = REACH;
			cell.height = leveled(world, at);
		}
		if (cell.secondDistance >= REACH)
		{
			cell.secondDistance = REACH;
			cell.secondHeight = cell.height;
		}
		if (cell.distance >= REACH)
		{
			continue;
		}
		TrackPath::Nearest nearest = track.nearest(at);
		if (nearest.found)
		{
			//Right from the edge of the floor: where a path climbs out of the canyon, the wall is cut too (no steep step up)
			cell.allowed = smoothstep(1.5f, 2.5f, std::abs(nearest.lateral) - nearest.floorHalfWidth);
		}
		if (world.caveRoofTop(at) > -1000.0f)
		{
			cell.allowed = 0.0f;
		}
	}
}

Landmarks::Cell Landmarks::cellAt(glm::vec2 position) const
{
	Cell result;
	result.distance = result.secondDistance = REACH;
	if (raster.empty())
	{
		return result;
	}
	float x = (position.x - rasterOrigin.x) / RASTER_CELL, z = (position.y - rasterOrigin.y) / RASTER_CELL;
	if (x < 0.0f || z < 0.0f || x >= static_cast<float>(rasterColumns - 1) || z >= static_cast<float>(rasterRows - 1))
	{
		return result;
	}
	int i = static_cast<int>(x), j = static_cast<int>(z);
	float fx = x - static_cast<float>(i), fz = z - static_cast<float>(j);
	const Cell& a = raster[static_cast<std::size_t>(j) * rasterColumns + i];
	const Cell& b = raster[static_cast<std::size_t>(j) * rasterColumns + i + 1];
	const Cell& c = raster[static_cast<std::size_t>(j + 1) * rasterColumns + i];
	const Cell& d = raster[static_cast<std::size_t>(j + 1) * rasterColumns + i + 1];
	auto mix = [&](float Cell::*field) {
		return (a.*field * (1.0f - fx) + b.*field * fx) * (1.0f - fz) + (c.*field * (1.0f - fx) + d.*field * fx) * fz;
	};
	result.distance = mix(&Cell::distance);
	result.height = mix(&Cell::height);
	result.secondDistance = mix(&Cell::secondDistance);
	result.secondHeight = mix(&Cell::secondHeight);
	result.allowed = mix(&Cell::allowed);
	return result;
}

float Landmarks::height(glm::vec2 position, float natural) const
{
	float result = applyLeveling(position, applySoftening(position, natural));
	Cell cell = cellAt(position);
	if (cell.distance >= REACH)
	{
		return result;
	}
	//Flat across the middle of the path, the slopes at its sides are wider where it is cut deeper (or filled higher)
	auto cut = [&](float ground, float distance, float pathHeight) {
		float depth = std::abs(ground - pathHeight);
		float shoulder = std::min(1.2f + 1.5f * depth, REACH - PATH_HALF_WIDTH - 0.5f);
		float weight = 1.0f - smoothstep(0.75f * PATH_HALF_WIDTH, PATH_HALF_WIDTH + shoulder, distance);
		return glm::mix(ground, pathHeight, weight * cell.allowed);
	};
	//The further leg first, the closer one cuts into it (e.g. the slope between the two sides of a hairpin bend)
	result = cut(result, cell.secondDistance, cell.secondHeight);
	return cut(result, cell.distance, cell.height);
}

float Landmarks::pathDistance(glm::vec2 position) const
{
	return cellAt(position).distance;
}

float Landmarks::trail(glm::vec2 position) const
{
	float result = 0.0f;
	float distance = pathDistance(position);
	if (distance < REACH)
	{
		float edge = distance + 0.35f * noise::fbm(position * 0.35f, 2, WorldGenerator::SEED + 70u);
		result = 1.0f - smoothstep(PATH_HALF_WIDTH - 0.45f, PATH_HALF_WIDTH + 0.45f, edge);
	}
	for (const PointOfInterest& point : points)
	{
		if (point.kind == PointOfInterest::Kind::CAMP)
		{
			//Trodden ground around the fire
			float fromFire = glm::distance(position, ground(point.position)) + 1.2f * noise::fbm(position * 0.4f, 2, WorldGenerator::SEED + 71u);
			result = std::max(result, 1.0f - smoothstep(3.0f, 5.5f, fromFire));
		}
	}
	return result;
}

int Landmarks::pointAt(glm::vec2 position) const
{
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		glm::vec2 offset = position - ground(points[i].position);
		if (glm::dot(offset, offset) < points[i].radius * points[i].radius)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

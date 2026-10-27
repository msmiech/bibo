#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <assimp/scene.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Physics/Physics.hpp"
#include "Scene/GameRules.hpp"
#include "World/Chunk.hpp"
#include "World/Landmarks.hpp"
#include "World/TrackPath.hpp"
#include "World/TrackTerrain.hpp"
#include "World/Vegetation.hpp"
#include "World/WorldGenerator.hpp"
#include "World/WorldStreamer.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
using namespace bibo::world;
namespace rules = bibo::scene::rules;
using glm::vec2;
using glm::vec3;

namespace
{
	vec3 startPosition()
	{
		return vec3(nodeModelMatrix(trackNode("BearBody"))[3]);
	}

	vec2 ground(vec3 position)
	{
		return vec2(position.x, position.z);
	}

}

TEST_CASE("The height field of the track has the height of the terrain mesh", "[assets][world]")
{
	std::vector<glm::vec3> vertices;
	std::vector<unsigned int> indices;
	REQUIRE(TrackTerrain::loadNodeMesh(trackScene(), "Boden", vertices, indices));
	const TrackTerrain& terrain = worldGenerator().getTrack();
	//At the vertices of the mesh (where the triangles meet at an angle, the height between the cells is a bit rounded).
	//A few walls of the mesh are vertical: a height field only has their top.
	int checked = 0, off = 0;
	for (const glm::vec3& vertex : vertices)
	{
		if (terrain.distanceInside(ground(vertex)) > 1.0f)
		{
			++checked;
			off += std::abs(terrain.height(ground(vertex)) - vertex.y) > 1.0f ? 1 : 0;
		}
	}
	CAPTURE(checked, off);
	REQUIRE(checked > 1000);
	CHECK(off < checked / 50);
	//In the middle of triangles it is the plane of the triangle
	float sumOfDifferences = 0.0f;
	int samples = 0;
	for (std::size_t t = 0; t + 2 < indices.size(); t += 3 * 7)
	{
		glm::vec3 middle = (vertices[indices[t]] + vertices[indices[t + 1]] + vertices[indices[t + 2]]) / 3.0f;
		if (terrain.distanceInside(ground(middle)) > 1.0f)
		{
			sumOfDifferences += std::abs(terrain.height(ground(middle)) - middle.y);
			++samples;
		}
	}
	REQUIRE(samples > 100);
	CHECK(sumOfDifferences / static_cast<float>(samples) < 0.05f);
}

TEST_CASE("The path of the track goes around the lap on the floor of the canyon", "[assets][world]")
{
	const TrackPath& path = worldGenerator().getPath();
	const TrackTerrain& terrain = worldGenerator().getTrack();
	const std::vector<TrackPath::Sample>& samples = path.getSamples();
	REQUIRE(samples.size() > 300);
	CHECK(path.getLength() > 400.0f);
	CHECK(path.getLength() < 900.0f);

	SECTION("It starts at the start and goes in the driving direction (along -z)")
	{
		CHECK(glm::distance(ground(samples[0].position), ground(startPosition())) < 3.0f);
		CHECK(samples[0].tangent.y < -0.8f);
	}

	SECTION("It goes through all checkpoints in their order")
	{
		float previous = 0.0f;
		for (std::size_t i = 0; i < rules::CHECKPOINT_COUNT; ++i)
		{
			TrackPath::Nearest nearest = path.nearest(rules::CHECKPOINTS[i]);
			CAPTURE(i, nearest.distance, nearest.lateral);
			REQUIRE(nearest.found);
			CHECK(std::abs(nearest.lateral) < rules::CHECKPOINT_RADIUS);
			CHECK(nearest.distance > previous);
			previous = nearest.distance;
		}
	}

	SECTION("The samples are evenly spaced, on the floor (the lowest point across the canyon) and in its middle")
	{
		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			const TrackPath::Sample& sample = samples[i];
			const TrackPath::Sample& next = samples[(i + 1) % samples.size()];
			CAPTURE(i, sample.position, sample.floorLeft, sample.floorRight);
			REQUIRE(glm::distance(ground(sample.position), ground(next.position)) < 1.5f);
			CHECK(sample.position.y == terrain.height(ground(sample.position)));
			CHECK(sample.floorLeft > 1.5f);
			CHECK(sample.floorRight > 1.5f);
			//The walls are higher than the floor on both sides
			CHECK(sample.wallLeft > 4.0f);
			CHECK(sample.wallRight > 4.0f);
		}
	}

	SECTION("The nearest point of the path is found from the side")
	{
		for (std::size_t i = 0; i < samples.size(); i += 17)
		{
			const TrackPath::Sample& sample = samples[i];
			vec2 right(-sample.tangent.y, sample.tangent.x);
			TrackPath::Nearest nearest = path.nearest(ground(sample.position) + right * 3.0f);
			CAPTURE(i);
			REQUIRE(nearest.found);
			CHECK(std::abs(nearest.lateral - 3.0f) < 0.3f);
			CHECK(std::abs(nearest.distance - sample.distance) < 1.0f);
		}
		CHECK_FALSE(path.nearest(vec2(5000.0f, 5000.0f)).found);
	}
}

TEST_CASE("The world keeps the track and puts an island around it", "[assets][world]")
{
	const WorldGenerator& world = worldGenerator();
	const TrackTerrain& track = world.getTrack();
	const TrackPath& path = world.getPath();

	SECTION("On the floor of the track the ground is the track terrain")
	{
		for (const TrackPath::Sample& sample : path.getSamples())
		{
			vec2 right(-sample.tangent.y, sample.tangent.x);
			for (float lateral : { -1.0f, 0.0f, 1.0f })
			{
				vec2 point = ground(sample.position) + right * (lateral * std::min(sample.floorLeft, sample.floorRight));
				REQUIRE(world.height(point) == track.height(point));
			}
		}
	}

	SECTION("Far from the track it is open sea, close to it land")
	{
		vec2 center = world.getCenter();
		for (int i = 0; i < 16; ++i)
		{
			float angle = static_cast<float>(i) * 6.2831853f / 16.0f;
			vec2 direction(std::cos(angle), std::sin(angle));
			CAPTURE(angle);
			CHECK(world.height(center + direction * 340.0f) < world.getSeaLevel() - 3.0f);
			CHECK(world.height(center + direction * 185.0f) > world.getSeaLevel() + 2.0f);
			float inland, cliff;
			world.coast(center + direction * 400.0f, inland, cliff);
			CHECK(inland < 0.0f);
		}
	}

	SECTION("The coast has beaches and cliffs")
	{
		int beaches = 0, cliffs = 0;
		for (int i = 0; i < 360; ++i)
		{
			float angle = static_cast<float>(i) * 6.2831853f / 360.0f;
			float inland, cliff;
			world.coast(world.getCenter() + vec2(std::cos(angle), std::sin(angle)) * 260.0f, inland, cliff);
			beaches += cliff < 0.2f ? 1 : 0;
			cliffs += cliff > 0.8f ? 1 : 0;
		}
		CHECK(beaches > 30);
		CHECK(cliffs > 30);
	}

	SECTION("The density is positive in the ground and negative in the air")
	{
		for (const TrackPath::Sample& sample : path.getSamples())
		{
			if (world.isInCave(sample.position + vec3(0.0f, 1.0f, 0.0f)))
			{
				continue;
			}
			REQUIRE(world.density(sample.position - vec3(0.0f, 0.5f, 0.0f)) > 0.0f);
			REQUIRE(world.density(sample.position + vec3(0.0f, 0.5f, 0.0f)) < 0.0f);
		}
	}

	SECTION("densityColumn gives exactly the values of density")
	{
		float column[40];
		for (vec2 point : { vec2(10.0f, 20.0f), ground(path.getSamples()[0].position), vec2(-150.0f, 160.0f) })
		{
			world.densityColumn(point.x, point.y, -10, 1.0f, 40, column);
			for (int i = 0; i < 40; ++i)
			{
				REQUIRE(column[i] == world.density(vec3(point.x, static_cast<float>(i - 10), point.y)));
			}
		}
	}
}

TEST_CASE("Caves cover parts of the track and leave room on the floor", "[assets][world]")
{
	const WorldGenerator& world = worldGenerator();
	const TrackPath& path = world.getPath();
	REQUIRE(world.getCaves().size() >= 1);
	for (const Cave& cave : world.getCaves())
	{
		CAPTURE(cave.start, cave.end);
		CHECK(cave.end - cave.start > 30.0f);
		//Not at the start and finish
		CHECK(cave.start > 40.0f);
		CHECK(cave.end < path.getLength() - 40.0f);
		for (float distance = cave.start + 4.0f; distance < cave.end - 4.0f; distance += 2.0f)
		{
			TrackPath::Sample sample = path.at(distance);
			CAPTURE(distance, sample.position);
			//Air above the trail for the bear and the camera
			for (float height = 0.5f; height < 6.0f; height += 0.5f)
			{
				REQUIRE(world.density(sample.position + vec3(0.0f, height, 0.0f)) < 0.0f);
			}
			CHECK(world.isInCave(sample.position + vec3(0.0f, 1.0f, 0.0f)));
			//Rock above (the roof follows the floor, but not down into the puddles)
			float floor = std::max(sample.position.y, cave.floorHeight - 1.5f);
			CHECK(world.density(vec3(sample.position.x, floor + cave.ceilingHeight + 3.0f, sample.position.z)) > 0.0f);
			CHECK(world.caveRoofTop(ground(sample.position)) > floor + cave.ceilingHeight);
		}
	}
	//Not under a roof outside of the caves
	CHECK_FALSE(world.isInCave(startPosition()));
	CHECK(world.caveRoofTop(ground(startPosition())) < -1000.0f);
}

TEST_CASE("The trail goes around the pools where the floor has room, through the ones that fill it", "[assets][world]")
{
	const WorldGenerator& world = worldGenerator();
	const TrackTerrain& track = world.getTrack();
	const float sea = world.getSeaLevel();
	auto dry = [&](const TrackPath::Sample& sample, float offset) {
		vec2 right(-sample.tangent.y, sample.tangent.x);
		for (float lateral = offset - TrackPath::TRAIL_HALF_WIDTH; lateral <= offset + TrackPath::TRAIL_HALF_WIDTH; lateral += 0.25f)
		{
			if (track.height(ground(sample.position) + right * lateral) < sea + 0.05f)
			{
				return false;
			}
		}
		return true;
	};
	int wet = 0, aroundPools = 0;
	bool inPool = false, fillsFloor = false;
	const std::vector<TrackPath::Sample>& samples = world.getPath().getSamples();
	for (const TrackPath::Sample& sample : samples)
	{
		CAPTURE(sample.distance, sample.position, sample.trailOffset, sample.floorLeft, sample.floorRight);
		//On the floor (it may reach a bit over its edge)
		CHECK(sample.trailOffset - TrackPath::TRAIL_HALF_WIDTH > -sample.floorLeft - 1.6f);
		CHECK(sample.trailOffset + TrackPath::TRAIL_HALF_WIDTH < sample.floorRight + 1.6f);
		if (dry(sample, sample.trailOffset))
		{
			//A stretch of the trail in the water only crosses a pool that fills the floor somewhere
			CHECK((!inPool || fillsFloor));
			inPool = fillsFloor = false;
			bool next = false;
			for (float lateral = -1.0f; lateral <= 1.0f; lateral += 0.5f)
			{
				next = next || !dry(sample, lateral);
			}
			aroundPools += next ? 1 : 0; //the middle of the floor is wet
			continue;
		}
		++wet;
		inPool = true;
		bool room = false;
		for (float offset = -sample.floorLeft + TrackPath::TRAIL_HALF_WIDTH; offset <= sample.floorRight - TrackPath::TRAIL_HALF_WIDTH; offset += 0.25f)
		{
			room = room || dry(sample, offset);
		}
		fillsFloor = fillsFloor || !room;
	}
	CHECK(wet > 0); //the race jumps over pools
	CHECK(aroundPools > 10);
}

TEST_CASE("Foot paths lead from the race track to the points of interest: beaches, a summit, a camp, standing stones, a cliff", "[assets][world]")
{
	const WorldGenerator& world = worldGenerator();
	const Landmarks& landmarks = world.getLandmarks();
	const std::vector<PointOfInterest>& points = landmarks.getPointsOfInterest();
	const TrackPath& track = world.getPath();
	const float sea = world.getSeaLevel();

	SECTION("There are beaches on different sides of the island and one of each other kind")
	{
		std::vector<float> beaches;
		int summits = 0, camps = 0, stones = 0, cliffs = 0;
		for (const PointOfInterest& point : points)
		{
			CAPTURE(point.position, static_cast<int>(point.kind));
			vec2 offset = ground(point.position) - world.getCenter();
			float inland, cliff;
			world.coast(ground(point.position), inland, cliff);
			CHECK(point.position.y > sea + 0.3f);
			CHECK(inland > 0.0f);
			switch (point.kind)
			{
			case PointOfInterest::Kind::BEACH:
				beaches.push_back(std::atan2(offset.y, offset.x));
				CHECK(inland < 20.0f);
				CHECK(world.material(point.position, vec3(0.0f, 1.0f, 0.0f)).sand > 0.5f);
				break;
			case PointOfInterest::Kind::SUMMIT:
				++summits;
				CHECK(point.position.y > 40.0f);
				break;
			case PointOfInterest::Kind::CAMP:
				++camps;
				break;
			case PointOfInterest::Kind::STANDING_STONES:
				++stones;
				break;
			case PointOfInterest::Kind::CLIFF_VIEW:
				++cliffs;
				CHECK(cliff > 0.8f);
				CHECK(point.position.y > sea + 8.0f);
				break;
			}
		}
		CHECK(summits == 1);
		CHECK(camps == 1);
		CHECK(stones == 1);
		CHECK(cliffs == 1);
		REQUIRE(beaches.size() >= 3);
		for (std::size_t i = 0; i < beaches.size(); ++i)
		{
			for (std::size_t j = i + 1; j < beaches.size(); ++j)
			{
				float apart = std::fmod(std::abs(beaches[i] - beaches[j]), 6.2831853f);
				CHECK(std::min(apart, 6.2831853f - apart) > glm::radians(50.0f));
			}
		}
	}

	SECTION("Every point of interest has a path that starts at the race track or branches off from another path")
	{
		std::vector<int> pathsTo(points.size(), 0);
		const std::vector<FootPath>& paths = landmarks.getPaths();
		int fromTrack = 0;
		for (std::size_t p = 0; p < paths.size(); ++p)
		{
			const FootPath& path = paths[p];
			REQUIRE(path.points.size() > 20);
			++pathsTo[path.pointOfInterest];
			CHECK(glm::distance(ground(path.points.back()), ground(points[path.pointOfInterest].position)) < 0.5f);
			vec2 start = ground(path.points.front());
			if (path.fromTrack)
			{
				++fromTrack;
				TrackPath::Nearest nearest = track.nearest(start);
				REQUIRE(nearest.found);
				CHECK(std::abs(nearest.lateral) < nearest.floorHalfWidth);
				CHECK(world.caveRoofTop(start) < -1000.0f);
			}
			else
			{
				float closest = 1e9f;
				for (std::size_t other = 0; other < p; ++other)
				{
					for (const vec3& point : paths[other].points)
					{
						closest = std::min(closest, glm::distance(ground(point), start));
					}
				}
				CHECK(closest < 0.01f);
			}
		}
		CHECK(fromTrack >= 1); //where the side of the canyon is gentle, the others branch off
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			CAPTURE(i);
			CHECK(pathsTo[i] == 1);
		}
	}

	SECTION("The paths are cut into the ground: not steep along them, walkable across, out of the water, trodden earth")
	{
		for (const FootPath& path : landmarks.getPaths())
		{
			for (std::size_t i = 1; i < path.points.size(); ++i)
			{
				const vec3& point = path.points[i];
				CAPTURE(point);
				float step = glm::distance(ground(point), ground(path.points[i - 1]));
				REQUIRE(step < 1.3f);
				CHECK(std::abs(point.y - path.points[i - 1].y) <= Landmarks::MAX_GRADE * step + 0.05f);
				TrackPath::Nearest nearest = track.nearest(ground(point));
				if (nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth + 4.0f)
				{
					continue; //where it comes out of the canyon of the track, the floor is not changed
				}
				vec3 surface, normal;
				REQUIRE(world.surface(ground(point), surface, normal));
				CHECK(std::abs(surface.y - point.y) < 0.2f);
				CHECK(surface.y > sea);
				CHECK(normal.y > 0.75f);
				SurfaceMaterial material = world.material(surface, normal);
				CHECK(material.trail + material.sand > 0.5f);
			}
		}
	}

	SECTION("The paths climb gently, also out of the canyon of the track, and do not run along crags")
	{
		for (const FootPath& path : landmarks.getPaths())
		{
			for (std::size_t i = 1; i < path.points.size(); ++i)
			{
				vec2 from = ground(path.points[i - 1]), to = ground(path.points[i]);
				CAPTURE(to);
				TrackPath::Nearest nearest = track.nearest(to);
				if (nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth)
				{
					continue; //on the floor of the track
				}
				//The ground the bear walks on (not only the middle line of the path). Right at the edge of the floor the side of
				//the canyon is not cut (the track terrain), it may be steeper there, but far less than the bear can climb.
				bool edge = nearest.found && std::abs(nearest.lateral) < nearest.floorHalfWidth + 2.5f;
				CHECK(std::abs(world.height(to) - world.height(from)) <= (edge ? 0.75f : 0.5f) * glm::distance(from, to) + 0.02f);
				//Beside the path: no walls of rock
				vec2 along = glm::normalize(to - from), side(-along.y, along.x);
				float steepest = 0.0f;
				for (float sign : { -1.0f, 1.0f })
				{
					for (float distance = 1.5f; distance < 5.5f; distance += 0.5f)
					{
						float h0 = world.height(to + side * (sign * distance)), h1 = world.height(to + side * (sign * (distance + 0.5f)));
						steepest = std::max(steepest, std::abs(h1 - h0) / 0.5f);
					}
				}
				CHECK(steepest < 2.6f);
			}
		}
	}

	SECTION("Nothing grows on the paths and in the places, the places have their stones and logs")
	{
		std::set<std::pair<int, int>> keys;
		for (const FootPath& path : landmarks.getPaths())
		{
			for (std::size_t i = 0; i < path.points.size(); i += 6)
			{
				ChunkKey key = chunkAt(path.points[i]);
				keys.insert({ key.x, key.z });
			}
			ChunkKey end = chunkAt(path.points.back());
			keys.insert({ end.x, end.z });
		}
		std::vector<int> props(points.size(), 0);
		for (const auto& key : keys)
		{
			std::vector<bibo::physics::ShapePart> collision;
			std::vector<Instance> large = scatter(world, { key.first, key.second }, Layer::LARGE, &collision);
			int solid = 0;
			for (const Instance& instance : large)
			{
				vec3 position(instance.positionScale);
				CAPTURE(position, static_cast<int>(instance.model()));
				bool plant = instance.model() <= Model::JUNIPER;
				solid += instance.model() <= Model::PINE || instance.model() >= Model::STONE_ROUND ? 1 : 0;
				if (plant)
				{
					CHECK(landmarks.pathDistance(ground(position)) > Landmarks::PATH_HALF_WIDTH + 0.5f);
					CHECK(landmarks.pointAt(ground(position)) < 0);
				}
				else if (landmarks.pointAt(ground(position)) >= 0)
				{
					++props[landmarks.pointAt(ground(position))];
				}
			}
			CHECK(collision.size() == static_cast<std::size_t>(solid));
		}
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			CAPTURE(i, static_cast<int>(points[i].kind));
			CHECK(props[i] >= 3);
		}
	}
}

TEST_CASE("The chunks of the world fit together", "[assets][world]")
{
	const WorldGenerator& world = worldGenerator();
	//A chunk on the track (with the start) and its neighbor along x
	const ChunkKey key = chunkAt(startPosition());
	const ChunkKey neighbor{ key.x + 1, key.y, key.z };
	ChunkMesh mesh = meshChunk(world, key, 0);
	ChunkMesh next = meshChunk(world, neighbor, 0);
	REQUIRE_FALSE(mesh.empty());
	REQUIRE_FALSE(next.empty());

	SECTION("The vertices are on the surface, their normals point into the air")
	{
		for (std::size_t i = 0; i < mesh.surfaceIndexCount; ++i)
		{
			const TerrainVertex& vertex = mesh.vertices[mesh.indices[i]];
			CAPTURE(vertex.position);
			//Distance to the surface: the density divided by its gradient (steep at the walls of the track)
			const float E = 0.05f;
			glm::vec3 gradient(world.density(vertex.position + vec3(E, 0, 0)) - world.density(vertex.position - vec3(E, 0, 0)),
				world.density(vertex.position + vec3(0, E, 0)) - world.density(vertex.position - vec3(0, E, 0)),
				world.density(vertex.position + vec3(0, 0, E)) - world.density(vertex.position - vec3(0, 0, E)));
			float distance = std::abs(world.density(vertex.position)) / std::max(glm::length(gradient) / (2.0f * E), 1e-3f);
			REQUIRE(distance < 0.25f); //a voxel is 1 unit
			REQUIRE(world.density(vertex.position + vertex.normal * 0.3f) < 0.0f);
		}
	}

	SECTION("Neighbors fit together without cracks")
	{
		//Both chunks compute the voxels at their common border the same way: the vertices are exactly the same,
		//the surfaces of the two chunks together have no open edges along the border
		std::map<std::tuple<float, float, float>, int> ids;
		auto id = [&ids](const glm::vec3& p) { return ids.emplace(std::make_tuple(p.x, p.y, p.z), static_cast<int>(ids.size())).first->second; };
		std::map<std::pair<int, int>, int> edges;
		for (const ChunkMesh* part : { &mesh, &next })
		{
			for (std::size_t t = 0; t < part->surfaceIndexCount; t += 3)
			{
				for (int e = 0; e < 3; ++e)
				{
					int a = id(part->vertices[part->indices[t + e]].position), b = id(part->vertices[part->indices[t + (e + 1) % 3]].position);
					++edges[{ std::min(a, b), std::max(a, b) }];
				}
			}
		}
		std::vector<glm::vec3> positions(ids.size());
		for (const auto& entry : ids)
		{
			positions[entry.second] = glm::vec3(std::get<0>(entry.first), std::get<1>(entry.first), std::get<2>(entry.first));
		}
		float border = chunkBox(neighbor).minimum.x;
		Box box = chunkBox(key);
		auto nearBorder = [&](const glm::vec3& p) {
			//Away from the other sides of the two chunks, where the surface is open
			return std::abs(p.x - border) < 2.0f && p.y > box.minimum.y + 2.0f && p.y < box.maximum.y - 2.0f
				&& p.z > box.minimum.z + 2.0f && p.z < box.maximum.z - 2.0f;
		};
		int bordering = 0;
		for (const auto& edge : edges)
		{
			const glm::vec3& a = positions[edge.first.first];
			const glm::vec3& b = positions[edge.first.second];
			if (nearBorder(a) && nearBorder(b))
			{
				++bordering;
				CAPTURE(a, b);
				CHECK(edge.second == 2);
			}
		}
		CHECK(bordering > 20);
	}

	SECTION("Every edge of the surface belongs to at most two triangles, the open ones get skirts")
	{
		std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
		for (std::size_t t = 0; t < mesh.surfaceIndexCount; t += 3)
		{
			for (int e = 0; e < 3; ++e)
			{
				std::uint32_t a = mesh.indices[t + e], b = mesh.indices[t + (e + 1) % 3];
				++edges[{ std::min(a, b), std::max(a, b) }];
			}
		}
		int open = 0;
		for (const auto& edge : edges)
		{
			REQUIRE(edge.second <= 2);
			open += edge.second == 1 ? 1 : 0;
		}
		CHECK(open > 0);
		CHECK(mesh.indices.size() - mesh.surfaceIndexCount == static_cast<std::size_t>(open) * 6);
	}

	SECTION("Coarser levels of detail have fewer triangles, but the same shape")
	{
		std::size_t previous = mesh.indices.size();
		for (int lod = 1; lod < LOD_COUNT; ++lod)
		{
			ChunkMesh coarse = meshChunk(world, key, lod);
			CAPTURE(lod);
			REQUIRE_FALSE(coarse.empty());
			CHECK(coarse.indices.size() < previous);
			previous = coarse.indices.size();
			//Its vertices are on the surface too (they are moved onto it)
			int onSurface = 0;
			for (const TerrainVertex& vertex : coarse.vertices)
			{
				onSurface += std::abs(world.density(vertex.position)) < 0.5f ? 1 : 0;
			}
			CHECK(onSurface >= static_cast<int>(coarse.vertices.size()) / 2); //the others are the skirts
		}
	}

	SECTION("Chunks high in the air and deep in the ground are empty")
	{
		CHECK(meshChunk(world, { key.x, 5, key.z }, 0).empty());
		CHECK(meshChunk(world, { key.x, -4, key.z }, 0).empty());
	}
}

TEST_CASE("Trees grow on the slopes and in the forest, but not on the track", "[assets][world][vegetation]")
{
	const WorldGenerator& world = worldGenerator();
	const TrackPath& path = world.getPath();
	//The columns along the track
	std::set<std::pair<int, int>> keys;
	for (std::size_t i = 0; i < path.getSamples().size(); i += 8)
	{
		ChunkKey key = chunkAt(path.getSamples()[i].position);
		keys.insert({ key.x, key.z });
	}
	int treesNextToTheTrack = 0, glowingMushrooms = 0;
	for (const auto& key : keys)
	{
		ColumnKey column{ key.first, key.second };
		std::vector<bibo::physics::ShapePart> collision;
		std::vector<Instance> large = scatter(world, column, Layer::LARGE, &collision);
		std::vector<Instance> small = scatter(world, column, Layer::SMALL);
		//Deterministic
		std::vector<Instance> again = scatter(world, column, Layer::LARGE);
		REQUIRE(again.size() == large.size());
		for (std::size_t i = 0; i < large.size(); ++i)
		{
			REQUIRE(again[i].positionScale == large[i].positionScale);
		}
		for (const Instance& instance : large)
		{
			vec3 position(instance.positionScale);
			TrackPath::Nearest nearest = path.nearest(ground(position));
			bool tree = instance.model() <= Model::PINE;
			CAPTURE(position, static_cast<int>(instance.model()), nearest.lateral, nearest.floorHalfWidth);
			REQUIRE(position.y > world.getSeaLevel() - 2.0f);
			if (tree && nearest.found)
			{
				//Not on the floor, where the bear runs
				REQUIRE(std::abs(nearest.lateral) > nearest.floorHalfWidth + 1.0f);
				treesNextToTheTrack += std::abs(nearest.lateral) < nearest.floorHalfWidth + 20.0f ? 1 : 0;
			}
		}
		for (const Instance& instance : small)
		{
			vec3 position(instance.positionScale);
			if (instance.model() == Model::MUSHROOM_GLOW)
			{
				++glowingMushrooms;
				CHECK(world.caveRoofTop(ground(position)) > position.y); //only in the caves
			}
			else
			{
				TrackPath::Nearest nearest = path.nearest(ground(position));
				bool onTrail = nearest.found && std::abs(nearest.trailLateral) < TrackPath::TRAIL_HALF_WIDTH - 1.0f && position.y < nearest.floorHeight + 1.0f;
				//Pebbles may lie on the trail, nothing else
				bool pebble = instance.model() >= Model::STONE_ROUND && instance.model() <= Model::STONE_SHARP;
				CHECK((!onTrail || pebble));
			}
		}
		//Every trunk and boulder has its collision shape
		int solid = 0;
		for (const Instance& instance : large)
		{
			solid += instance.model() <= Model::PINE || instance.model() >= Model::STONE_ROUND ? 1 : 0;
		}
		CHECK(collision.size() == static_cast<std::size_t>(solid));
	}
	CHECK(treesNextToTheTrack > 300);
	CHECK(glowingMushrooms > 20);
}

TEST_CASE("The world is streamed with the level of detail of the distance", "[assets][world][physics]")
{
	bibo::physics::PhysicsWorld physicsWorld;
	const vec3 start = startPosition();
	WorldStreamer::Settings settings;
	auto streamer = std::make_unique<WorldStreamer>(worldGenerator(), &physicsWorld, settings, 2);

	SECTION("The bear has ground under its feet right away")
	{
		streamer->ensureCollision(start, 10.0f);
		bibo::physics::RayHit hit;
		REQUIRE(physicsWorld.castRay(start + vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -15.0f, 0.0f), hit, true));
		CHECK(std::abs(hit.position.y - worldGenerator().height(ground(start))) < 0.1f);
	}

	SECTION("Close chunks get full detail, far ones less; the vegetation is complete")
	{
		REQUIRE(streamer->finish(start, start, 120.0f));
		std::unordered_map<ChunkKey, int, ChunkKeyHash> lods;
		for (const WorldStreamer::MeshUpdate& update : streamer->takeMeshUpdates())
		{
			if (update.mesh)
			{
				lods[update.key] = update.mesh->lod;
			}
		}
		REQUIRE_FALSE(lods.empty());
		for (const auto& entry : lods)
		{
			float distance = distanceToBox(start, chunkBox(entry.first));
			CAPTURE(distance, entry.second);
			if (distance < settings.lodDistances[0])
			{
				CHECK(entry.second == 0);
			}
			if (distance > settings.lodDistances[LOD_COUNT - 2] + 20.0f)
			{
				CHECK(entry.second == LOD_COUNT - 1);
			}
		}
		WorldStreamer::Statistics statistics = streamer->getStatistics();
		CHECK(statistics.pendingJobs == 0);
		CHECK(statistics.collisionBodies > 0);
		CHECK(statistics.smallColumns > 0);
		bool small = false, large = false;
		for (const WorldStreamer::VegetationUpdate& update : streamer->takeVegetationUpdates())
		{
			(update.layer == Layer::SMALL ? small : large) = true;
		}
		CHECK(large);
		CHECK(small);
	}

	SECTION("The physics follows the bear, the grass the camera")
	{
		REQUIRE(streamer->finish(start, start, 120.0f));
		vec3 far = vec3(worldGenerator().getPath().at(300.0f).position);
		REQUIRE(streamer->finish(far, far, 120.0f));
		//Bodies around the new position, the ones at the start are gone
		bibo::physics::RayHit hit;
		CHECK(physicsWorld.castRay(far + vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -15.0f, 0.0f), hit, true));
		CHECK_FALSE(physicsWorld.castRay(start + vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, -15.0f, 0.0f), hit, true));
		bool removed = false;
		for (const WorldStreamer::VegetationUpdate& update : streamer->takeVegetationUpdates())
		{
			removed = removed || (update.layer == Layer::SMALL && !update.instances);
		}
		CHECK(removed);
	}

	SECTION("The streamer can be destroyed while its threads work")
	{
		streamer->update(start, start);
		streamer.reset();
		CHECK(physicsWorld.getBodyCount() == 0);
	}
}

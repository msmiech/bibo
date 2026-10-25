#include "Vegetation.hpp"
#include <algorithm>
#include <cmath>
#include "Chunk.hpp"
#include "Noise.hpp"

using namespace bibo;
using namespace bibo::world;

namespace
{
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);

	//Height and radius of the models at scale 1 (VegetationMeshes builds them this size)
	const struct { float height, radius; } MODEL_SIZES[MODEL_COUNT] = {
		{ 15.0f, 3.6f }, //SPRUCE_TALL
		{ 11.0f, 4.0f }, //SPRUCE_WIDE
		{ 6.5f, 2.2f }, //SPRUCE_YOUNG
		{ 17.0f, 3.4f }, //PINE
		{ 1.3f, 1.4f }, //BUSH
		{ 0.9f, 1.6f }, //JUNIPER
		{ 0.9f, 1.0f }, //FERN
		{ 0.55f, 0.35f }, //GRASS
		{ 0.6f, 0.35f }, //GRASS_DRY
		{ 0.32f, 0.16f }, //MUSHROOM_RED
		{ 0.24f, 0.14f }, //MUSHROOM_BROWN
		{ 0.2f, 0.1f }, //MUSHROOM_GLOW
		{ 0.8f, 0.6f }, //STONE_ROUND
		{ 0.5f, 0.7f }, //STONE_FLAT
		{ 1.0f, 0.55f }, //STONE_SHARP
		{ 2.1f, 0.4f }, //STONE_STANDING
		{ 0.8f, 2.6f }, //LOG
		{ 0.7f, 0.6f } }; //STUMP

	float smoothstep(float edge0, float edge1, float x)
	{
		float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	glm::quat rotationBetween(glm::vec3 from, glm::vec3 to)
	{
		float cosine = glm::dot(from, to);
		if (cosine > 0.99999f)
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		glm::vec3 axis = glm::cross(from, to);
		return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
	}

	/**
	* Where something could be placed and what is there
	*/
	struct Site
	{
		glm::vec3 position, normal;
		TrackPath::Nearest track;
		SurfaceMaterial material;
		float snow = 0.0f;
		float forest = 0.0f; //density of the forest (0: clearing)
		bool onFloor = false; //on the floor of the race track
		float trailDistance = 1e6f; //from the edge of the trail (negative on the trail)
		float pathDistance = 1e6f; //from the edge of the closest foot path (negative on it)
		bool clearing = false; //in a point of interest
	};

	class Scatterer
	{
	public:
		Scatterer(const WorldGenerator& world, ColumnKey column, std::vector<physics::ShapePart>* collision)
			: world(world), column(column), collision(collision)
		{
			origin = glm::vec2(static_cast<float>(column.x), static_cast<float>(column.z)) * CHUNK_SIZE;
		}

		/**
		* Calls place(position, random) for the points of a jittered grid (one random point per cell) that are in the column.
		* The grid is the same for all columns, so every point belongs to exactly one column.
		*/
		template <typename Place>
		void forEachPoint(float spacing, std::uint32_t salt, Place place)
		{
			int i0 = static_cast<int>(std::floor(origin.x / spacing)), i1 = static_cast<int>(std::floor((origin.x + CHUNK_SIZE) / spacing));
			int j0 = static_cast<int>(std::floor(origin.y / spacing)), j1 = static_cast<int>(std::floor((origin.y + CHUNK_SIZE) / spacing));
			for (int j = j0; j <= j1; ++j)
			{
				for (int i = i0; i <= i1; ++i)
				{
					noise::Random random(noise::hash(i, j, WorldGenerator::SEED ^ salt));
					glm::vec2 point = (glm::vec2(static_cast<float>(i), static_cast<float>(j)) + glm::vec2(random.unit(), random.unit())) * spacing;
					if (inColumn(point))
					{
						place(point, random);
					}
				}
			}
		}

		bool inColumn(glm::vec2 point) const
		{
			return point.x >= origin.x && point.y >= origin.y && point.x < origin.x + CHUNK_SIZE && point.y < origin.y + CHUNK_SIZE;
		}

		bool nearColumn(glm::vec2 point, float radius) const
		{
			return point.x > origin.x - radius && point.y > origin.y - radius && point.x < origin.x + CHUNK_SIZE + radius && point.y < origin.y + CHUNK_SIZE + radius;
		}

		/**
		* A stone, log or stump on the ground with its collision shape (like the ones scattered in the forest)
		* @param alignment 0: upright, 1: like the ground
		* @param sink How deep it is in the ground (times its scale)
		* @param upright A standing stone (the collision is a capsule that goes up)
		*/
		void prop(Model model, glm::vec2 at, float yaw, float scale, float alignment, float sink, std::uint32_t seed, bool upright = false)
		{
			glm::vec3 position, normal;
			if (!inColumn(at) || !world.surface(at, position, normal))
			{
				return;
			}
			noise::Random random(noise::hash(static_cast<int>(std::floor(at.x * 8.0f)), static_cast<int>(std::floor(at.y * 8.0f)), seed));
			glm::quat rotation = orientation(yaw, normal, alignment);
			glm::vec3 foot = position - glm::normalize(glm::mix(UP, normal, alignment)) * (sink * scale);
			solid(model, foot, rotation, scale, normal, upright, random);
		}

		/**
		* Flat stones stacked on each other, smaller to the top
		*/
		void cairn(glm::vec2 at, float size, int count, std::uint32_t seed)
		{
			glm::vec3 position, normal;
			if (!inColumn(at) || !world.surface(at, position, normal))
			{
				return;
			}
			noise::Random random(noise::hash(static_cast<int>(std::floor(at.x * 8.0f)), static_cast<int>(std::floor(at.y * 8.0f)), seed));
			glm::vec3 foot = position - glm::vec3(0.0f, 0.1f * size, 0.0f);
			for (int i = 0; i < count; ++i)
			{
				float scale = size * (1.0f - 0.7f * static_cast<float>(i) / static_cast<float>(std::max(count - 1, 1)));
				glm::vec3 jitter(random.range(-0.06f, 0.06f) * size, 0.0f, random.range(-0.06f, 0.06f) * size);
				solid(Model::STONE_FLAT, foot + jitter, orientation(random.range(0.0f, 6.2831853f), UP, 0.0f), scale, UP, false, random);
				foot.y += 0.36f * scale; //a flat stone is 0.5 high at scale 1, they lie a bit in each other
			}
		}

		void solid(Model model, glm::vec3 foot, glm::quat rotation, float scale, glm::vec3 normal, bool upright, noise::Random& random)
		{
			add(model, foot, rotation, scale, random);
			if (!collision)
			{
				return;
			}
			physics::ShapePart part;
			if (model == Model::LOG)
			{
				//The log lies along its x axis, the capsule goes along y
				part.type = physics::ShapePart::CAPSULE;
				part.radius = 0.36f * scale;
				part.halfHeight = 2.0f * scale;
				part.rotation = rotation * glm::angleAxis(1.5707963f, glm::vec3(0.0f, 0.0f, 1.0f));
				part.position = foot + rotation * glm::vec3(0.0f, part.radius, 0.0f);
			}
			else if (model == Model::STUMP)
			{
				part.type = physics::ShapePart::CAPSULE;
				part.radius = 0.42f * scale;
				part.halfHeight = 0.15f * scale;
				part.position = foot + glm::vec3(0.0f, part.radius + part.halfHeight, 0.0f);
			}
			else if (upright)
			{
				part.type = physics::ShapePart::CAPSULE;
				part.radius = 0.9f * scale * modelRadius(model);
				part.halfHeight = std::max(0.0f, 0.5f * scale * modelHeight(model) - part.radius);
				part.position = foot + glm::vec3(0.0f, part.radius + part.halfHeight, 0.0f);
			}
			else
			{
				part.type = physics::ShapePart::SPHERE;
				part.radius = 0.85f * scale * modelRadius(model);
				part.position = foot + normal * (0.2f * scale);
			}
			collision->push_back(part);
		}

		bool site(glm::vec2 point, Site& result) const
		{
			if (!world.surface(point, result.position, result.normal))
			{
				return false;
			}
			result.track = world.getPath().nearest(point);
			result.material = world.material(result.position, result.normal);
			result.forest = smoothstep(-0.3f, 0.2f, noise::fbm(point * 0.011f, 3, WorldGenerator::SEED + 60u));
			if (result.track.found)
			{
				float fromFloor = std::abs(result.track.lateral) - result.track.floorHalfWidth;
				result.onFloor = fromFloor < 1.0f && result.position.y < result.track.floorHeight + 1.8f;
				result.trailDistance = std::abs(result.track.trailLateral) - TrackPath::TRAIL_HALF_WIDTH;
				if (fromFloor > 1.0f && fromFloor < 24.0f)
				{
					result.forest = std::max(result.forest, 0.92f); //trees on the slopes left and right of the track
				}
			}
			const Landmarks& landmarks = world.getLandmarks();
			result.pathDistance = landmarks.pathDistance(point) - Landmarks::PATH_HALF_WIDTH;
			result.clearing = landmarks.pointAt(point) >= 0;
			result.forest *= 1.0f - smoothstep(34.0f, 50.0f, result.position.y); //bare mountain tops
			result.snow = world.snowCover(result.position, result.normal, 1.0f, result.material);
			return true;
		}

		bool keepsClear(glm::vec3 position, float radius) const
		{
			for (const KeepClear& object : world.getKeepClear())
			{
				glm::vec2 offset(position.x - object.center.x, position.z - object.center.z);
				if (glm::length(offset) < object.radius + radius && position.y < object.center.y + object.radius + 1.0f)
				{
					return false;
				}
			}
			return true;
		}

		void add(Model model, glm::vec3 position, glm::quat rotation, float scale, noise::Random& random)
		{
			Instance instance;
			instance.positionScale = glm::vec4(position, scale);
			instance.rotation = glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w);
			instance.parameters = glm::vec4(random.range(-1.0f, 1.0f), random.unit(), static_cast<float>(model), 0.0f);
			instances.push_back(instance);
		}

		/**
		* Turned around the vertical axis, leaning with the ground (alignment 0: upright, 1: like the ground)
		*/
		static glm::quat orientation(float yaw, glm::vec3 normal, float alignment)
		{
			glm::vec3 up = glm::normalize(glm::mix(UP, normal, alignment));
			return rotationBetween(UP, up) * glm::angleAxis(yaw, UP);
		}

		void scatterLarge()
		{
			const float seaLevel = world.getSeaLevel();
			//Trees
			forEachPoint(4.4f, 101u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.onFloor || s.normal.y < 0.5f || s.position.y < seaLevel + 2.2f || s.material.sand > 0.25f
					|| s.material.cave > 0.5f || (s.material.cliff > 0.5f && s.normal.y < 0.85f) || s.pathDistance < 1.8f || s.clearing || random.unit() > 0.9f * s.forest)
				{
					return;
				}
				if (s.track.found && std::abs(s.track.lateral) < s.track.floorHalfWidth + 1.5f)
				{
					return;
				}
				float choice = random.unit();
				//Mostly spruces, a few pines (more of them at the coast)
				float pines = 0.04f + 0.25f * (1.0f - smoothstep(4.0f, 15.0f, s.position.y - seaLevel));
				Model model = choice < pines ? Model::PINE : choice < 0.45f ? Model::SPRUCE_TALL : choice < 0.75f ? Model::SPRUCE_WIDE : Model::SPRUCE_YOUNG;
				float scale = random.range(0.75f, 1.2f);
				if (!keepsClear(s.position, modelRadius(model) * scale * 0.6f))
				{
					return;
				}
				float yaw = random.range(0.0f, 6.2831853f);
				glm::quat rotation = glm::angleAxis(random.range(-0.05f, 0.05f), glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(yaw, UP);
				glm::vec3 foot = s.position - glm::vec3(0.0f, 0.2f, 0.0f);
				add(model, foot, rotation, scale, random);
				if (collision)
				{
					//The trunk up to where the branches are dense
					physics::ShapePart trunk;
					trunk.type = physics::ShapePart::CAPSULE;
					trunk.radius = (model == Model::PINE ? 0.3f : 0.26f) * scale;
					trunk.halfHeight = 2.6f * scale;
					trunk.position = foot + glm::vec3(0.0f, trunk.halfHeight + trunk.radius, 0.0f);
					collision->push_back(trunk);
				}
			});
			//Bushes
			forEachPoint(5.0f, 102u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.normal.y < 0.55f || s.position.y < seaLevel + 1.6f || s.material.sand > 0.4f || s.material.cave > 0.5f
					|| s.trailDistance < 1.6f || s.pathDistance < 0.8f || s.clearing || random.unit() > 0.3f * (0.4f + 0.6f * s.forest))
				{
					return;
				}
				Model model = random.unit() < 0.6f ? Model::BUSH : Model::JUNIPER;
				float scale = random.range(0.7f, 1.35f);
				if (!keepsClear(s.position, modelRadius(model) * scale))
				{
					return;
				}
				add(model, s.position - glm::vec3(0.0f, 0.1f, 0.0f), orientation(random.range(0.0f, 6.2831853f), s.normal, 0.3f), scale, random);
			});
			//Boulders, more of them on the cliffs and the mountains
			forEachPoint(12.0f, 103u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.onFloor || s.normal.y < 0.4f || s.position.y < seaLevel - 1.0f || s.material.cave > 0.5f || s.pathDistance < 2.0f || s.clearing)
				{
					return;
				}
				float chance = 0.2f + 0.45f * s.material.cliff + 0.35f * smoothstep(30.0f, 45.0f, s.position.y);
				if (random.unit() > chance || (s.track.found && std::abs(s.track.lateral) < s.track.floorHalfWidth + 2.0f))
				{
					return;
				}
				float choice = random.unit();
				Model model = choice < 0.4f ? Model::STONE_ROUND : choice < 0.75f ? Model::STONE_SHARP : Model::STONE_FLAT;
				float scale = random.range(1.0f, 2.8f);
				if (!keepsClear(s.position, scale))
				{
					return;
				}
				glm::vec3 foot = s.position - s.normal * (0.25f * scale);
				add(model, foot, orientation(random.range(0.0f, 6.2831853f), s.normal, 0.7f), scale, random);
				if (collision)
				{
					physics::ShapePart stone;
					stone.type = physics::ShapePart::SPHERE;
					stone.radius = 0.85f * scale * modelRadius(model);
					stone.position = foot + s.normal * (0.2f * scale);
					collision->push_back(stone);
				}
			});
			//Fallen logs and stumps in the forest
			forEachPoint(20.0f, 104u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.onFloor || s.normal.y < 0.82f || s.forest < 0.45f || s.position.y < seaLevel + 2.5f || s.material.cave > 0.5f
					|| s.pathDistance < 2.0f || s.clearing || random.unit() > 0.38f || (s.track.found && std::abs(s.track.lateral) < s.track.floorHalfWidth + 3.0f))
				{
					return;
				}
				bool isLog = random.unit() < 0.55f;
				float scale = random.range(0.8f, 1.2f);
				Model model = isLog ? Model::LOG : Model::STUMP;
				if (!keepsClear(s.position, modelRadius(model) * scale))
				{
					return;
				}
				float yaw = random.range(0.0f, 6.2831853f);
				glm::quat rotation = orientation(yaw, s.normal, isLog ? 1.0f : 0.2f);
				glm::vec3 foot = s.position - glm::vec3(0.0f, isLog ? 0.1f : 0.15f, 0.0f);
				add(model, foot, rotation, scale, random);
				if (collision)
				{
					physics::ShapePart part;
					part.type = physics::ShapePart::CAPSULE;
					if (isLog)
					{
						//The log lies along its x axis, the capsule goes along y
						part.radius = 0.36f * scale;
						part.halfHeight = 2.0f * scale;
						part.rotation = rotation * glm::angleAxis(1.5707963f, glm::vec3(0.0f, 0.0f, 1.0f));
						part.position = foot + rotation * glm::vec3(0.0f, part.radius, 0.0f);
					}
					else
					{
						part.radius = 0.42f * scale;
						part.halfHeight = 0.15f * scale;
						part.position = foot + glm::vec3(0.0f, part.radius + part.halfHeight, 0.0f);
					}
					collision->push_back(part);
				}
			});
		}

		/**
		* The things at the points of interest (cairns, a fire ring with log seats, standing stones, driftwood) and the cairns
		* where the foot paths begin. Every prop has its own random numbers, so it is the same in every column.
		*/
		void scatterLandmarks()
		{
			const Landmarks& landmarks = world.getLandmarks();
			const std::vector<PointOfInterest>& points = landmarks.getPointsOfInterest();
			const float TAU = 6.2831853f;
			for (std::size_t p = 0; p < points.size(); ++p)
			{
				const PointOfInterest& point = points[p];
				glm::vec2 middle(point.position.x, point.position.z);
				if (!nearColumn(middle, point.radius + 8.0f))
				{
					continue;
				}
				std::uint32_t salt = WorldGenerator::SEED + 300u + static_cast<std::uint32_t>(p) * 101u;
				noise::Random layout(noise::hash(static_cast<int>(p), 0, salt));
				glm::vec2 facing = point.facing, across(-point.facing.y, point.facing.x);
				auto around = [&](float angle, float radius) { return middle + radius * glm::vec2(std::cos(angle), std::sin(angle)); };
				switch (point.kind)
				{
				case PointOfInterest::Kind::SUMMIT:
					cairn(middle, 1.5f, 5, salt);
					for (int i = 0; i < 4; ++i)
					{
						float angle = TAU * (static_cast<float>(i) + layout.range(0.0f, 0.6f)) / 4.0f;
						Model model = layout.unit() < 0.5f ? Model::STONE_ROUND : Model::STONE_SHARP;
						prop(model, around(angle, layout.range(4.5f, 6.0f)), layout.range(0.0f, TAU), layout.range(1.2f, 2.0f), 0.7f, 0.25f, salt + 10u + i);
					}
					break;
				case PointOfInterest::Kind::CAMP:
					//A ring of stones around the fire with two half burnt logs in it, logs and stumps to sit on
					for (int i = 0; i < 10; ++i)
					{
						prop(Model::STONE_ROUND, around(TAU * static_cast<float>(i) / 10.0f, 0.9f), layout.range(0.0f, TAU), layout.range(0.26f, 0.34f), 0.6f, 0.3f, salt + 10u + i);
					}
					prop(Model::LOG, middle, layout.range(0.0f, TAU), 0.24f, 1.0f, 0.1f, salt + 30u);
					prop(Model::LOG, middle, layout.range(0.0f, TAU), 0.22f, 1.0f, -0.2f, salt + 31u);
					for (int i = 0; i < 3; ++i)
					{
						float angle = TAU * static_cast<float>(i) / 3.0f + layout.range(-0.15f, 0.15f);
						//Along the circle: its axis (x) is the tangent
						glm::vec2 tangent(-std::sin(angle), std::cos(angle));
						prop(Model::LOG, around(angle, 3.3f), std::atan2(-tangent.y, tangent.x), 0.55f, 1.0f, 0.1f, salt + 40u + i);
					}
					prop(Model::STUMP, around(TAU / 6.0f, 3.4f), layout.range(0.0f, TAU), 0.85f, 0.2f, 0.15f, salt + 50u);
					prop(Model::STUMP, around(TAU / 2.0f, 3.4f), layout.range(0.0f, TAU), 0.8f, 0.2f, 0.15f, salt + 51u);
					break;
				case PointOfInterest::Kind::STANDING_STONES:
					for (int i = 0; i < 7; ++i)
					{
						float angle = TAU * (static_cast<float>(i) + layout.range(-0.15f, 0.15f)) / 7.0f;
						prop(Model::STONE_STANDING, around(angle, 6.5f), layout.range(0.0f, TAU), layout.range(1.0f, 1.35f), 0.05f, 0.1f, salt + 10u + i, true);
					}
					prop(Model::STONE_FLAT, middle, layout.range(0.0f, TAU), 2.0f, 1.0f, 0.2f, salt + 20u);
					break;
				case PointOfInterest::Kind::BEACH:
					//Driftwood and stones washed up on the sand
					for (int i = 0; i < 3; ++i)
					{
						glm::vec2 at = middle + facing * layout.range(-1.0f, 4.0f) + across * layout.range(-6.0f, 6.0f);
						prop(Model::LOG, at, layout.range(0.0f, TAU), layout.range(0.8f, 1.3f), 1.0f, 0.15f, salt + 10u + i);
					}
					prop(Model::STUMP, middle + facing * 2.0f + across * layout.range(-5.0f, 5.0f), layout.range(0.0f, TAU), 1.0f, 0.6f, 0.2f, salt + 20u);
					for (int i = 0; i < 4; ++i)
					{
						glm::vec2 at = middle + facing * layout.range(-2.0f, 5.0f) + across * layout.range(-7.0f, 7.0f);
						prop(layout.unit() < 0.5f ? Model::STONE_ROUND : Model::STONE_FLAT, at, layout.range(0.0f, TAU), layout.range(0.5f, 1.1f), 0.8f, 0.3f, salt + 30u + i);
					}
					break;
				case PointOfInterest::Kind::CLIFF_VIEW:
					//A log to sit on, looking at the sea, and a cairn at the edge
					prop(Model::LOG, middle - facing * 1.5f, std::atan2(-across.y, across.x), 0.55f, 1.0f, 0.1f, salt + 10u);
					cairn(middle + facing * 2.5f, 1.0f, 4, salt + 20u);
					prop(Model::STONE_ROUND, middle + across * 4.5f, layout.range(0.0f, TAU), 1.6f, 0.7f, 0.25f, salt + 30u);
					prop(Model::STONE_SHARP, middle - across * 4.0f, layout.range(0.0f, TAU), 1.3f, 0.7f, 0.25f, salt + 31u);
					break;
				}
			}
			const std::vector<glm::vec3>& markers = landmarks.getMarkers();
			for (std::size_t m = 0; m < markers.size(); ++m)
			{
				cairn(glm::vec2(markers[m].x, markers[m].z), 0.75f, 3, WorldGenerator::SEED + 400u + static_cast<std::uint32_t>(m) * 7u);
			}
		}

		void scatterSmall()
		{
			const float seaLevel = world.getSeaLevel();
			//Grass: thinner close to the trail, none in the snow and on the sand
			forEachPoint(0.65f, 201u, [&](glm::vec2 point, noise::Random& random) {
				float meadow = 0.3f + 0.7f * smoothstep(-0.25f, 0.3f, noise::fbm(point * 0.03f, 2, WorldGenerator::SEED + 61u));
				if (random.unit() > meadow)
				{
					return; //before the (expensive) site
				}
				Site s;
				if (!site(point, s) || s.normal.y < 0.6f || s.position.y < seaLevel + 1.0f || s.material.sand > 0.15f || s.material.cave > 0.5f
					|| s.material.cliff > 0.5f || s.trailDistance < -0.6f || s.pathDistance < -0.5f)
				{
					return;
				}
				float nearTrail = smoothstep(-0.6f, 1.6f, std::min(s.trailDistance, s.pathDistance));
				if (random.unit() > nearTrail * (1.0f - 1.8f * s.snow) || !keepsClear(s.position, 0.3f))
				{
					return;
				}
				Model model = random.unit() < 0.3f + 0.4f * smoothstep(20.0f, 40.0f, s.position.y) ? Model::GRASS_DRY : Model::GRASS;
				add(model, s.position - glm::vec3(0.0f, 0.03f, 0.0f), orientation(random.range(0.0f, 6.2831853f), s.normal, 0.5f), random.range(0.7f, 1.3f), random);
			});
			//Ferns in the forest
			forEachPoint(2.6f, 202u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.forest < 0.45f || s.normal.y < 0.6f || s.trailDistance < 1.5f || s.pathDistance < 0.8f || s.clearing || s.snow > 0.3f || s.material.cave > 0.5f
					|| s.position.y < seaLevel + 2.0f || random.unit() > 0.35f * s.forest || !keepsClear(s.position, 0.8f))
				{
					return;
				}
				add(Model::FERN, s.position - glm::vec3(0.0f, 0.05f, 0.0f), orientation(random.range(0.0f, 6.2831853f), s.normal, 0.6f), random.range(0.6f, 1.2f), random);
			});
			//Mushrooms in small groups
			forEachPoint(7.0f, 203u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.forest < 0.4f || s.normal.y < 0.75f || s.trailDistance < 0.6f || s.pathDistance < 0.3f || s.material.cave > 0.5f || s.snow > 0.5f
					|| s.position.y < seaLevel + 2.0f || random.unit() > 0.4f)
				{
					return;
				}
				Model model = random.unit() < 0.35f ? Model::MUSHROOM_RED : Model::MUSHROOM_BROWN;
				int count = 2 + static_cast<int>(random.unit() * 5.0f);
				for (int i = 0; i < count; ++i)
				{
					float angle = random.range(0.0f, 6.2831853f), distance = random.range(0.1f, 1.0f);
					glm::vec2 spot = point + distance * glm::vec2(std::cos(angle), std::sin(angle));
					glm::vec3 position, normal;
					if (!inColumn(spot) || !world.surface(spot, position, normal) || !keepsClear(position, 0.2f))
					{
						continue;
					}
					add(model, position - glm::vec3(0.0f, 0.02f, 0.0f), orientation(random.range(0.0f, 6.2831853f), normal, 0.3f), random.range(0.6f, 1.3f), random);
				}
			});
			//Pebbles and small stones, also at the edges of the trail
			forEachPoint(3.2f, 204u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.normal.y < 0.55f || s.trailDistance < -0.8f || s.position.y < seaLevel - 0.5f
					|| random.unit() > 0.3f + 0.3f * s.material.cliff || !keepsClear(s.position, 0.5f))
				{
					return;
				}
				float choice = random.unit();
				Model model = choice < 0.4f ? Model::STONE_ROUND : choice < 0.7f ? Model::STONE_FLAT : Model::STONE_SHARP;
				float scale = random.range(0.15f, 0.5f);
				add(model, s.position - s.normal * (0.2f * scale), orientation(random.range(0.0f, 6.2831853f), s.normal, 0.8f), scale, random);
			});
			//Glowing mushrooms along the walls of the caves (on the floor under the roof)
			const TrackPath& path = world.getPath();
			for (std::size_t c = 0; c < world.getCaves().size(); ++c)
			{
				const Cave& cave = world.getCaves()[c];
				int steps = static_cast<int>((cave.end - cave.start - 4.0f) / 2.5f);
				for (int step = 0; step <= steps; ++step)
				{
					for (int side = -1; side <= 1; side += 2)
					{
						noise::Random random(noise::hash(static_cast<int>(c), step * 2 + (side > 0 ? 1 : 0), WorldGenerator::SEED + 205u));
						if (random.unit() > 0.55f)
						{
							continue;
						}
						TrackPath::Sample sample = path.at(cave.start + 2.0f + 2.5f * static_cast<float>(step) + random.range(-1.0f, 1.0f));
						glm::vec2 right(-sample.tangent.y, sample.tangent.x);
						float halfWidth = side > 0 ? sample.floorRight : sample.floorLeft;
						glm::vec2 center = glm::vec2(sample.position.x, sample.position.z) + right * (static_cast<float>(side) * (halfWidth - 0.3f + random.range(0.0f, 0.8f)));
						int count = 3 + static_cast<int>(random.unit() * 6.0f);
						for (int i = 0; i < count; ++i)
						{
							float angle = random.range(0.0f, 6.2831853f), distance = random.range(0.0f, 0.7f);
							glm::vec2 spot = center + distance * glm::vec2(std::cos(angle), std::sin(angle));
							if (!inColumn(spot))
							{
								continue;
							}
							//The floor under the roof: the height field
							const float E = 0.25f;
							float y = world.height(spot);
							glm::vec3 normal = glm::normalize(glm::vec3(world.height(spot - glm::vec2(E, 0.0f)) - world.height(spot + glm::vec2(E, 0.0f)), 2.0f * E,
								world.height(spot - glm::vec2(0.0f, E)) - world.height(spot + glm::vec2(0.0f, E))));
							if (y < seaLevel + 0.2f)
							{
								continue; //in a puddle
							}
							add(Model::MUSHROOM_GLOW, glm::vec3(spot.x, y - 0.02f, spot.y), orientation(random.range(0.0f, 6.2831853f), normal, 0.4f), random.range(1.2f, 2.6f), random);
						}
					}
				}
			}
		}

		std::vector<Instance> instances;

	private:
		const WorldGenerator& world;
		ColumnKey column;
		std::vector<physics::ShapePart>* collision;
		glm::vec2 origin;
	};
}

std::vector<Instance> bibo::world::scatter(const WorldGenerator& world, ColumnKey column, Layer layer, std::vector<physics::ShapePart>* collision)
{
	Scatterer scatterer(world, column, collision);
	if (layer == Layer::LARGE)
	{
		scatterer.scatterLarge();
		scatterer.scatterLandmarks();
	}
	else
	{
		scatterer.scatterSmall();
	}
	return std::move(scatterer.instances);
}

float bibo::world::modelHeight(Model model)
{
	return MODEL_SIZES[static_cast<int>(model)].height;
}

float bibo::world::modelRadius(Model model)
{
	return MODEL_SIZES[static_cast<int>(model)].radius;
}

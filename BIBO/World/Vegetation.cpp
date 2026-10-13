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
					|| s.material.cave > 0.5f || (s.material.cliff > 0.5f && s.normal.y < 0.85f) || random.unit() > 0.9f * s.forest)
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
					|| s.trailDistance < 1.6f || random.unit() > 0.3f * (0.4f + 0.6f * s.forest))
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
				if (!site(point, s) || s.onFloor || s.normal.y < 0.4f || s.position.y < seaLevel - 1.0f || s.material.cave > 0.5f)
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
					|| random.unit() > 0.38f || (s.track.found && std::abs(s.track.lateral) < s.track.floorHalfWidth + 3.0f))
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
					|| s.material.cliff > 0.5f || s.trailDistance < -0.6f)
				{
					return;
				}
				float nearTrail = smoothstep(-0.6f, 1.6f, s.trailDistance);
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
				if (!site(point, s) || s.forest < 0.45f || s.normal.y < 0.6f || s.trailDistance < 1.5f || s.snow > 0.3f || s.material.cave > 0.5f
					|| s.position.y < seaLevel + 2.0f || random.unit() > 0.35f * s.forest || !keepsClear(s.position, 0.8f))
				{
					return;
				}
				add(Model::FERN, s.position - glm::vec3(0.0f, 0.05f, 0.0f), orientation(random.range(0.0f, 6.2831853f), s.normal, 0.6f), random.range(0.6f, 1.2f), random);
			});
			//Mushrooms in small groups
			forEachPoint(7.0f, 203u, [&](glm::vec2 point, noise::Random& random) {
				Site s;
				if (!site(point, s) || s.forest < 0.4f || s.normal.y < 0.75f || s.trailDistance < 0.6f || s.material.cave > 0.5f || s.snow > 0.5f
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

#include "CreatureMeshes.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include "../World/MeshBuilder.hpp"

using namespace bibo;
using namespace bibo::scene;
using world::MeshBuilder;
using world::VegetationMaterial;
using world::rgb;

namespace
{
	const float PI = glm::pi<float>();

	/**
	* Ellipsoid made of rings of latitude, the color may change with the direction from the center
	*/
	template <typename ColorFunction>
	void addEllipsoid(MeshBuilder& builder, glm::vec3 center, glm::vec3 radii, int longitude, int latitude, VegetationMaterial material, ColorFunction color)
	{
		std::uint32_t first = static_cast<std::uint32_t>(builder.mesh.vertices.size());
		for (int i = 0; i <= latitude; ++i)
		{
			float polar = PI * static_cast<float>(i) / static_cast<float>(latitude);
			for (int j = 0; j <= longitude; ++j)
			{
				float azimuth = 2.0f * PI * static_cast<float>(j % longitude) / static_cast<float>(longitude);
				glm::vec3 direction(std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth));
				float occlusion = glm::mix(0.6f, 1.0f, 0.5f + 0.5f * direction.y); //darker underneath
				builder.add(center + direction * radii, direction / radii, glm::vec2(0.0f), color(direction), material, 0.0f, 0.0f, occlusion);
			}
		}
		for (int i = 0; i < latitude; ++i)
		{
			for (int j = 0; j < longitude; ++j)
			{
				std::uint32_t a = first + i * (longitude + 1) + j;
				std::uint32_t b = a + longitude + 1;
				builder.quad(a, b, b + 1, a + 1);
			}
		}
	}

	void addEllipsoid(MeshBuilder& builder, glm::vec3 center, glm::vec3 radii, int longitude, int latitude, VegetationMaterial material, glm::vec3 color)
	{
		addEllipsoid(builder, center, radii, longitude, latitude, material, [color](glm::vec3) { return color; });
	}

	//The salmon: half its height and width along its length (z from -0.5 at the tail to 0.5 at the snout)
	float salmonProfile(float z)
	{
		float u = std::clamp(z + 0.5f, 0.0f, 1.0f);
		float shape = std::pow(std::max(0.0f, std::sin(PI * std::pow(u, 0.8f))), 0.6f);
		return u < 0.5f ? std::max(shape, 0.22f) : shape;
	}
	const float SALMON_HEIGHT = 0.15f, SALMON_WIDTH = 0.09f;

	glm::vec3 salmonColor(glm::vec3 direction)
	{
		//Dark blue back, pink sides, white belly
		if (direction.y > 0.0f)
		{
			return glm::mix(rgb(238, 158, 142), rgb(58, 78, 98), glm::smoothstep(0.25f, 0.7f, direction.y));
		}
		return glm::mix(rgb(238, 158, 142), rgb(242, 240, 236), glm::smoothstep(0.1f, 0.6f, -direction.y));
	}

	/**
	* A stretch of the body of the salmon between two places along its length, with the pink flesh where it is cut
	* @param shift Added to the positions (the tail has its origin at its joint)
	*/
	void addSalmonPart(MeshBuilder& builder, float from, float to, bool capFrom, bool capTo, float shift)
	{
		const int SIDES = 14, RINGS = 10;
		std::uint32_t first = static_cast<std::uint32_t>(builder.mesh.vertices.size());
		for (int k = 0; k <= RINGS; ++k)
		{
			float z = glm::mix(from, to, static_cast<float>(k) / static_cast<float>(RINGS));
			float profile = salmonProfile(z);
			//Slope of the profile for the normals
			float slope = (salmonProfile(z + 0.01f) - salmonProfile(z - 0.01f)) / 0.02f;
			for (int j = 0; j <= SIDES; ++j)
			{
				float angle = 2.0f * PI * static_cast<float>(j % SIDES) / static_cast<float>(SIDES);
				glm::vec3 direction(std::cos(angle), std::sin(angle), 0.0f);
				glm::vec3 position(direction.x * SALMON_WIDTH * profile, direction.y * SALMON_HEIGHT * profile, z + shift);
				glm::vec3 normal(direction.x / SALMON_WIDTH, direction.y / SALMON_HEIGHT, 0.0f);
				normal = glm::normalize(normal) - glm::vec3(0.0f, 0.0f, slope * 0.6f);
				builder.add(position, normal, glm::vec2(0.0f), salmonColor(direction), world::VEGETATION_GLOSSY, 0.0f, 0.0f, glm::mix(0.75f, 1.0f, 0.5f + 0.5f * direction.y));
			}
		}
		for (int k = 0; k < RINGS; ++k)
		{
			for (int j = 0; j < SIDES; ++j)
			{
				std::uint32_t a = first + k * (SIDES + 1) + j;
				std::uint32_t b = a + SIDES + 1;
				builder.quad(a, b, b + 1, a + 1);
			}
		}
		auto cap = [&](float z, float facing) {
			float profile = salmonProfile(z);
			std::uint32_t middle = builder.add(glm::vec3(0.0f, 0.0f, z + shift), glm::vec3(0.0f, 0.0f, facing), glm::vec2(0.0f), rgb(246, 128, 92),
				world::VEGETATION_PLAIN, 0.0f, 0.0f, 0.9f);
			std::uint32_t ring[SIDES];
			for (int j = 0; j < SIDES; ++j)
			{
				float angle = 2.0f * PI * static_cast<float>(j) / static_cast<float>(SIDES);
				ring[j] = builder.add(glm::vec3(std::cos(angle) * SALMON_WIDTH * profile, std::sin(angle) * SALMON_HEIGHT * profile, z + shift),
					glm::vec3(0.0f, 0.0f, facing), glm::vec2(0.0f), rgb(236, 112, 80), world::VEGETATION_PLAIN, 0.0f, 0.0f, 0.9f);
			}
			for (int j = 0; j < SIDES; ++j)
			{
				builder.triangle(middle, ring[j], ring[(j + 1) % SIDES]);
			}
		};
		if (capFrom)
		{
			cap(from, -1.0f);
		}
		if (capTo)
		{
			cap(to, 1.0f);
		}
	}

	/**
	* A fin: flat triangles, seen from both sides
	*/
	void addFin(MeshBuilder& builder, std::initializer_list<glm::vec3> points, glm::vec3 color)
	{
		std::vector<std::uint32_t> indices;
		for (glm::vec3 point : points)
		{
			indices.push_back(builder.add(point, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f), color, world::VEGETATION_GLOSSY, 0.0f, 0.0f, 1.0f));
		}
		for (std::size_t i = 1; i + 1 < indices.size(); ++i)
		{
			builder.triangle(indices[0], indices[i], indices[i + 1]);
		}
	}

	/**
	* Exclamation mark: a stroke that gets thinner towards the bottom and a dot under it, glowing
	*/
	world::VegetationMesh buildQuestMarker(glm::vec3 color)
	{
		MeshBuilder builder;
		const glm::vec3 STROKE_CENTER(0.0f, 0.63f, 0.0f), STROKE_RADII(0.12f, 0.37f, 0.12f);
		std::uint32_t first = static_cast<std::uint32_t>(builder.mesh.vertices.size());
		const int LONGITUDE = 16, LATITUDE = 14;
		for (int i = 0; i <= LATITUDE; ++i)
		{
			float polar = PI * static_cast<float>(i) / static_cast<float>(LATITUDE);
			for (int j = 0; j <= LONGITUDE; ++j)
			{
				float azimuth = 2.0f * PI * static_cast<float>(j % LONGITUDE) / static_cast<float>(LONGITUDE);
				glm::vec3 direction(std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth));
				float taper = glm::mix(0.45f, 1.0f, 0.5f + 0.5f * direction.y);
				glm::vec3 radii = STROKE_RADII * glm::vec3(taper, 1.0f, taper);
				builder.add(STROKE_CENTER + direction * radii, direction / radii, glm::vec2(0.0f), color, world::VEGETATION_GLOW, 0.0f, 0.0f, 1.0f);
			}
		}
		for (int i = 0; i < LATITUDE; ++i)
		{
			for (int j = 0; j < LONGITUDE; ++j)
			{
				std::uint32_t a = first + i * (LONGITUDE + 1) + j;
				std::uint32_t b = a + LONGITUDE + 1;
				builder.quad(a, b, b + 1, a + 1);
			}
		}
		addEllipsoid(builder, glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.1f), 12, 8, world::VEGETATION_GLOW, color);
		return builder.finish();
	}

	world::VegetationMesh buildSalmonHead()
	{
		MeshBuilder builder;
		addSalmonPart(builder, SALMON_HEAD_JOINT, 0.5f, true, false, 0.0f);
		//Big round eyes
		const float EYE_Z = 0.38f;
		float profile = salmonProfile(EYE_Z);
		for (float side : { 1.0f, -1.0f })
		{
			glm::vec3 eye(side * SALMON_WIDTH * profile * 0.8f, SALMON_HEIGHT * profile * 0.22f, EYE_Z);
			addEllipsoid(builder, eye, glm::vec3(0.012f, 0.03f, 0.03f), 10, 6, world::VEGETATION_PLAIN, rgb(250, 250, 245));
			addEllipsoid(builder, eye + glm::vec3(side * 0.01f, 0.003f, 0.004f), glm::vec3(0.008f, 0.019f, 0.019f), 8, 5, world::VEGETATION_GLOSSY, rgb(15, 15, 20));
		}
		return builder.finish();
	}

	world::VegetationMesh buildSalmonBody()
	{
		MeshBuilder builder;
		addSalmonPart(builder, SALMON_TAIL_JOINT, SALMON_HEAD_JOINT, true, true, 0.0f);
		float top = SALMON_HEIGHT * salmonProfile(0.03f) * 0.95f;
		addFin(builder, { glm::vec3(0.0f, top, -0.08f), glm::vec3(0.0f, top + 0.085f, -0.04f), glm::vec3(0.0f, top + 0.02f, 0.12f) }, rgb(62, 82, 104));
		return builder.finish();
	}

	world::VegetationMesh buildSalmonTail()
	{
		MeshBuilder builder;
		const float shift = -SALMON_TAIL_JOINT;
		addSalmonPart(builder, -0.47f, SALMON_TAIL_JOINT, false, true, shift);
		//Forked tail fin
		addFin(builder, { glm::vec3(0.0f, 0.0f, -0.43f + shift), glm::vec3(0.0f, 0.15f, -0.6f + shift), glm::vec3(0.0f, 0.02f, -0.53f + shift),
			glm::vec3(0.0f, -0.02f, -0.53f + shift), glm::vec3(0.0f, -0.15f, -0.6f + shift) }, rgb(70, 88, 110));
		return builder.finish();
	}
}

world::VegetationMesh bibo::scene::buildCreatureModel(CreatureModel model)
{
	switch (model)
	{
	case CreatureModel::SALMON_HEAD:
		return buildSalmonHead();
	case CreatureModel::SALMON_BODY:
		return buildSalmonBody();
	case CreatureModel::SALMON_TAIL:
		return buildSalmonTail();
	case CreatureModel::DROPLET:
	{
		MeshBuilder builder;
		addEllipsoid(builder, glm::vec3(0.0f), glm::vec3(1.0f), 8, 6, world::VEGETATION_GLOSSY, rgb(215, 232, 245));
		return builder.finish();
	}
	case CreatureModel::QUEST_MARKER:
		return buildQuestMarker(rgb(255, 196, 36));
	case CreatureModel::QUEST_MARKER_REPEATABLE:
		return buildQuestMarker(rgb(70, 160, 255));
	case CreatureModel::COUNT:
		break;
	}
	return world::VegetationMesh();
}

#include "HedgehogModel.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include "../Resources/SkinnedModel.hpp"

using namespace bibo;
using namespace bibo::scene;
using resources::SkinnedModel;

namespace
{
	const float PI = glm::pi<float>();
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	const glm::vec3 BACKWARDS(0.0f, 0.0f, -1.0f);
	//The body is a surface around this point: an egg with a pointed snout that points forward and a bit down, flat underneath
	const glm::vec3 CENTER(0.0f, HEDGEHOG_BODY_HEIGHT, -0.02f);
	const glm::vec3 RADII(0.33f, 0.25f, 0.47f);
	const glm::vec3 SNOUT = glm::normalize(glm::vec3(0.0f, -0.22f, 1.0f));
	const float BELLY_HEIGHT = 0.03f;

	//Colors (linear)
	const glm::vec3 SPINE_COLOR(0.36f, 0.29f, 0.19f); //the light parts of the spines (the shader adds the dark band)
	const glm::vec3 FACE_COLOR(0.2f, 0.14f, 0.09f);
	const glm::vec3 BELLY_COLOR(0.24f, 0.19f, 0.13f);
	const glm::vec3 SNOUT_COLOR(0.11f, 0.075f, 0.055f);
	const glm::vec3 SKIN_COLOR(0.11f, 0.075f, 0.065f); //bare feet
	const glm::vec3 NOSE_COLOR(0.02f, 0.015f, 0.014f);
	const glm::vec3 CLAW_COLOR(0.08f, 0.065f, 0.05f);

	float smoothMin(float a, float b, float k)
	{
		float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
		return glm::mix(b, a, h) - k * h * (1.0f - h);
	}

	/**
	* Distance of the surface of the body from its center in a direction
	*/
	float bodyRadius(glm::vec3 direction)
	{
		//The head tapers to the snout (a teardrop seen from above and from the side)
		float taper = 1.0f - 0.45f * glm::smoothstep(0.2f, 0.95f, direction.z);
		float radius = 1.0f / glm::length(direction / (RADII * glm::vec3(taper, taper, 1.0f)));
		float snout = glm::smoothstep(0.86f, 1.0f, glm::dot(direction, SNOUT));
		radius += 0.16f * std::pow(snout, 1.5f);
		if (direction.y < 0.0f)
		{
			radius = smoothMin(radius, (CENTER.y - BELLY_HEIGHT) / std::max(-direction.y, 1e-3f), 0.04f);
		}
		return radius;
	}

	glm::vec3 bodyPoint(glm::vec3 direction)
	{
		return CENTER + direction * bodyRadius(direction);
	}

	/**
	* > 0 on the face, < 0 where the spines grow (they cover the forehead like a cap and reach further forward on top)
	*/
	float faceSide(glm::vec3 direction)
	{
		return direction.z - (0.5f + 0.3f * std::max(direction.y, 0.0f));
	}

	class Builder
	{
	public:
		HedgehogModel model;

		unsigned int add(glm::vec3 position, glm::vec3 normal, glm::vec4 color, SkinnedModel::Surface surface, int part, bool spine)
		{
			model.positions.push_back(position);
			model.normals.push_back(glm::length(normal) > 1e-6f ? glm::normalize(normal) : UP);
			model.colors.push_back(color);
			model.surfaces.push_back(static_cast<float>(surface));
			model.parts.push_back(part);
			model.combs.push_back(BACKWARDS);
			model.spines.push_back(spine);
			return static_cast<unsigned int>(model.positions.size() - 1);
		}

		/**
		* Rings of latitude around the y axis, the vertices come from a function of the direction
		* @param vertex Adds the vertex in a direction and returns its index
		* @return The first triangle (index into indices / 3)
		*/
		template <typename Vertex>
		std::size_t sphere(int longitude, int latitude, Vertex vertex)
		{
			std::size_t firstTriangle = model.indices.size() / 3;
			std::vector<unsigned int> grid;
			for (int i = 0; i <= latitude; ++i)
			{
				float polar = PI * static_cast<float>(i) / static_cast<float>(latitude);
				for (int j = 0; j <= longitude; ++j)
				{
					float azimuth = 2.0f * PI * static_cast<float>(j % longitude) / static_cast<float>(longitude);
					grid.push_back(vertex(glm::vec3(std::sin(polar) * std::sin(azimuth), std::cos(polar), std::sin(polar) * std::cos(azimuth))));
				}
			}
			for (int i = 0; i < latitude; ++i)
			{
				for (int j = 0; j < longitude; ++j)
				{
					unsigned int a = grid[i * (longitude + 1) + j], b = grid[(i + 1) * (longitude + 1) + j];
					unsigned int c = grid[(i + 1) * (longitude + 1) + j + 1], d = grid[i * (longitude + 1) + j + 1];
					triangle(a, b, c);
					triangle(a, c, d);
				}
			}
			return firstTriangle;
		}

		void ellipsoid(glm::vec3 center, glm::vec3 radii, int longitude, int latitude, glm::vec4 color, SkinnedModel::Surface surface, int part)
		{
			sphere(longitude, latitude, [&](glm::vec3 direction) {
				return add(center + direction * radii, direction / radii, color, surface, part, false);
			});
		}

		/**
		* The triangle faces outwards (where its vertex normals point)
		*/
		void triangle(unsigned int a, unsigned int b, unsigned int c)
		{
			const std::vector<glm::vec3>& p = model.positions;
			glm::vec3 face = glm::cross(p[b] - p[a], p[c] - p[a]);
			if (glm::dot(face, model.normals[a] + model.normals[b] + model.normals[c]) < 0.0f)
			{
				std::swap(b, c);
			}
			model.indices.insert(model.indices.end(), { a, b, c });
		}

		/**
		* Normals of the vertices from the triangles from firstTriangle on (for the body, whose shape is not an ellipsoid)
		*/
		void smoothNormals(std::size_t firstTriangle, std::size_t lastTriangle)
		{
			std::vector<glm::vec3> sums(model.positions.size(), glm::vec3(0.0f));
			std::vector<bool> used(model.positions.size(), false);
			for (std::size_t t = firstTriangle; t < lastTriangle; ++t)
			{
				unsigned int a = model.indices[t * 3], b = model.indices[t * 3 + 1], c = model.indices[t * 3 + 2];
				glm::vec3 face = glm::cross(model.positions[b] - model.positions[a], model.positions[c] - model.positions[a]);
				for (unsigned int v : { a, b, c })
				{
					sums[v] += face;
					used[v] = true;
				}
			}
			for (std::size_t v = 0; v < sums.size(); ++v)
			{
				if (used[v] && glm::length(sums[v]) > 1e-12f)
				{
					model.normals[v] = glm::normalize(sums[v]);
				}
			}
		}
	};
}

HedgehogModel HedgehogModel::build()
{
	Builder builder;

	//Body: spines on the back and the sides, short fur on the face and the belly
	std::size_t firstBodyTriangle = builder.sphere(72, 44, [&](glm::vec3 direction) {
		float face = faceSide(direction);
		bool belly = direction.y < -0.6f + 0.25f * std::max(direction.z, 0.0f);
		bool spine = face < 0.0f && !belly;
		glm::vec4 color;
		if (spine)
		{
			//Shorter at the edge of the spines
			float edge = glm::smoothstep(0.0f, 0.14f, -face) * glm::smoothstep(-0.6f, -0.35f, direction.y);
			color = glm::vec4(SPINE_COLOR, glm::mix(0.5f, 1.0f, edge));
		}
		else
		{
			float snout = glm::smoothstep(0.85f, 1.0f, glm::dot(direction, SNOUT));
			glm::vec3 fur = belly ? BELLY_COLOR : glm::mix(FACE_COLOR, SNOUT_COLOR, snout);
			color = glm::vec4(fur, belly ? 0.8f : glm::mix(0.75f, 0.2f, snout));
		}
		return builder.add(bodyPoint(direction), direction, color, SkinnedModel::FUR, BODY, spine);
	});
	std::size_t lastBodyTriangle = builder.model.indices.size() / 3;
	builder.smoothNormals(firstBodyTriangle, lastBodyTriangle);
	for (std::size_t t = firstBodyTriangle; t < lastBodyTriangle; ++t)
	{
		int spineVertices = 0;
		for (int k = 0; k < 3; ++k)
		{
			spineVertices += builder.model.spines[builder.model.indices[t * 3 + k]] ? 1 : 0;
		}
		std::vector<unsigned int>& target = spineVertices >= 2 ? builder.model.spineTriangles : builder.model.furTriangles;
		target.insert(target.end(), builder.model.indices.begin() + t * 3, builder.model.indices.begin() + t * 3 + 3);
	}

	//Wet nose at the tip of the snout
	glm::vec3 tip = bodyPoint(SNOUT);
	builder.ellipsoid(tip + SNOUT * 0.005f, glm::vec3(0.035f, 0.03f, 0.03f), 14, 10, glm::vec4(NOSE_COLOR, 0.0f), SkinnedModel::NOSE, BODY);

	//Small dark eyes (shaded like the eyes of the bear: the color is the direction in eye space, gaze along +z)
	for (float side : { 1.0f, -1.0f })
	{
		glm::vec3 direction = glm::normalize(glm::vec3(0.36f * side, 0.33f, 0.87f));
		glm::vec3 center = bodyPoint(direction) - direction * 0.012f;
		glm::vec3 gaze = glm::normalize(glm::vec3(0.55f * side, 0.05f, 0.83f));
		glm::vec3 x = glm::normalize(glm::cross(UP, gaze));
		glm::vec3 y = glm::cross(gaze, x);
		const float EYE_RADIUS = 0.026f;
		builder.sphere(16, 12, [&](glm::vec3 d) {
			glm::vec4 color(glm::vec3(glm::dot(d, x), glm::dot(d, y), glm::dot(d, gaze)) * 0.5f + 0.5f, 0.0f);
			return builder.add(center + d * EYE_RADIUS, d, color, SkinnedModel::EYE, BODY, false);
		});

		//Round ears just in front of the spines
		glm::vec3 earDirection = glm::normalize(glm::vec3(0.5f * side, 0.62f, 0.4f));
		glm::vec3 ear = bodyPoint(earDirection);
		std::size_t firstEarTriangle = builder.model.indices.size() / 3;
		builder.ellipsoid(ear, glm::vec3(0.045f, 0.05f, 0.022f), 12, 8, glm::vec4(FACE_COLOR * 1.1f, 0.12f), SkinnedModel::FUR, BODY);
		builder.model.furTriangles.insert(builder.model.furTriangles.end(), builder.model.indices.begin() + firstEarTriangle * 3, builder.model.indices.end());
	}

	//Feet with bare soles and dark claws, each in the space of its foot
	for (int part = FOOT_FRONT_LEFT; part <= FOOT_HIND_RIGHT; ++part)
	{
		builder.ellipsoid(glm::vec3(0.0f, 0.025f, 0.0f), glm::vec3(0.042f, 0.027f, 0.058f), 14, 8, glm::vec4(SKIN_COLOR, 0.0f), SkinnedModel::FUR, part);
		for (int claw = 0; claw < 4; ++claw)
		{
			float x = (static_cast<float>(claw) - 1.5f) * 0.02f;
			builder.ellipsoid(glm::vec3(x, 0.01f, 0.06f), glm::vec3(0.006f, 0.006f, 0.018f), 6, 4, glm::vec4(CLAW_COLOR, 0.0f), SkinnedModel::CLAW, part);
		}
	}
	return builder.model;
}

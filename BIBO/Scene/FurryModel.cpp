#include "FurryModel.hpp"
#include <algorithm>
#include <cmath>

using namespace bibo;
using namespace bibo::scene;
using resources::SkinnedModel;

unsigned int FurryModelBuilder::add(glm::vec3 position, glm::vec3 normal, glm::vec4 color, SkinnedModel::Surface surface, int part, bool spine, glm::vec3 comb)
{
	model.positions.push_back(position);
	model.normals.push_back(glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f));
	model.colors.push_back(color);
	model.surfaces.push_back(static_cast<float>(surface));
	model.parts.push_back(part);
	model.combs.push_back(glm::length(comb) > 1e-6f ? glm::normalize(comb) : glm::vec3(0.0f, 0.0f, -1.0f));
	model.spines.push_back(spine);
	return static_cast<unsigned int>(model.positions.size() - 1);
}

std::size_t FurryModelBuilder::ellipsoid(glm::vec3 center, glm::vec3 radii, int longitude, int latitude, glm::vec4 color, SkinnedModel::Surface surface, int part)
{
	return sphere(longitude, latitude, [&](glm::vec3 direction) {
		return add(center + direction * radii, direction / radii, color, surface, part);
	});
}

void FurryModelBuilder::triangle(unsigned int a, unsigned int b, unsigned int c)
{
	const std::vector<glm::vec3>& p = model.positions;
	glm::vec3 face = glm::cross(p[b] - p[a], p[c] - p[a]);
	if (glm::dot(face, model.normals[a] + model.normals[b] + model.normals[c]) < 0.0f)
	{
		std::swap(b, c);
	}
	model.indices.insert(model.indices.end(), { a, b, c });
}

void FurryModelBuilder::smoothNormals(std::size_t firstTriangle, std::size_t lastTriangle)
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

void FurryModelBuilder::growFur(std::size_t firstTriangle, std::size_t lastTriangle)
{
	model.furTriangles.insert(model.furTriangles.end(), model.indices.begin() + firstTriangle * 3, model.indices.begin() + lastTriangle * 3);
}

void FurryModelBuilder::growFurAndSpines(std::size_t firstTriangle, std::size_t lastTriangle)
{
	for (std::size_t t = firstTriangle; t < lastTriangle; ++t)
	{
		int spineVertices = 0;
		for (int k = 0; k < 3; ++k)
		{
			spineVertices += model.spines[model.indices[t * 3 + k]] ? 1 : 0;
		}
		std::vector<unsigned int>& target = spineVertices >= 2 ? model.spineTriangles : model.furTriangles;
		target.insert(target.end(), model.indices.begin() + t * 3, model.indices.begin() + t * 3 + 3);
	}
}

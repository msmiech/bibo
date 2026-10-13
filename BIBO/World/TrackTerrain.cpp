#include "TrackTerrain.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <assimp/scene.h>

using namespace bibo::world;

TrackTerrain::TrackTerrain(const std::vector<glm::vec3>& vertices, const std::vector<unsigned int>& indices)
{
	glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
	for (const glm::vec3& vertex : vertices)
	{
		low = glm::min(low, vertex);
		high = glm::max(high, vertex);
	}
	if (vertices.empty())
	{
		low = high = glm::vec3(0.0f);
	}
	origin = glm::vec2(low.x, low.z);
	columns = static_cast<int>((high.x - low.x) / CELL_SIZE) + 2;
	rows = static_cast<int>((high.z - low.z) / CELL_SIZE) + 2;
	const float EMPTY = std::numeric_limits<float>::lowest();
	heights.assign(static_cast<std::size_t>(columns) * rows, EMPTY);

	//Highest triangle above the center of every cell
	for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
	{
		glm::vec3 a = vertices[indices[t]], b = vertices[indices[t + 1]], c = vertices[indices[t + 2]];
		float determinant = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
		if (std::abs(determinant) < 1e-9f)
		{
			continue; //vertical or degenerate
		}
		int i0 = std::max(0, static_cast<int>(std::ceil((std::min({ a.x, b.x, c.x }) - origin.x) / CELL_SIZE)));
		int i1 = std::min(columns - 1, static_cast<int>(std::floor((std::max({ a.x, b.x, c.x }) - origin.x) / CELL_SIZE)));
		int j0 = std::max(0, static_cast<int>(std::ceil((std::min({ a.z, b.z, c.z }) - origin.y) / CELL_SIZE)));
		int j1 = std::min(rows - 1, static_cast<int>(std::floor((std::max({ a.z, b.z, c.z }) - origin.y) / CELL_SIZE)));
		for (int j = j0; j <= j1; ++j)
		{
			for (int i = i0; i <= i1; ++i)
			{
				float x = origin.x + i * CELL_SIZE, z = origin.y + j * CELL_SIZE;
				float l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / determinant;
				float l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / determinant;
				float l3 = 1.0f - l1 - l2;
				if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f)
				{
					continue;
				}
				float& cell = heights[static_cast<std::size_t>(j) * columns + i];
				cell = std::max(cell, l1 * a.y + l2 * b.y + l3 * c.y);
			}
		}
	}

	//The ragged border of the mesh leaves some cells empty: they get the average of their filled neighbors
	bool changed = true;
	while (changed)
	{
		changed = false;
		std::vector<float> filled = heights;
		for (int j = 0; j < rows; ++j)
		{
			for (int i = 0; i < columns; ++i)
			{
				if (heights[static_cast<std::size_t>(j) * columns + i] != EMPTY)
				{
					continue;
				}
				float sum = 0.0f;
				int count = 0;
				for (int dj = -1; dj <= 1; ++dj)
				{
					for (int di = -1; di <= 1; ++di)
					{
						int ni = i + di, nj = j + dj;
						if (ni >= 0 && nj >= 0 && ni < columns && nj < rows && heights[static_cast<std::size_t>(nj) * columns + ni] != EMPTY)
						{
							sum += heights[static_cast<std::size_t>(nj) * columns + ni];
							++count;
						}
					}
				}
				if (count > 0)
				{
					filled[static_cast<std::size_t>(j) * columns + i] = sum / static_cast<float>(count);
					changed = true;
				}
			}
		}
		heights.swap(filled);
	}

	minimum = glm::vec2(low.x, low.z) + glm::vec2(BORDER_MARGIN);
	maximum = glm::vec2(high.x, high.z) - glm::vec2(BORDER_MARGIN);
	lowest = std::numeric_limits<float>::max();
	highest = std::numeric_limits<float>::lowest();
	for (float h : heights)
	{
		lowest = std::min(lowest, h == EMPTY ? lowest : h);
		highest = std::max(highest, h);
	}
	if (heights.empty() || highest == EMPTY)
	{
		lowest = highest = 0.0f;
	}

	//Blurred copy (box filter along both axes, twice: close to a Gaussian)
	smoothHeights = heights;
	std::vector<float> line;
	const int radius = static_cast<int>(SMOOTH_RADIUS / CELL_SIZE);
	for (int pass = 0; pass < 2; ++pass)
	{
		for (int axis = 0; axis < 2; ++axis)
		{
			int count = axis == 0 ? columns : rows, lines = axis == 0 ? rows : columns;
			line.resize(count);
			for (int l = 0; l < lines; ++l)
			{
				auto cell = [&](int k) -> float& {
					return axis == 0 ? smoothHeights[static_cast<std::size_t>(l) * columns + k] : smoothHeights[static_cast<std::size_t>(k) * columns + l];
				};
				//Running sum, the border cells are repeated
				float sum = 0.0f;
				for (int k = -radius - 1; k < radius; ++k)
				{
					sum += cell(std::clamp(k, 0, count - 1));
				}
				for (int k = 0; k < count; ++k)
				{
					sum += cell(std::min(k + radius, count - 1)) - cell(std::max(k - radius - 1, 0));
					line[k] = sum / static_cast<float>(2 * radius + 1);
				}
				for (int k = 0; k < count; ++k)
				{
					cell(k) = line[k];
				}
			}
		}
	}
}

bool TrackTerrain::loadNodeMesh(const aiScene& scene, const char* nodeName, std::vector<glm::vec3>& vertices, std::vector<unsigned int>& indices)
{
	const aiNode* node = scene.mRootNode ? scene.mRootNode->FindNode(nodeName) : nullptr;
	if (!node || node->mNumMeshes == 0)
	{
		return false;
	}
	//Global transformation: the transformations of all parents (like resources::Model::transformNode)
	aiMatrix4x4 transformation = node->mTransformation;
	for (const aiNode* parent = node->mParent; parent; parent = parent->mParent)
	{
		transformation = parent->mTransformation * transformation;
	}
	vertices.clear();
	indices.clear();
	for (unsigned int m = 0; m < node->mNumMeshes; ++m)
	{
		const aiMesh* mesh = scene.mMeshes[node->mMeshes[m]];
		unsigned int first = static_cast<unsigned int>(vertices.size());
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
		{
			aiVector3D v = transformation * mesh->mVertices[i];
			vertices.emplace_back(v.x, v.y, v.z);
		}
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
		{
			const aiFace& face = mesh->mFaces[f];
			if (face.mNumIndices == 3)
			{
				indices.insert(indices.end(), { first + face.mIndices[0], first + face.mIndices[1], first + face.mIndices[2] });
			}
		}
	}
	return !indices.empty();
}

bool TrackTerrain::nodeSphere(const aiScene& scene, const char* nodeName, glm::vec3& center, float& radius)
{
	std::vector<glm::vec3> vertices;
	std::vector<unsigned int> indices;
	if (!loadNodeMesh(scene, nodeName, vertices, indices))
	{
		return false;
	}
	glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
	for (const glm::vec3& vertex : vertices)
	{
		low = glm::min(low, vertex);
		high = glm::max(high, vertex);
	}
	center = 0.5f * (low + high);
	radius = 0.0f;
	for (const glm::vec3& vertex : vertices)
	{
		radius = std::max(radius, glm::distance(center, vertex));
	}
	return true;
}

float TrackTerrain::bilinear(const std::vector<float>& grid, glm::vec2 position) const
{
	if (grid.empty())
	{
		return 0.0f;
	}
	float x = std::clamp((position.x - origin.x) / CELL_SIZE, 0.0f, static_cast<float>(columns - 1) - 1e-3f);
	float z = std::clamp((position.y - origin.y) / CELL_SIZE, 0.0f, static_cast<float>(rows - 1) - 1e-3f);
	int i = static_cast<int>(x), j = static_cast<int>(z);
	float fx = x - static_cast<float>(i), fz = z - static_cast<float>(j);
	int i1 = std::min(i + 1, columns - 1), j1 = std::min(j + 1, rows - 1);
	auto at = [&](int column, int row) { return grid[static_cast<std::size_t>(row) * columns + column]; };
	return (at(i, j) * (1.0f - fx) + at(i1, j) * fx) * (1.0f - fz) + (at(i, j1) * (1.0f - fx) + at(i1, j1) * fx) * fz;
}

float TrackTerrain::height(glm::vec2 position) const
{
	return bilinear(heights, position);
}

float TrackTerrain::smoothHeight(glm::vec2 position) const
{
	return bilinear(smoothHeights, position);
}

float TrackTerrain::distanceInside(glm::vec2 position) const
{
	//Signed distance of a rectangle with rounded corners: the rectangle shrunk by the radius, grown by it again
	glm::vec2 middle = 0.5f * (minimum + maximum);
	glm::vec2 half = 0.5f * (maximum - minimum) - glm::vec2(CORNER_RADIUS);
	glm::vec2 q = glm::abs(position - middle) - half;
	float outside = glm::length(glm::max(q, glm::vec2(0.0f))) + std::min(std::max(q.x, q.y), 0.0f) - CORNER_RADIUS;
	return -outside;
}

glm::vec2 TrackTerrain::clampInside(glm::vec2 position) const
{
	glm::vec2 middle = 0.5f * (minimum + maximum);
	glm::vec2 half = 0.5f * (maximum - minimum) - glm::vec2(CORNER_RADIUS);
	glm::vec2 offset = position - middle;
	glm::vec2 core = glm::clamp(offset, -half, half);
	glm::vec2 fromCore = offset - core;
	float length = glm::length(fromCore);
	if (length > CORNER_RADIUS)
	{
		fromCore *= CORNER_RADIUS / length;
	}
	return middle + core + fromCore;
}

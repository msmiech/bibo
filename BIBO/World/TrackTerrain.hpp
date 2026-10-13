#pragma once
#include <vector>
#include <glm/glm.hpp>

struct aiScene;

namespace bibo
{
	namespace world
	{
		/**
		* The terrain of the race track (the node "Boden" of the track file) as a height field: the height of the surface
		* for every point of the ground plane. The triangles are rasterized once (the terrain has no overhangs), afterwards
		* the height of any point is a bilinear lookup. The world is built around it, so the track stays exactly as it was.
		*/
		class TrackTerrain
		{
		public:
			static constexpr float CELL_SIZE = 0.5f;
			//The outermost border of the mesh is ragged, the usable rectangle starts this far inside
			static constexpr float BORDER_MARGIN = 4.0f;

			/**
			* @param vertices Triangle mesh in world space (y up)
			* @param indices 3 per triangle
			*/
			TrackTerrain(const std::vector<glm::vec3>& vertices, const std::vector<unsigned int>& indices);

			/**
			* Vertices and triangles of a node of the scene in world space (with the transformation of the node, like the game draws it)
			* @return false if the node does not exist or has no mesh
			*/
			static bool loadNodeMesh(const aiScene& scene, const char* nodeName, std::vector<glm::vec3>& vertices, std::vector<unsigned int>& indices);
			/**
			* Sphere around the meshes of a node of the scene (world space)
			* @return false if the node does not exist or has no mesh
			*/
			static bool nodeSphere(const aiScene& scene, const char* nodeName, glm::vec3& center, float& radius);

			/**
			* Height of the surface (bilinear between the cells, clamped to the rectangle of the terrain)
			*/
			float height(glm::vec2 position) const;
			/**
			* Height of the terrain blurred over about 2 * SMOOTH_RADIUS (for continuing the terrain outside of its border)
			*/
			float smoothHeight(glm::vec2 position) const;
			/**
			* @return Distance from the border of the usable rectangle with rounded corners, negative outside of it
			*/
			float distanceInside(glm::vec2 position) const;
			/**
			* @return The closest point inside of the usable rectangle with rounded corners
			*/
			glm::vec2 clampInside(glm::vec2 position) const;
			static constexpr float CORNER_RADIUS = 60.0f;
			static constexpr float SMOOTH_RADIUS = 10.0f;
			glm::vec2 getMinimum() const { return minimum; }
			glm::vec2 getMaximum() const { return maximum; }
			glm::vec2 getCenter() const { return 0.5f * (minimum + maximum); }
			float getLowest() const { return lowest; }
			float getHighest() const { return highest; }

			//Raw grid (for the path finding)
			int getColumns() const { return columns; }
			int getRows() const { return rows; }
			glm::vec2 getOrigin() const { return origin; }
			float heightAt(int column, int row) const { return heights[static_cast<std::size_t>(row) * columns + column]; }

		private:
			float bilinear(const std::vector<float>& grid, glm::vec2 position) const;

			glm::vec2 origin; //position of the cell (0, 0)
			int columns = 0, rows = 0;
			std::vector<float> heights;
			std::vector<float> smoothHeights;
			glm::vec2 minimum, maximum; //usable rectangle
			float lowest = 0.0f, highest = 0.0f;
		};
	}
}

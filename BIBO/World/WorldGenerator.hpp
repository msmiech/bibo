#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Landmarks.hpp"
#include "TrackPath.hpp"
#include "TrackTerrain.hpp"

namespace bibo
{
	namespace world
	{
		struct Box
		{
			glm::vec3 minimum = glm::vec3(0.0f), maximum = glm::vec3(0.0f);
			bool intersects(const Box& other) const
			{
				return glm::all(glm::lessThanEqual(minimum, other.maximum)) && glm::all(glm::lessThanEqual(other.minimum, maximum));
			}
			bool contains(glm::vec3 point) const
			{
				return glm::all(glm::lessThanEqual(minimum, point)) && glm::all(glm::lessThanEqual(point, maximum));
			}
		};

		/**
		* Something of the old track that the new world must not cover, e.g. the finish line or a sign
		*/
		struct KeepClear
		{
			glm::vec3 center;
			float radius;
		};

		/**
		* A cave: a roof of rock over the track between two distances along it
		*/
		struct Cave
		{
			float start = 0.0f, end = 0.0f; //distance along the track
			float floorHeight = 0.0f; //average height of the floor
			float ceilingHeight = 0.0f; //of the roof above the floor (in the middle)
			float roofHeight = 0.0f; //top of the roof above the floor
			Box bounds; //everything of the roof is inside
		};

		/**
		* What a point of the surface is covered with (the shader adds snow, rock on steep slopes and the forest floor)
		*/
		struct SurfaceMaterial
		{
			float trail = 0.0f; //the dirt trail of the race track and of the foot paths
			float sand = 0.0f; //beach
			float cave = 0.0f; //inside of a cave (bare rock, no snow)
			float cliff = 0.0f; //rocky coast
		};

		/**
		* The island: a signed density field that is positive inside of the ground (rock, earth) and negative in the air.
		* Its surface (density 0) is the terrain, which can have overhangs and caves (a voxel world, the chunks are meshed from it).
		*
		* In the middle the density is the height field of the race track, so the track stays exactly as it was.
		* Around it the forest goes on over hills down to the coast, which has beaches and cliffs that go into the sea.
		* Some stretches of the track go through caves. Everything is computed from the position (deterministic), so any part
		* of the world can be generated at any time in any thread.
		*/
		class WorldGenerator
		{
		public:
			static constexpr float SEA_FLOOR = -26.0f;
			static constexpr std::uint32_t SEED = 1337u;

			struct Description
			{
				std::unique_ptr<TrackTerrain> track;
				glm::vec2 start = glm::vec2(0.0f); //start and finish of the lap
				glm::vec2 startDirection = glm::vec2(0.0f, -1.0f);
				std::vector<glm::vec2> checkpoints; //in driving direction
				std::vector<KeepClear> keepClear;
				float seaLevel = 0.0f;
			};

			explicit WorldGenerator(Description description);
			~WorldGenerator();

			/**
			* @return Positive inside of the ground, negative in the air, about the distance to the surface (exactly for flat ground)
			*/
			float density(glm::vec3 position) const;
			/**
			* Density at count points above each other, at (x, (firstIndex + i) * step, z): the same values as density() at these
			* points, but faster (what does not depend on the height is computed once)
			*/
			void densityColumn(float x, float z, int firstIndex, float step, int count, float* densities) const;
			/**
			* @return Normal of the surface (pointing into the air) at or close to the position
			*/
			glm::vec3 normal(glm::vec3 position) const;
			/**
			* Height of the ground without caves and overhangs (the height field the world is made of), with the foot paths
			*/
			float height(glm::vec2 position) const;
			/**
			* Height of the ground before the foot paths are cut into it and the clearings are leveled
			*/
			float naturalHeight(glm::vec2 position) const;
			/**
			* Finds the highest surface at a point of the ground plane (e.g. on the roof of a cave, not in it)
			* @return false if there is no ground there
			*/
			bool topSurface(glm::vec2 position, glm::vec3& surface, glm::vec3& surfaceNormal) const;
			/**
			* Like topSurface, but faster where the ground is a height field (no cave, no cliff)
			*/
			bool surface(glm::vec2 position, glm::vec3& surface, glm::vec3& surfaceNormal) const;
			/**
			* How much snow lies on the ground (patches, more on the mountains, none on the trail, in caves and on the beach)
			* @param skyVisibility See skyVisibility (snow needs an open sky)
			*/
			float snowCover(glm::vec3 position, glm::vec3 surfaceNormal, float skyVisibility, const SurfaceMaterial& material) const;
			/**
			* @return The top of the cave roof above the point, or a large negative number if there is no cave
			*/
			float caveRoofTop(glm::vec2 position) const;
			/**
			* @return true if the point is under the roof of a cave
			*/
			bool isInCave(glm::vec3 position) const;
			SurfaceMaterial material(glm::vec3 position, glm::vec3 surfaceNormal) const;
			/**
			* @return How much of the sky the point sees (0 in a closed cave, 1 on open flat ground), from the density around it
			*/
			float skyVisibility(glm::vec3 position, glm::vec3 surfaceNormal) const;

			/**
			* Distance from the coast (positive inland) and how rocky the coast is there (0 beach, 1 cliff)
			*/
			void coast(glm::vec2 position, float& inland, float& cliff) const;

			/**
			* Everything that can have ground (the island and the sea floor around it)
			*/
			Box getBounds() const { return bounds; }
			/**
			* Lowest and highest density 0 in a box (plus a margin), so that chunks completely in the air or in the ground can be skipped
			* @return false if the box surely contains no surface
			*/
			bool mayContainSurface(const Box& box) const;
			float getSeaLevel() const { return seaLevel; }
			glm::vec2 getCenter() const { return center; }
			const TrackTerrain& getTrack() const { return *track; }
			const TrackPath& getPath() const { return *path; }
			const std::vector<Cave>& getCaves() const { return caves; }
			const std::vector<KeepClear>& getKeepClear() const { return keepClear; }
			/**
			* The points of interest and the foot paths to them
			*/
			const Landmarks& getLandmarks() const { return *landmarks; }
			/**
			* Where the mountains are highest (about)
			*/
			std::vector<glm::vec2> getMountainPeaks() const;

		private:
			/**
			* What the density of a vertical column of points needs (independent of the height)
			*/
			struct Column
			{
				float height = 0.0f;
				float inland = 0.0f, cliff = 0.0f;
				bool rugged = false; //cliff noise
				struct Roof
				{
					float ceiling, top, side, ends; //without the 3D noise
					float bottom, upper; //heights of the bounds of the cave
				};
				Roof roofs[4];
				int roofCount = 0;
			};
			Column column(glm::vec2 position) const;
			float densityIn(const Column& column, glm::vec3 position) const;
			float proceduralHeight(glm::vec2 position) const;
			float coastRadius(glm::vec2 direction) const;
			float cliffNoise(glm::vec3 position, float inland, float cliff) const;
			void chooseCaves();
			void computeColumnRanges();

			std::unique_ptr<TrackTerrain> track;
			std::unique_ptr<TrackPath> path;
			std::vector<Cave> caves;
			std::unique_ptr<Landmarks> landmarks; //null while they are made (the height is the natural height then)
			std::vector<KeepClear> keepClear;
			float seaLevel;
			glm::vec2 center;
			Box bounds;

			//Lowest and highest ground in coarse columns of the bounds (for mayContainSurface)
			static constexpr float RANGE_CELL = 16.0f;
			int rangeColumns = 0, rangeRows = 0;
			std::vector<glm::vec2> columnRanges;
		};
	}
}

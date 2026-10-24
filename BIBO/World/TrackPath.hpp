#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "TrackTerrain.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* The center line of the race track: a closed loop along the floor of the canyon of the track terrain.
		* It is found in the terrain (not stored in the track file): the cheapest way through the checkpoints in their order,
		* where it is cheap to go along the floor of the canyon and far from its walls. The world uses it for the trail,
		* the caves and to keep the trees off the track.
		*/
		class TrackPath
		{
		public:
			struct Sample
			{
				glm::vec3 position; //on the floor, in the middle of the canyon
				glm::vec2 tangent; //direction of the track in the ground plane
				float distance = 0.0f; //along the track from the start
				float floorLeft = 0.0f, floorRight = 0.0f; //width of the flat floor to the left and right of the center
				float wallLeft = 0.0f, wallRight = 0.0f; //height of the walls above the floor (highest point close to the track)
				float trailOffset = 0.0f; //the trail winds a bit on the floor (and around the pools): offset of its middle to the right
			};

			/**
			* What a point of the ground plane is next to
			*/
			struct Nearest
			{
				float distance = 0.0f; //along the track
				float lateral = 0.0f; //from the center line, positive to the right (of the driving direction)
				float trailLateral = 0.0f; //from the middle of the trail
				float floorHalfWidth = 0.0f; //width of the floor on the side of the point
				float floorHeight = 0.0f;
				bool found = false; //false if the point is far from the track (further than SEARCH_RADIUS)
			};

			static constexpr float SAMPLE_SPACING = 1.0f;
			static constexpr float TRAIL_HALF_WIDTH = 2.3f;
			//Points further from the track are not looked up exactly
			static constexpr float SEARCH_RADIUS = 48.0f;

			/**
			* @param start Start (and finish) of the lap
			* @param startDirection Driving direction at the start
			* @param waypoints Points on the track in driving direction (the checkpoints)
			* @param waterLevel Below it the floor is under water (the pools on the track): the trail goes around them where it can
			*/
			TrackPath(const TrackTerrain& terrain, glm::vec2 start, glm::vec2 startDirection, const std::vector<glm::vec2>& waypoints, float waterLevel);

			const std::vector<Sample>& getSamples() const { return samples; }
			float getLength() const { return length; }
			/**
			* Interpolated sample at a distance along the track (wraps around)
			*/
			Sample at(float distance) const;
			Nearest nearest(glm::vec2 position) const;

		private:
			void measure(const TrackTerrain& terrain, Sample& sample) const;
			/**
			* @return true if the trail with the offset (and half width) is out of the water at the sample
			*/
			bool dryTrail(const TrackTerrain& terrain, float waterLevel, const Sample& sample, float offset, float halfWidth) const;
			void avoidWater(const TrackTerrain& terrain, float waterLevel, std::vector<float>& offsets) const;
			void buildBuckets();

			std::vector<Sample> samples;
			float length = 0.0f;

			//Segments of the center line sorted into a grid for the nearest point queries
			static constexpr float BUCKET_SIZE = 16.0f;
			glm::vec2 bucketOrigin;
			int bucketColumns = 0, bucketRows = 0;
			std::vector<std::vector<int>> buckets; //indices of the segments (from sample i to i + 1) that come close
		};
	}
}

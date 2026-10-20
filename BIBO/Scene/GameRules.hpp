#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

namespace bibo
{
	namespace scene
	{
		/**
		* Win and lose conditions of the race: a lap is complete when the bear reaches the finish line after it passed
		* all checkpoints along the track in their order. The bear starts on the finish line.
		*/
		namespace rules
		{
			//The bear fell off the track (e.g. into the water) when it is below this height
			const float FALL_OFF_HEIGHT = -1.1f;

			inline bool hasFallenOff(glm::vec3 playerPosition)
			{
				return playerPosition.y < FALL_OFF_HEIGHT;
			}

			/**
			* Checkpoints along the track in driving direction (x and z in world coordinates, where the salmon of the model are placed).
			* Going backwards from the start reaches them in the wrong order, so the bear has to go around the whole track.
			*/
			inline const glm::vec2 CHECKPOINTS[] = {
				glm::vec2(51.2f, -35.7f),
				glm::vec2(6.0f, -58.6f),
				glm::vec2(-7.8f, -101.6f),
				glm::vec2(-59.4f, -71.2f),
				glm::vec2(-66.0f, 1.8f),
				glm::vec2(-55.3f, 76.9f),
				glm::vec2(31.3f, 77.1f),
				glm::vec2(80.7f, 11.6f)
			};
			const std::size_t CHECKPOINT_COUNT = sizeof(CHECKPOINTS) / sizeof(CHECKPOINTS[0]);
			//More than half the width of the track, so the bear cannot go around a checkpoint
			const float CHECKPOINT_RADIUS = 12.0f;

			/**
			* @param nextCheckpoint Index of the checkpoint the bear has to pass next (CHECKPOINT_COUNT: all passed)
			* @return The new index: one more if the bear is at the next checkpoint (in the xz plane, it may also jump over it)
			*/
			inline std::size_t passCheckpoint(glm::vec3 playerPosition, std::size_t nextCheckpoint)
			{
				if (nextCheckpoint < CHECKPOINT_COUNT &&
					glm::distance(glm::vec2(playerPosition.x, playerPosition.z), CHECKPOINTS[nextCheckpoint]) <= CHECKPOINT_RADIUS)
				{
					return nextCheckpoint + 1;
				}
				return nextCheckpoint;
			}

			/**
			* Invisible trigger at the finish line: as wide as the finish line board and deeper than it,
			* so that the bear cannot step over it in one frame
			*/
			struct FinishLine
			{
				glm::vec3 center;
				glm::vec3 across; //horizontal direction along the finish line, across the track
				float halfWidth = 0.0f;
			};
			const float FINISH_LINE_HALF_DEPTH = 1.5f;

			/**
			* @param modelMatrix Model matrix of the finish line board (Geometry), which goes along its x axis across the track
			* @param halfLength Half the length of the mesh of the board along its x axis (model space)
			*/
			inline FinishLine finishLine(const glm::mat4& modelMatrix, float halfLength)
			{
				glm::vec3 across = glm::vec3(modelMatrix[0]);
				FinishLine line;
				line.center = glm::vec3(modelMatrix[3]);
				line.halfWidth = halfLength * glm::length(across);
				line.across = glm::normalize(glm::vec3(across.x, 0.0f, across.z));
				return line;
			}

			inline bool isOnFinishLine(glm::vec3 playerPosition, const FinishLine& line)
			{
				glm::vec3 offset = playerPosition - line.center;
				offset.y = 0.0f; //the bear may also jump over it
				float acrossDistance = glm::dot(offset, line.across);
				float alongDistance = glm::length(offset - acrossDistance * line.across);
				return std::abs(acrossDistance) <= line.halfWidth && alongDistance <= FINISH_LINE_HALF_DEPTH;
			}

			inline bool hasCompletedLap(std::size_t checkpointsPassed, glm::vec3 playerPosition, const FinishLine& line)
			{
				return checkpointsPassed == CHECKPOINT_COUNT && isOnFinishLine(playerPosition, line);
			}

			//Bonus points for a salmon that the bear catches (one per pool of water on the track)
			const int SALMON_POINTS = 100;
		}
	}
}

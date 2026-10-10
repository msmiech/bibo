#pragma once
#include <glm/glm.hpp>

namespace bibo
{
	namespace scene
	{
		/**
		* Win and lose conditions of the race
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
			* The finish area is a box around the goal: 1 unit wide and deep, from 0.5 below to 3.5 above the goal
			*/
			inline bool hasReachedGoal(glm::vec3 playerPosition, glm::vec3 goalPosition)
			{
				return playerPosition.x > goalPosition.x - 0.5f &&
					playerPosition.y > goalPosition.y - 0.5f &&
					playerPosition.z > goalPosition.z - 0.5f &&
					playerPosition.x < goalPosition.x + 0.5f &&
					playerPosition.y < goalPosition.y + 3.5f &&
					playerPosition.z < goalPosition.z + 0.5f;
			}
		}
	}
}

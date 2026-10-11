#include "InverseKinematics.hpp"
#include <algorithm>
#include <cmath>

using namespace bibo::animation;

TwoBoneSolution bibo::animation::solveTwoBoneIK(glm::vec3 root, float upperLength, float lowerLength, glm::vec3 target, glm::vec3 pole, float minimumBend)
{
	TwoBoneSolution solution;
	glm::vec3 toTarget = target - root;
	float distance = glm::length(toTarget);
	glm::vec3 direction = distance > 1e-6f ? toTarget / distance : glm::vec3(0.0f, -1.0f, 0.0f);

	//Law of cosines: the chain reaches at most (upper + lower) and at least |upper - lower|
	float cosMax = 1.0f - minimumBend;
	float maxDistance = std::sqrt(upperLength * upperLength + lowerLength * lowerLength + 2.0f * upperLength * lowerLength * cosMax);
	float minDistance = std::abs(upperLength - lowerLength) + 1e-4f;
	float clamped = std::clamp(distance, minDistance, maxDistance);
	solution.reached = std::abs(clamped - distance) < 1e-5f;
	solution.end = root + direction * clamped;

	//Distance of the middle joint along the line from the root to the end, and its distance from that line
	float along = (upperLength * upperLength - lowerLength * lowerLength + clamped * clamped) / (2.0f * clamped);
	float height = std::sqrt(std::max(upperLength * upperLength - along * along, 0.0f));

	//The middle joint bends towards the pole, perpendicular to the line
	glm::vec3 toPole = pole - root;
	glm::vec3 bend = toPole - glm::dot(toPole, direction) * direction;
	if (glm::dot(bend, bend) < 1e-10f)
	{
		//Pole on the line: any perpendicular direction
		glm::vec3 helper = std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		bend = glm::cross(direction, helper);
	}
	bend = glm::normalize(bend);
	solution.joint = root + direction * along + bend * height;
	return solution;
}

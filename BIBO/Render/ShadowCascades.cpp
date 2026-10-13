#include "ShadowCascades.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using namespace bibo::render;

bool ShadowCascade::contains(glm::vec3 center, float sphereRadius) const
{
	//The light view is a rigid transformation: distances stay the same
	glm::vec3 local = glm::vec3(lightView * glm::vec4(center, 1.0f));
	return std::abs(local.x) <= radius + sphereRadius && std::abs(local.y) <= radius + sphereRadius
		&& local.z <= sphereRadius && local.z >= -depth - sphereRadius;
}

std::vector<ShadowCascade> bibo::render::computeCascades(const glm::mat4& view, float fieldOfView, float aspectRatio, float nearPlane,
	const std::vector<float>& splits, glm::vec3 toSun, int resolution, float casterDistance)
{
	std::vector<ShadowCascade> cascades;
	glm::mat4 cameraToWorld = glm::inverse(view);
	float tangent = std::tan(glm::radians(fieldOfView) * 0.5f);
	toSun = glm::normalize(toSun);
	glm::vec3 up = std::abs(toSun.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
	float start = nearPlane;
	for (float end : splits)
	{
		ShadowCascade cascade;
		cascade.splitNear = start;
		cascade.splitFar = end;
		//Corners of the slice of the view frustum (the camera looks along -z)
		glm::vec3 corners[8];
		int index = 0;
		for (float depth : { start, end })
		{
			float halfHeight = depth * tangent, halfWidth = halfHeight * aspectRatio;
			for (int corner = 0; corner < 4; ++corner)
			{
				glm::vec3 cameraSpace((corner & 1 ? 1.0f : -1.0f) * halfWidth, (corner & 2 ? 1.0f : -1.0f) * halfHeight, -depth);
				corners[index++] = glm::vec3(cameraToWorld * glm::vec4(cameraSpace, 1.0f));
			}
		}
		//Bounding sphere: its size does not change when the camera turns, so the shadows do not shimmer
		glm::vec3 center(0.0f);
		for (const glm::vec3& corner : corners)
		{
			center += corner / 8.0f;
		}
		float radius = 0.0f;
		for (const glm::vec3& corner : corners)
		{
			radius = std::max(radius, glm::distance(center, corner));
		}
		radius = std::ceil(radius * 2.0f) * 0.5f;

		cascade.radius = radius;
		cascade.depth = 2.0f * radius + casterDistance;
		glm::vec3 eye = center + toSun * (radius + casterDistance);
		cascade.lightView = glm::lookAt(eye, center, up);
		cascade.lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, cascade.depth);
		//The map only moves by whole texels: the shadows do not shimmer when the camera moves
		glm::mat4 lightSpace = cascade.lightProjection * cascade.lightView;
		glm::vec4 origin = lightSpace * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::vec2 texels = glm::vec2(origin) * (static_cast<float>(resolution) * 0.5f);
		glm::vec2 offset = (glm::round(texels) - texels) * (2.0f / static_cast<float>(resolution));
		cascade.lightProjection[3][0] += offset.x;
		cascade.lightProjection[3][1] += offset.y;
		cascade.lightSpace = cascade.lightProjection * cascade.lightView;
		cascade.texelSize = 2.0f * radius / static_cast<float>(resolution);
		cascades.push_back(cascade);
		start = end;
	}
	return cascades;
}

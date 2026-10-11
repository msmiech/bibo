#include "Plane.hpp"

using namespace bibo::resources;
using namespace glm;

Plane::Plane(const vec3& v1, const vec3& v2, const vec3& v3)
{
	set3Points(v1, v2, v3);
}

void Plane::set3Points(const vec3& v1, const vec3& v2, const vec3& v3)
{
	normal = normalize(cross(v3 - v2, v1 - v2));
	d = -dot(normal, v2);
}

void Plane::setNormalAndPoint(const vec3& normal, const vec3& point)
{
	this->normal = normalize(normal);
	d = -dot(this->normal, point);
}

void Plane::setCoefficients(float a, float b, float c, float d)
{
	//Normalized, so that distance() returns the euclidean distance
	float length = glm::length(vec3(a, b, c));
	normal = vec3(a, b, c) / length;
	this->d = d / length;
}

float Plane::distance(const vec3& p) const
{
	return d + dot(normal, p);
}

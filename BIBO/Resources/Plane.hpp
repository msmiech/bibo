// Plane.h
//
//////////////////////////////////////////////////////////////////////
#pragma once

#ifndef _PLANE_
#define _PLANE_
#include <glm/glm.hpp>
using namespace glm;

class Plane
{

public:

	vec3 normal, point;
	float d;


	Plane(vec3 &v1, vec3 &v2, vec3 &v3);
	Plane(void);
	~Plane();

	void set3Points(vec3 &v1, vec3 &v2, vec3 &v3);
	void setNormalAndPoint(vec3 &normal, vec3 &point);
	void setCoefficients(float a, float b, float c, float d);
	float distance(vec3 &p);
};


#endif
// Plane.cpp
//
//////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include "Plane.hpp"
#include <glm/detail/func_geometric.inl>


Plane::Plane(vec3 &v1, vec3 &v2, vec3 &v3) {

	set3Points(v1, v2, v3);
}


Plane::Plane() {}

Plane::~Plane() {}


void Plane::set3Points(vec3 &v1, vec3 &v2, vec3 &v3) {


	vec3 aux1, aux2;

	aux1 = v1 - v2;
	aux2 = v3 - v2;

	normal = aux2 * aux1;

	normal = normalize(normal);
	point.x = v2.x;
	point.y = v2.y;
	point.z = v2.z;

	d = -dot(normal, point);
}

void Plane::setNormalAndPoint(vec3 &normal, vec3 &point) {
	this->normal.x = normal.x;
	this->normal.y = normal.y;
	this->normal.z = normal.z;
	this->normal = normalize(this->normal);
	d = -dot(this->normal, point);
}

void Plane::setCoefficients(float a, float b, float c, float d) {

	// set the normal vector
	normal.x = a;
	normal.y = b;
	normal.z = c;
	//compute the lenght of the vector
	float l = normal.length();
	// normalize the vector

	normal.x = a / l;
	normal.y = b / l;
	normal.z = c / l;
	// and divide d by th length as well
	this->d = d / l;
}




float Plane::distance(vec3 &p) {

	return d + dot(normal, p);
}


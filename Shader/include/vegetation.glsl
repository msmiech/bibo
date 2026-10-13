#ifndef VEGETATION_GLSL
#define VEGETATION_GLSL
//Instanced vegetation (BIBO/World/VegetationRenderer.cpp): the vertex of the model and the instance that places it
#include "frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 color; //sRGB
layout(location = 4) in vec4 parameters; //material / 255, wind weight, snow, ambient occlusion
layout(location = 5) in vec4 instancePositionScale;
layout(location = 6) in vec4 instanceRotation; //quaternion
layout(location = 7) in vec4 instanceParameters; //tint, random number

//How far the model sways in the wind (at weight 1 and strength 1)
uniform float windAmplitude;
//The model shrinks to nothing between these distances (instead of disappearing suddenly)
uniform vec2 fadeDistances;

vec3 rotate(vec4 quaternion, vec3 v)
{
	return v + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, v) + quaternion.w * v);
}

/**
* @param worldNormal Normal of the vertex in world space
* @return Position of the vertex in world space
*/
vec3 placeInstance(out vec3 worldNormal)
{
	vec3 base = instancePositionScale.xyz;
	float scale = instancePositionScale.w;
	float distance = length(base - cameraPosition());
	scale *= 1.0 - smoothstep(fadeDistances.x, fadeDistances.y, distance);
	vec3 world = base + rotate(instanceRotation, position * scale);
	//Wind: the gusts move through the forest, every plant sways a bit differently
	float time = frameTime();
	float phase = instanceParameters.y * 6.2831853 + dot(base.xz, frame.wind.xy) * 0.08;
	float gust = 0.65 + 0.35 * sin(time * 0.6 + dot(base.xz, frame.wind.xy) * 0.03);
	float sway = sin(time * 1.4 + phase) * 0.55 + sin(time * 3.1 + phase * 1.7) * 0.25 + sin(time * 6.3 + phase * 2.9) * 0.08;
	float weight = parameters.y * parameters.y;
	vec3 direction = vec3(frame.wind.x, 0.0, frame.wind.y);
	vec3 across = vec3(-direction.z, 0.0, direction.x);
	world += (direction * (0.5 + sway) + across * 0.3 * sin(time * 2.3 + phase)) * (frame.wind.z * gust * windAmplitude * weight * scale);
	worldNormal = rotate(instanceRotation, normal);
	return world;
}

#endif

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
layout(location = 7) in vec4 instanceParameters; //tint, random number, model, fade between levels of detail (dither.glsl)

//How far the model sways in the wind (at weight 1 and strength 1)
uniform float windAmplitude;
//The model shrinks to nothing between these distances (instead of disappearing suddenly)
uniform vec2 fadeDistances;
//x: how far the bear and the animals push the model aside (relative to its height, 0: not at all or no body is close),
//y: its height at scale 1
uniform vec2 interaction;

vec3 rotate(vec4 quaternion, vec3 v)
{
	return v + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, v) + quaternion.w * v);
}

/**
* How the bodies in frame.foliageInteractors push a point of a plant aside (xz, at most 1 long)
* @param point Where the point would be without wind and bodies
* @param foot Of the plant
* @param plantHeight Of the plant (bodies higher up go over it)
*/
vec2 pushAside(vec3 point, vec3 foot, float plantHeight)
{
	vec2 push = vec2(0.0);
	int count = int(frame.wind.w);
	for (int i = 0; i < count; ++i)
	{
		vec4 body = frame.foliageInteractors[i];
		vec4 pushing = frame.foliagePushes[i];
		vec2 away = point.xz - body.xz;
		float distance = length(away);
		float around = 1.0 - smoothstep(body.w * 0.55, body.w * 1.15, distance);
		//The body touches the plant: its bottom is lower than the top of the plant, the plant does not grow far above it
		float bottom = body.y - body.w;
		float touching = (1.0 - smoothstep(0.5 * plantHeight, plantHeight + 0.1, bottom - foot.y)) * (1.0 - smoothstep(body.w, body.w + 1.0, foot.y - body.y));
		vec2 direction = distance > 1e-4 ? away / distance : frame.wind.xy;
		//Away from the body and a bit along with its movement
		push += (direction + pushing.yz * 0.6) * (pushing.x * around * touching);
	}
	float total = length(push);
	return total > 1.0 ? push / total : push;
}

/**
* Wind (gusts that roll over the island, the whole plant swaying, branches bobbing, leaves fluttering) and the bear and the
* animals pushing the plant aside. The model bends rather than stretches: what moves sideways comes down.
* @param worldNormal Normal of the vertex in world space
* @return Position of the vertex in world space
*/
vec3 placeInstance(out vec3 worldNormal)
{
	vec3 base = instancePositionScale.xyz;
	float scale = instancePositionScale.w;
	float distance = length(base - cameraPosition());
	scale *= 1.0 - smoothstep(fadeDistances.x, fadeDistances.y, distance);
	vec3 local = rotate(instanceRotation, position * scale);
	worldNormal = rotate(instanceRotation, normal);
	float height = max(local.y, 0.0);
	float time = frameTime();
	vec2 wind = frame.wind.xy;

	//Gusts: patches of stronger wind that the wind carries over the forest and the meadows (where two fronts that move with
	//the wind at slightly different angles meet). Sines, not noise: cheap enough for every vertex in every shadow map.
	float downwind = dot(base.xz, wind), crosswind = dot(base.xz, vec2(-wind.y, wind.x));
	float gust = 0.25 + 0.275 * (1.0 + sin(downwind * 0.09 + crosswind * 0.04 - time * 0.8)) + 0.225 * (1.0 + sin(downwind * 0.035 - crosswind * 0.03 - time * 0.3));
	//Every plant sways a bit differently
	float phase = instanceParameters.y * 6.2831853 + downwind * 0.08;
	float sway = sin(time * 1.4 + phase) * 0.55 + sin(time * 3.1 + phase * 1.7) * 0.25 + sin(time * 6.3 + phase * 2.9) * 0.08;
	float weight = parameters.y * parameters.y;
	float amount = frame.wind.z * gust * windAmplitude * weight * scale;
	vec3 direction = vec3(wind.x, 0.0, wind.y);
	vec3 across = vec3(-direction.z, 0.0, direction.x);
	vec3 offset = (direction * (0.5 + sway) + across * 0.3 * sin(time * 2.3 + phase)) * amount;
	//Close to the camera the branches bob up and down, each on its own (by where it is in the crown), and the needles,
	//leaves and blades flutter quickly (too small to see further away, where most of the vertices are)
	float closeness = 1.0 - smoothstep(50.0, 80.0, distance);
	if (closeness > 0.0)
	{
		offset.y += sin(time * 2.6 + phase * 3.0 + position.x * 1.9 + position.z * 2.3) * (0.25 * amount * closeness);
		float flutter = sin(time * 9.0 + dot(position, vec3(13.1, 7.7, 11.3)) + phase * 5.0);
		offset += worldNormal * (flutter * 0.1 * windAmplitude * frame.wind.z * gust * parameters.y * scale * closeness);
	}

	if (interaction.x > 0.0)
	{
		float plantHeight = interaction.y * scale;
		float flex = clamp(height / max(plantHeight, 0.01), 0.0, 1.0);
		offset.xz += pushAside(base + local, base, plantHeight) * (interaction.x * plantHeight * flex * flex);
	}

	//Bent on a circle around the foot: what moves sideways comes down (never below the ground)
	float sideways = length(offset.xz);
	offset.y -= min(height - sqrt(max(height * height - sideways * sideways, 0.0)), 0.85 * height);
	return base + local + offset;
}

#endif

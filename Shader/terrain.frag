#version 430 core
/*
* The terrain: forest floor, the dirt trail of the race track, bare rock on steep slopes, in the caves and at the cliffs,
* sand at the beaches and patches of snow. The generated PBR materials (MaterialTextures) are projected from the three
* axes (triplanar, so the cliffs and caves are not stretched) and blended by their height (snow fills the hollows of the
* forest floor first, pebbles of the trail stick out of the moss).
*/
#include "include/lighting.glsl"
#include "include/noise.glsl"

in vec3 worldPosition;
in vec3 worldNormal;
in vec4 surfaceMaterial; //trail, sand, bare rock, snow
in float visibility;

layout(binding = 3) uniform sampler2DArray materialAlbedo;
layout(binding = 4) uniform sampler2DArray materialNormal;

layout(location = 0) out vec4 fragColor;

//Layers (MaterialTextures.hpp) and how many times they repeat per world unit
const int SNOW = 0, FOREST = 1, DIRT = 2, ROCK = 3, SAND = 4;
const int LAYER_COUNT = 5;
const float LAYER_SCALE[LAYER_COUNT] = float[LAYER_COUNT](1.0 / 6.0, 1.0 / 3.5, 1.0 / 3.0, 1.0 / 7.0, 1.0 / 6.0);

struct Sample
{
	vec4 albedoRoughness;
	vec3 normal;
	float height;
	float occlusion;
};

vec3 unpackNormal(vec4 encoded)
{
	vec2 xy = encoded.xy * 2.0 - 1.0;
	return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

/**
* Projected from the axes (blend: weight of the projections along x, y and z), the normal map blended with the
* "whiteout" method (Ben Golus, Normal Mapping for a Triplanar Shader)
*/
Sample sampleLayer(int layer, vec3 position, vec3 N, vec3 blend, bool normalMapping)
{
	Sample result = Sample(vec4(0.0), vec3(0.0), 0.0, 0.0);
	float scale = LAYER_SCALE[layer];
	vec3 normalX = vec3(0.0), normalY = vec3(0.0), normalZ = vec3(0.0);
	if (blend.x > 0.0)
	{
		vec3 uvw = vec3(position.zy * scale, float(layer));
		vec4 encoded = texture(materialNormal, uvw);
		result.albedoRoughness += texture(materialAlbedo, uvw) * blend.x;
		result.height += encoded.z * blend.x;
		result.occlusion += encoded.w * blend.x;
		vec3 t = unpackNormal(encoded);
		t.z *= sign(N.x);
		normalX = vec3(t.xy + N.zy, abs(t.z) * N.x).zyx;
	}
	if (blend.y > 0.0)
	{
		vec3 uvw = vec3(position.xz * scale, float(layer));
		vec4 encoded = texture(materialNormal, uvw);
		result.albedoRoughness += texture(materialAlbedo, uvw) * blend.y;
		result.height += encoded.z * blend.y;
		result.occlusion += encoded.w * blend.y;
		vec3 t = unpackNormal(encoded);
		t.z *= sign(N.y);
		normalY = vec3(t.xy + N.xz, abs(t.z) * N.y).xzy;
	}
	if (blend.z > 0.0)
	{
		vec3 uvw = vec3(position.xy * scale, float(layer));
		vec4 encoded = texture(materialNormal, uvw);
		result.albedoRoughness += texture(materialAlbedo, uvw) * blend.z;
		result.height += encoded.z * blend.z;
		result.occlusion += encoded.w * blend.z;
		vec3 t = unpackNormal(encoded);
		t.z *= sign(N.z);
		normalZ = vec3(t.xy + N.xy, abs(t.z) * N.z);
	}
	result.normal = normalMapping ? normalize(normalX * blend.x + normalY * blend.y + normalZ * blend.z) : N;
	return result;
}

void main()
{
	vec3 N = normalize(worldNormal);
	vec3 position = worldPosition;
	float distance = length(position - cameraPosition());
	bool normalMapping = frame.sunColorNormalMapping.w > 0.5 && distance < 220.0;

	//Projection weights: mostly from above on flat ground, from the side on cliffs
	vec3 blend = pow(abs(N), vec3(4.0));
	blend /= blend.x + blend.y + blend.z;
	blend = max(blend - 0.03, 0.0);
	blend /= blend.x + blend.y + blend.z;

	//What covers the ground (from the generator), with uneven borders
	float breakup = fbm(position.xz * 0.35, 3);
	//Bare rock only on very steep faces, moss and needles hold on the slopes below
	float steep = 1.0 - smoothstep(0.42, 0.62, N.y + (breakup - 0.5) * 0.16);
	float weights[LAYER_COUNT];
	weights[ROCK] = max(surfaceMaterial.z, steep);
	weights[SAND] = surfaceMaterial.y * (1.0 - weights[ROCK]);
	weights[DIRT] = surfaceMaterial.x * max(1.0 - weights[ROCK] - weights[SAND], 0.0);
	weights[FOREST] = max(1.0 - weights[ROCK] - weights[SAND] - weights[DIRT], 0.0);
	//Moss grows on the rock walls where they are not overhanging (not in the caves)
	float mossOnRock = smoothstep(0.15, 0.45, N.y + (breakup - 0.5) * 0.5) * smoothstep(0.35, 0.65, fbm(position.xz * 0.12 + position.y * 0.05, 3)) * (1.0 - surfaceMaterial.z);
	weights[FOREST] += weights[ROCK] * mossOnRock * 0.7;
	weights[ROCK] *= 1.0 - mossOnRock * 0.7;
	float snow = surfaceMaterial.w * smoothstep(0.2, 0.55, surfaceMaterial.w + (breakup - 0.5) * 0.7);
	weights[SNOW] = snow;
	for (int i = 1; i < LAYER_COUNT; ++i)
	{
		weights[i] *= 1.0 - snow * (i == ROCK ? 0.7 : 1.0);
	}

	//Height blending: the higher material wins where both are
	Sample samples[LAYER_COUNT];
	float highest = -1.0;
	for (int i = 0; i < LAYER_COUNT; ++i)
	{
		if (weights[i] > 0.01)
		{
			samples[i] = sampleLayer(i, position, N, blend, normalMapping);
			highest = max(highest, weights[i] + samples[i].height * 0.5);
		}
	}
	vec4 albedoRoughness = vec4(0.0);
	vec3 normal = vec3(0.0);
	float occlusion = 0.0, total = 0.0;
	float snowWeight = 0.0;
	for (int i = 0; i < LAYER_COUNT; ++i)
	{
		if (weights[i] > 0.01)
		{
			float weight = max(weights[i] + samples[i].height * 0.5 - highest + 0.22, 0.0);
			albedoRoughness += samples[i].albedoRoughness * weight;
			normal += samples[i].normal * weight;
			occlusion += samples[i].occlusion * weight;
			total += weight;
			if (i == SNOW)
			{
				snowWeight = weight;
			}
		}
	}
	albedoRoughness /= total;
	occlusion /= total;
	snowWeight /= total;
	normal = normalize(normal);

	//Large scale variation of the colors, so that the repetition of the textures is not visible
	float variation = fbm(position.xz * 0.018, 3);
	float tint = fbm(position.xz * 0.05 + 17.0, 2);
	vec3 albedo = albedoRoughness.rgb * mix(0.72, 1.25, variation) * (1.0 - snowWeight * 0.3 * (1.0 - variation));
	albedo = mix(albedo, albedo * vec3(1.15, 1.08, 0.75), (1.0 - snowWeight) * smoothstep(0.45, 0.75, tint) * 0.6);
	albedo = mix(albedoRoughness.rgb, albedo, 1.0 - snowWeight * 0.8);
	//Wet sand and rock at the water line
	float seaLevel = frame.worldMap.w;
	float wet = 1.0 - smoothstep(seaLevel + 0.1, seaLevel + 0.9, position.y);
	albedo *= 1.0 - 0.45 * wet;
	float roughness = mix(albedoRoughness.a, 0.25, wet * 0.8);

	Surface surface = defaultSurface(albedo, normal);
	surface.roughness = roughness;
	surface.occlusion = occlusion * pow(visibility, 1.3);
	//In a cave the roof shades the sun, even without shadow maps
	float shadow = min(sunShadow(position, N), smoothstep(0.08, 0.45, visibility));
	vec3 color = shade(surface, position, shadow);

	//Glittering snow crystals in the sun
	if (snowWeight > 0.2 && distance < 60.0)
	{
		vec3 V = normalize(cameraPosition() - position);
		float crystal = step(0.9965, hash13(floor(position * 55.0)));
		float glint = pow(max(dot(reflect(-V, normal), sunDirection()), 0.0), 24.0);
		color += frame.sunColorNormalMapping.rgb * crystal * glint * shadow * snowWeight * 3.0;
	}

	fragColor = vec4(applyFog(color, position), 1.0);
}

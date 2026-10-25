#version 430 core
/*
* Generates the tileable PBR materials of the world (BIBO/World/MaterialTextures.cpp): the color (linear, written to an sRGB
* texture) and roughness of one material, and its height (for the normal map and ambient occlusion, see material_normals.frag).
* Everything repeats at the border of the texture, so that the material can cover the terrain without seams.
*/
#include "include/noise.glsl"

in vec2 uv;

uniform int material;

layout(location = 0) out vec4 albedoRoughness;
layout(location = 1) out float heightOutput;

//Layers of the material textures (MaterialTextures.hpp)
const int SNOW = 0;
const int FOREST_FLOOR = 1;
const int DIRT = 2;
const int ROCK = 3;
const int SAND = 4;
const int BARK = 5;
const int NEEDLES = 6;
const int LEAVES = 7;

struct Material
{
	vec3 albedo;
	float roughness;
	float height;
};

/**
* Thin strokes in random directions, e.g. fallen spruce needles or the needles on a branch
* @return x: coverage of the strokes, y: random value of the stroke on top
*/
vec2 strokes(vec2 p, float period, float strokeLength, float width, float seed)
{
	vec2 scaled = p * period;
	vec2 cell = floor(scaled);
	vec2 f = fract(scaled);
	vec2 result = vec2(0.0);
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 neighbor = cell + vec2(x, y);
			vec2 wrapped = mod(neighbor, period);
			vec2 center = vec2(x, y) + hash22(wrapped + seed) - f;
			float angle = hash12(wrapped * 1.7 + seed) * 6.2831853;
			vec2 direction = vec2(cos(angle), sin(angle));
			float along = dot(-center, direction);
			float across = dot(-center, vec2(-direction.y, direction.x));
			float stroke = (1.0 - smoothstep(0.3 * width, width, abs(across))) * (1.0 - smoothstep(0.8 * strokeLength, strokeLength, abs(along)));
			if (stroke > result.x)
			{
				result = vec2(stroke, hash12(wrapped + seed * 3.1));
			}
		}
	}
	return result;
}

Material snow(vec2 p)
{
	float drifts = tiledFbm(p, 4.0, 5);
	float grains = tiledFbm(p, 96.0, 2);
	float height = 0.85 * drifts + 0.15 * grains;
	//Hollows are a bit blue (light scattered in the snow)
	vec3 albedo = mix(vec3(0.64, 0.71, 0.83), vec3(0.85, 0.88, 0.92), smoothstep(0.3, 0.65, height));
	return Material(albedo, mix(0.72, 0.5, grains), height);
}

Material forestFloor(vec2 p)
{
	//Dark soil under a litter of fallen spruce needles and twigs, with cushions of moss
	float soil = tiledFbm(p, 6.0, 5);
	vec3 albedo = mix(vec3(0.035, 0.025, 0.016), vec3(0.085, 0.06, 0.036), soil);
	float height = 0.3 * soil;
	for (int layer = 0; layer < 3; ++layer)
	{
		vec2 needles = strokes(p, 26.0 + 9.0 * float(layer), 0.46, 0.03, 1.0 + 6.0 * float(layer));
		vec3 color = mix(vec3(0.13, 0.065, 0.028), vec3(0.27, 0.15, 0.06), needles.y) * (0.75 + 0.15 * float(layer));
		albedo = mix(albedo, color, needles.x * 0.9);
		height = max(height, 0.3 + 0.12 * float(layer) * needles.x);
	}
	float twigs = strokes(p, 7.0, 0.42, 0.016, 13.0).x * step(0.65, hash12(floor(p * 7.0)));
	albedo = mix(albedo, vec3(0.12, 0.085, 0.055), twigs);
	height += 0.12 * twigs;
	float moss = smoothstep(0.45, 0.7, tiledFbm(p + vec2(0.37, 0.11), 5.0, 5));
	float mossDetail = tiledFbm(p, 64.0, 3);
	vec3 mossColor = mix(vec3(0.03, 0.05, 0.014), vec3(0.085, 0.115, 0.03), mossDetail);
	albedo = mix(albedo, mossColor, moss);
	height = mix(height, 0.55 + 0.3 * mossDetail, moss);
	return Material(albedo, mix(0.9, 0.85, moss), height);
}

Material dirt(vec2 p)
{
	//Trodden earth: trodden and smoother where it is walked on most, loose soil around it, fine gravel and pebbles of
	//two sizes and different colors (more of them where the earth is loose)
	float soil = tiledFbm(p, 5.0, 6);
	float trodden = smoothstep(0.35, 0.7, tiledFbm(p + vec2(0.23, 0.61), 3.0, 4));
	vec3 albedo = mix(vec3(0.11, 0.068, 0.036), vec3(0.25, 0.16, 0.085), soil);
	albedo = mix(albedo, albedo * vec3(1.15, 1.1, 1.0), trodden);
	float height = 0.3 * soil + 0.1 * trodden;
	float stones = 0.0;
	for (int size = 0; size < 2; ++size)
	{
		vec3 cells = tiledCells(p + vec2(0.17, 0.43) * float(size), size == 0 ? 9.0 : 20.0);
		float radius = size == 0 ? 0.34 : 0.3;
		float pebble = step(size == 0 ? 0.84 : 0.7, cells.z) * (1.0 - smoothstep(0.7 * radius, radius, cells.x)) * (1.0 - 0.6 * trodden);
		float tone = hash11(cells.z * 91.7 + float(size) * 7.3);
		vec3 pebbleColor = mix(vec3(0.1, 0.09, 0.075), vec3(0.25, 0.21, 0.16), tone);
		pebbleColor = mix(pebbleColor, vec3(0.21, 0.12, 0.07), step(0.8, hash11(cells.z * 13.1)) * 0.6); //a few reddish ones
		albedo = mix(albedo, pebbleColor * (0.85 + 0.3 * tiledFbm(p, 64.0, 2)), pebble);
		height = mix(height, 0.45 + 0.45 * sqrt(max(1.0 - cells.x / radius, 0.0)), pebble);
		stones = max(stones, pebble);
	}
	vec3 gravel = tiledCells(p, 52.0);
	float grit = step(0.55, gravel.z) * (1.0 - smoothstep(0.12, 0.32, gravel.x)) * (1.0 - 0.5 * trodden);
	albedo = mix(albedo, vec3(0.19, 0.165, 0.135), grit * 0.5);
	height += 0.08 * grit;
	return Material(albedo, mix(mix(0.93, 0.82, trodden), 0.75, stones), height);
}

Material rock(vec2 p)
{
	//Granite: dark with light and black crystals, split by joints into blocks (warped cells, a bit longer than high) whose
	//faces are a bit different, the joints fade in and out along their length; rusty stains and a bit of lichen
	vec2 warp = vec2(tiledFbm(p, 4.0, 4), tiledFbm(p + vec2(0.37, 0.71), 4.0, 4)) - 0.5;
	vec2 q = p + 0.08 * warp;
	float base = tiledFbm(q, 3.0, 6);
	float detail = tiledFbm(q, 24.0, 4);
	vec3 albedo = mix(vec3(0.068, 0.065, 0.06), vec3(0.185, 0.175, 0.16), 0.65 * base + 0.35 * detail);
	float crystals = tiledNoise(p * 256.0, 256.0);
	albedo = mix(albedo, vec3(0.26, 0.245, 0.22), step(0.88, crystals) * 0.5);
	albedo = mix(albedo, vec3(0.025), step(0.9, 1.0 - crystals) * 0.45);
	vec3 blocks = tiledCells(q + 0.06 * warp, vec2(3.0, 5.0));
	float edge = blocks.y - blocks.x;
	float width = tiledFbm(q + vec2(0.83, 0.39), 8.0, 3);
	float joint = (1.0 - smoothstep(0.0, 0.02 + 0.04 * width, edge)) * smoothstep(0.35, 0.65, tiledFbm(q + vec2(0.29, 0.83), 6.0, 4));
	float small = tiledFbm(q + vec2(0.61, 0.23), 11.0, 4) - 0.5;
	float fine = (1.0 - smoothstep(0.0, 0.015, abs(small))) * 0.3 * smoothstep(0.45, 0.7, tiledFbm(q + vec2(0.53, 0.07), 9.0, 3));
	float face = blocks.z;
	albedo *= (0.84 + 0.3 * face) * (1.0 - 0.4 * joint) * (1.0 - fine);
	float stain = smoothstep(0.55, 0.78, tiledFbm(p + vec2(0.41, 0.19), 3.0, 5));
	albedo = mix(albedo, albedo * vec3(1.3, 0.95, 0.68), stain * 0.55);
	//The blocks are rounded off at their edges
	float height = 0.3 * base + 0.2 * detail + 0.3 * smoothstep(0.0, 0.3, edge) + 0.1 * face - 0.2 * joint - 0.1 * fine;
	float lichen = smoothstep(0.62, 0.7, tiledFbm(p + vec2(0.71, 0.29), 6.0, 5)) * smoothstep(0.1, 0.4, edge);
	albedo = mix(albedo, vec3(0.17, 0.19, 0.11), lichen * 0.45);
	return Material(albedo, mix(0.85, 0.65, detail) + 0.08 * lichen, height + 0.04 * lichen);
}

Material sand(vec2 p)
{
	float warp = tiledFbm(p, 3.0, 3);
	float ripples = 0.5 + 0.5 * sin(6.2831853 * (14.0 * p.y + 2.0 * warp + 1.0 * p.x));
	ripples = pow(ripples, 1.5);
	float grains = tiledFbm(p, 128.0, 2);
	float variation = tiledFbm(p, 4.0, 4);
	vec3 albedo = mix(vec3(0.42, 0.36, 0.26), vec3(0.62, 0.55, 0.41), variation);
	albedo *= 0.9 + 0.2 * grains;
	vec3 shells = tiledCells(p, 24.0);
	float shell = step(0.93, shells.z) * (1.0 - smoothstep(0.1, 0.2, shells.x));
	albedo = mix(albedo, vec3(0.75, 0.72, 0.66), shell);
	return Material(albedo, 0.92, 0.45 * ripples + 0.35 * variation + 0.1 * grains + 0.1 * shell);
}

Material bark(vec2 p)
{
	//u goes around the trunk, v along it: rough gray brown bark with long, thin fissures and small flakes
	float streaks = tiledFbm(p, 6.0, 4) * 0.4 + tiledNoise(vec2(p.x * 48.0, p.y * 4.0), vec2(48.0, 4.0)) * 0.6;
	float flakes = tiledFbm(p, 40.0, 3);
	vec3 scales = tiledCells(p + vec2(0.02 * tiledFbm(p, 4.0, 3), 0.0), vec2(14.0, 6.0));
	float fissure = (1.0 - smoothstep(0.0, 0.07, scales.y - scales.x)) * 0.7;
	vec3 albedo = mix(vec3(0.07, 0.055, 0.045), vec3(0.17, 0.14, 0.115), 0.45 * streaks + 0.35 * flakes + 0.2 * scales.z);
	albedo *= 1.0 - 0.65 * fissure;
	float height = 0.5 * streaks + 0.3 * flakes + 0.2 * scales.x - 0.35 * fissure;
	return Material(albedo, 0.9, height);
}

Material needles(vec2 p)
{
	//Many layers of needles, the upper ones lighter
	vec3 albedo = vec3(0.012, 0.035, 0.015);
	float height = 0.0;
	for (int layer = 0; layer < 4; ++layer)
	{
		vec2 stroke = strokes(p, 18.0 + 6.0 * float(layer), 0.5, 0.06, float(layer) * 5.3 + 2.0);
		float top = float(layer) / 3.0;
		vec3 color = mix(vec3(0.02, 0.06, 0.025), vec3(0.06, 0.13, 0.05), top) * (0.75 + 0.5 * stroke.y);
		albedo = mix(albedo, color, stroke.x);
		height = max(height, stroke.x * (0.4 + 0.6 * top));
	}
	return Material(albedo, 0.6, height);
}

Material leaves(vec2 p)
{
	vec3 albedo = vec3(0.02, 0.05, 0.015);
	float height = 0.0;
	for (int layer = 0; layer < 3; ++layer)
	{
		float period = 9.0 + 5.0 * float(layer);
		vec2 scaled = p * period;
		vec3 cells = tiledCells(p + vec2(0.13, 0.31) * float(layer), period);
		float leaf = 1.0 - smoothstep(0.32, 0.45, cells.x);
		float vein = 1.0 - smoothstep(0.0, 0.06, abs(fract(scaled.x - scaled.y) - 0.5));
		vec3 color = mix(vec3(0.03, 0.09, 0.02), vec3(0.10, 0.20, 0.05), cells.z) * (0.85 + 0.15 * vein);
		albedo = mix(albedo, color, leaf);
		height = max(height, leaf * (0.5 + 0.5 * float(layer) / 2.0) * (1.0 - cells.x));
	}
	return Material(albedo, 0.55, height);
}

void main()
{
	Material result;
	if (material == SNOW) result = snow(uv);
	else if (material == FOREST_FLOOR) result = forestFloor(uv);
	else if (material == DIRT) result = dirt(uv);
	else if (material == ROCK) result = rock(uv);
	else if (material == SAND) result = sand(uv);
	else if (material == BARK) result = bark(uv);
	else if (material == NEEDLES) result = needles(uv);
	else result = leaves(uv);
	albedoRoughness = vec4(result.albedo, clamp(result.roughness, 0.05, 1.0));
	heightOutput = clamp(result.height, 0.0, 1.0);
}

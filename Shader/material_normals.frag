#version 430 core
/*
* Normal map and ambient occlusion of a generated material (BIBO/World/MaterialTextures.cpp) from its height
* (material_generator.frag): xy of the tangent space normal, the height and the occlusion of the hollows.
*/
in vec2 uv;

layout(binding = 0) uniform sampler2D heightMap;
//How steep the surface gets for a difference of the height of 1 over 1/512 of the texture
uniform float bumpStrength;

layout(location = 0) out vec4 normalHeight;

float heightAt(ivec2 texel, ivec2 size)
{
	return texelFetch(heightMap, ((texel % size) + size) % size, 0).r;
}

void main()
{
	ivec2 size = textureSize(heightMap, 0);
	ivec2 texel = ivec2(gl_FragCoord.xy);
	float height = heightAt(texel, size);
	float scale = bumpStrength * float(size.x) / 512.0;
	float dx = heightAt(texel + ivec2(1, 0), size) - heightAt(texel - ivec2(1, 0), size);
	float dy = heightAt(texel + ivec2(0, 1), size) - heightAt(texel - ivec2(0, 1), size);
	vec3 normal = normalize(vec3(-0.5 * dx * scale, -0.5 * dy * scale, 1.0));

	//Hollows (lower than the surroundings) get less ambient light
	float around = 0.0;
	const int SAMPLES = 12;
	for (int i = 0; i < SAMPLES; ++i)
	{
		float angle = float(i) * 6.2831853 / float(SAMPLES);
		float radius = (i % 2 == 0 ? 3.0 : 8.0) * float(size.x) / 512.0;
		around += heightAt(texel + ivec2(round(radius * vec2(cos(angle), sin(angle)))), size);
	}
	around /= float(SAMPLES);
	float occlusion = clamp(1.0 - 2.0 * max(around - height, 0.0), 0.35, 1.0);

	normalHeight = vec4(normal.xy * 0.5 + 0.5, height, occlusion);
}

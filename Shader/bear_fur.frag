#version 430 core
/*
* Shading of the fur strands (bear_fur.vert): Kajiya-Kay diffuse and two shifted specular highlights along the strand,
* soft wrap-around lighting (fur scatters light), darker roots (self shadowing of the fur), lighter tips
* and light that shines through the tips when the sun is behind the bear.
* The spines of the hedgehogs (spines = 1) are banded like real ones: light at the root, a dark brown band and a light tip,
* and smooth like horn.
*/
#include "include/lighting.glsl"

in vec3 worldPosition;
in vec3 strandTangent;
in vec3 skinNormal;
in vec3 furColor;
in float strandParameter;
in float coverage;

uniform vec3 worldEye;
uniform int spines;

layout (location = 0) out vec4 fragColor;

float strandHighlight(vec3 tangent, vec3 halfway, float exponent)
{
	float dotTH = dot(tangent, halfway);
	return pow(sqrt(max(0.0, 1.0 - dotTH * dotTH)), exponent);
}

void main()
{
	float t = strandParameter;
	bool stiff = spines == 1;
	float alpha = coverage * (stiff ? 1.0 - smoothstep(0.93, 1.0, t) : 1.0 - 0.75 * smoothstep(0.7, 1.0, t));
	if (alpha < 0.03)
	{
		discard;
	}

	vec3 T = normalize(strandTangent);
	vec3 N = normalize(skinNormal);
	vec3 L = sunDirection();
	vec3 V = normalize(worldEye - worldPosition);
	vec3 H = normalize(L + V);
	vec3 sun = frame.sunColorNormalMapping.rgb / PI;

	//Sun-bleached tips, dark roots
	vec3 albedo = furColor * mix(0.75, 1.3, t);
	float occlusion = mix(0.4, 1.0, t * t * (3.0 - 2.0 * t));
	if (stiff)
	{
		float band = smoothstep(0.12, 0.24, t) * (1.0 - smoothstep(0.84, 0.93, t));
		albedo = mix(furColor, furColor * vec3(0.16, 0.12, 0.09), band);
		occlusion = mix(0.5, 1.0, smoothstep(0.0, 0.6, t));
	}

	float wrap = clamp((dot(N, L) + 0.4) / 1.4, 0.0, 1.0);
	float dotTL = dot(T, L);
	float kajiyaDiffuse = sqrt(max(0.0, 1.0 - dotTL * dotTL));
	float diffuse = wrap * mix(1.0, kajiyaDiffuse, 0.35);

	//Primary highlight (white, shifted towards the tip) and secondary highlight (colored, shifted towards the root)
	float primary = strandHighlight(normalize(T + N * 0.15), H, stiff ? 160.0 : 90.0);
	float secondary = strandHighlight(normalize(T - N * 0.1), H, 24.0);
	vec3 specular = sun * ((stiff ? 0.22 : 0.1) * primary + 0.25 * secondary * albedo) * wrap * occlusion;

	float lit = sunShadow(worldPosition, N);
	vec3 sky = skyLight(N, 0.85);
	vec3 backlight = sun * pow(clamp(dot(-L, V), 0.0, 1.0), 3.0) * t * 0.5;

	vec3 color = sky * albedo * occlusion + lit * (sun * albedo * diffuse * occlusion + specular + backlight * albedo);

	fragColor = vec4(applyFog(color, worldPosition), alpha);
}

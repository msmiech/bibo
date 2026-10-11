#version 430 core
/*
* Shading of the fur strands (bear_fur.vert): Kajiya-Kay diffuse and two shifted specular highlights along the strand,
* soft wrap-around lighting (fur scatters light), darker roots (self shadowing of the fur), lighter tips
* and light that shines through the tips when the sun is behind the bear.
*/
in vec3 worldPosition;
in vec3 strandTangent;
in vec3 skinNormal;
in vec3 furColor;
in float strandParameter;
in float coverage;
in vec4 fragPosLightSpace;

uniform struct Light {
	uint type;
	vec3 position;
	vec3 direction;
	vec3 ambientColor;
	vec3 diffuseColor;
	vec3 specularColor;
} light;
uniform sampler2D shadowMap;
uniform bool shadowMappingEnabled;
uniform vec3 worldEye;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 brightColor;

float shadowCalculation(vec4 positionLightSpace)
{
	vec3 projected = positionLightSpace.xyz / positionLightSpace.w * 0.5 + 0.5;
	if (projected.z > 1.0)
	{
		return 0.0;
	}
	float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			float depth = texture(shadowMap, projected.xy + vec2(x, y) * texelSize).r;
			shadow += projected.z - 0.004 > depth ? 1.0 : 0.0;
		}
	}
	return shadow / 9.0;
}

float strandHighlight(vec3 tangent, vec3 halfway, float exponent)
{
	float dotTH = dot(tangent, halfway);
	return pow(sqrt(max(0.0, 1.0 - dotTH * dotTH)), exponent);
}

void main()
{
	float t = strandParameter;
	float alpha = coverage * (1.0 - 0.75 * smoothstep(0.7, 1.0, t));
	if (alpha < 0.03)
	{
		discard;
	}

	vec3 T = normalize(strandTangent);
	vec3 N = normalize(skinNormal);
	vec3 L = normalize(-light.direction);
	vec3 V = normalize(worldEye - worldPosition);
	vec3 H = normalize(L + V);

	//Sun-bleached tips, dark roots
	vec3 albedo = furColor * mix(0.75, 1.3, t);
	float occlusion = mix(0.4, 1.0, t * t * (3.0 - 2.0 * t));

	float wrap = clamp((dot(N, L) + 0.4) / 1.4, 0.0, 1.0);
	float dotTL = dot(T, L);
	float kajiyaDiffuse = sqrt(max(0.0, 1.0 - dotTL * dotTL));
	float diffuse = wrap * mix(1.0, kajiyaDiffuse, 0.35);

	//Primary highlight (white, shifted towards the tip) and secondary highlight (colored, shifted towards the root)
	float primary = strandHighlight(normalize(T + N * 0.15), H, 90.0);
	float secondary = strandHighlight(normalize(T - N * 0.1), H, 24.0);
	vec3 specular = light.specularColor * (0.1 * primary + 0.25 * secondary * albedo) * wrap * occlusion;

	float shadow = shadowMappingEnabled ? shadowCalculation(fragPosLightSpace) : 0.0;
	vec3 sky = light.ambientColor * 0.25;
	vec3 backlight = light.diffuseColor * pow(clamp(dot(-L, V), 0.0, 1.0), 3.0) * t * 0.5;

	vec3 color = sky * albedo * occlusion
		+ (1.0 - shadow) * (light.diffuseColor * albedo * diffuse * occlusion + specular + backlight * albedo);

	fragColor = vec4(color, alpha);
	float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
	brightColor = brightness > 1.0 ? vec4(color, alpha) : vec4(0.0, 0.0, 0.0, alpha);
}

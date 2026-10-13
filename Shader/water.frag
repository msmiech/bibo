#version 430 core
/*
* Simple water: waves from moving noise, the sky reflected (Fresnel), the glitter of the sun, turquoise where it is shallow,
* foam at the beaches. In the caves the water reflects the dark roof instead of the sky.
*/
#include "include/lighting.glsl"
#include "include/noise.glsl"

in vec3 worldPosition;

layout(binding = 6) uniform sampler2D worldMap; //r: height of the ground, g: top of a cave roof, b: close to the coast
uniform bool transparent;

layout(location = 0) out vec4 fragColor;

float waves(vec2 p, float time)
{
	float height = 0.0;
	height += 0.50 * valueNoise(p * 0.15 + vec2(time * 0.05, time * 0.03));
	height += 0.25 * valueNoise(p * 0.37 - vec2(time * 0.07, -time * 0.04));
	height += 0.12 * valueNoise(p * 0.9 + vec2(-time * 0.11, time * 0.09));
	height += 0.06 * valueNoise(p * 2.1 + vec2(time * 0.17, time * 0.13));
	return height;
}

void main()
{
	vec3 position = worldPosition;
	vec3 toCamera = cameraPosition() - position;
	float distance = length(toCamera);
	vec3 V = toCamera / distance;
	float time = frameTime();

	//Normal of the waves, calmer in the distance (no flickering)
	const float E = 0.08;
	float h = waves(position.xz, time);
	float dx = waves(position.xz + vec2(E, 0.0), time) - h;
	float dz = waves(position.xz + vec2(0.0, E), time) - h;
	float strength = 0.9 * (1.0 - smoothstep(30.0, 500.0, distance));
	vec3 N = normalize(vec3(-dx / E * strength, 1.0, -dz / E * strength));

	vec2 mapPosition = (position.xz - frame.worldMap.xy) / frame.worldMap.z;
	vec4 map = all(greaterThanEqual(mapPosition, vec2(0.0))) && all(lessThanEqual(mapPosition, vec2(1.0))) ? texture(worldMap, mapPosition) : vec4(-40.0, -1e6, 0.0, 0.0);
	float depth = max(position.y - map.r, 0.0);
	bool underRoof = map.g > position.y;

	//Light in the water: dark blue in the deep, turquoise over sand
	float shallow = exp(-depth * 0.35);
	vec3 body = mix(vec3(0.004, 0.03, 0.045), vec3(0.03, 0.16, 0.15), shallow);
	vec3 ambient = underRoof ? vec3(0.04) : skyLight(vec3(0.0, 1.0, 0.0), 0.9);
	vec3 color = body * ambient * 1.4;

	//Reflection of the sky (or the cave roof)
	float NdotV = max(dot(N, V), 0.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
	vec3 reflected = reflect(-V, N);
	reflected.y = abs(reflected.y);
	vec3 reflection = underRoof ? vec3(0.015, 0.02, 0.02) : skyLight(reflected, 0.04);
	color = mix(color, reflection, fresnel);

	//Glitter of the sun
	float shadow = underRoof ? 0.0 : sunShadow(position, vec3(0.0, 1.0, 0.0));
	vec3 H = normalize(sunDirection() + V);
	float NdotH = max(dot(N, H), 0.0);
	color += frame.sunColorNormalMapping.rgb * (pow(NdotH, 500.0) * 6.0 + pow(NdotH, 80.0) * 0.25) * shadow;

	//Foam where the waves reach the beach
	float foamLine = (1.0 - smoothstep(0.0, 0.7, depth)) * map.b;
	float foam = foamLine * smoothstep(0.35, 0.75, valueNoise(position.xz * 1.3 + vec2(time * 0.25, 0.0)) * 0.6 + 0.5 * (1.0 - depth / 0.7));
	color = mix(color, vec3(0.9, 0.94, 0.97) * ambient * 0.9, foam * 0.8);

	float alpha = transparent ? clamp(0.55 + depth * 0.2 + fresnel + foam, 0.0, 1.0) : 1.0;
	fragColor = vec4(applyFog(color, position), alpha);
}

#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL
//The sky of the island: a bright polar day with a low sun, a few clouds and polar lights (aurora) in the north.
//Used for the environment cube map (sky_environment.frag), which lights the scene, and for the background (sky.frag).
#include "frame.glsl"
#include "noise.glsl"

const vec3 ZENITH = vec3(0.055, 0.15, 0.48);
const vec3 HORIZON = vec3(0.50, 0.65, 0.88);
const vec3 WARM_HORIZON = vec3(1.25, 0.95, 0.70);
const vec3 GROUND = vec3(0.045, 0.06, 0.06);
const float SKY_BRIGHTNESS = 1.15;

/**
* Horizontal axes of the aurora: it is in the "north", opposite of the sun
*/
void auroraAxes(out vec2 north, out vec2 east)
{
	vec2 toSun = sunDirection().xz;
	north = length(toSun) > 1e-3 ? -normalize(toSun) : vec2(0.0, -1.0);
	east = vec2(north.y, -north.x);
}

/**
* Light of the sky without the disk of the sun (the gradient, the haze at the horizon and the glow around the sun)
*/
vec3 skyGradient(vec3 direction)
{
	vec3 L = sunDirection();
	float up = max(direction.y, 0.0);
	vec3 color = mix(HORIZON, ZENITH, pow(up, 0.55));
	float horizonBand = pow(1.0 - up, 10.0);
	float towardsSun = max(dot(direction, L), 0.0);
	//Haze at the horizon, warm on the side of the low sun
	color += vec3(0.12, 0.11, 0.10) * horizonBand;
	color = mix(color, WARM_HORIZON, horizonBand * pow(towardsSun, 4.0) * 0.55);
	//Glow around the sun (scattering in the air)
	vec3 sun = frame.sunColorNormalMapping.rgb;
	color += sun * (0.035 * pow(towardsSun, 6.0) + 0.09 * pow(towardsSun, 48.0) + 0.35 * pow(towardsSun, 400.0));
	//Below the horizon: the sea and the land far away
	if (direction.y < 0.0)
	{
		color = mix(HORIZON * 0.85 + vec3(0.1) * horizonBand, GROUND, smoothstep(0.0, 0.2, -direction.y));
	}
	return color * SKY_BRIGHTNESS;
}

/**
* Clouds on a layer high above: rgb color, a coverage
*/
vec4 clouds(vec3 direction)
{
	if (direction.y < 0.015)
	{
		return vec4(0.0);
	}
	float time = frameTime();
	vec2 drift = frame.wind.xy * time * 0.0035;
	vec2 position = direction.xz / (direction.y + 0.08) * 0.9 + drift;
	float heap = fbm(position * 1.4, 6);
	float cumulus = smoothstep(0.56, 0.78, heap);
	float veil = smoothstep(0.5, 0.8, fbm(position * 0.45 + vec2(7.3, -2.1), 4)) * 0.35;
	float coverage = max(cumulus, veil) * smoothstep(0.015, 0.16, direction.y);
	//Lit from the sun: bright tops, gray bottoms, silver lining towards the sun
	float towardsSun = max(dot(direction, sunDirection()), 0.0);
	float thickness = smoothstep(0.56, 0.9, heap);
	vec3 lit = vec3(1.0, 0.97, 0.94) * (1.15 + 0.6 * pow(towardsSun, 6.0));
	vec3 shaded = vec3(0.58, 0.64, 0.74);
	vec3 color = mix(lit, shaded, thickness * 0.65) * SKY_BRIGHTNESS;
	return vec4(color, coverage * 0.9);
}

/**
* Curtains of polar light: thin, folded sheets hanging from the sky, green at their sharp lower edge and violet above,
* with fine vertical rays that move along them. The view ray is followed through the height of the curtains.
* @param steps Samples along the ray (fewer for software rendering)
*/
vec3 aurora(vec3 direction, int steps)
{
	float intensity = frame.groundAmbient.w;
	if (direction.y < 0.02 || intensity <= 0.0)
	{
		return vec3(0.0);
	}
	vec2 north, east;
	auroraAxes(north, east);
	vec2 horizontal = vec2(dot(direction.xz, east), dot(direction.xz, north));
	float time = frameTime();
	//Curtains: distance to the north (in units of their lowest height), amplitude of the folds, brightness
	const vec3 CURTAINS[3] = vec3[3](vec3(1.3, 0.55, 1.0), vec3(2.3, 0.8, 0.8), vec3(3.6, 1.1, 0.55));
	vec3 light = vec3(0.0);
	float jitter = hash12(gl_FragCoord.xy) / float(steps);
	for (int i = 0; i < steps; ++i)
	{
		float t = float(i) / float(steps) + jitter; //0: lower edge, 1: top
		float altitude = 1.0 + 1.8 * t;
		vec2 p = horizontal / direction.y * altitude; //where the ray goes through this height
		float profile = smoothstep(0.0, 0.05, t) * exp(-3.2 * t);
		vec3 color = mix(vec3(0.10, 1.0, 0.42), vec3(0.62, 0.18, 0.95), smoothstep(0.2, 0.75, t));
		color = mix(color, vec3(0.95, 0.25, 0.45), smoothstep(0.75, 1.0, t) * 0.5);
		for (int c = 0; c < 3; ++c)
		{
			vec3 curtain = CURTAINS[c];
			float phase = float(c) * 1.9;
			float fold = curtain.x + curtain.y * sin(p.x * 0.5 + phase + time * 0.045)
				+ 0.45 * curtain.y * (valueNoise(vec2(p.x * 0.9 + phase, time * 0.04)) - 0.5) * 2.0;
			float distance = p.y - fold;
			float sheet = exp(-distance * distance * 55.0);
			if (sheet < 0.002)
			{
				continue;
			}
			float rays = 0.3 + 0.7 * valueNoise(vec2(p.x * 7.0 + phase, time * 0.35));
			rays *= 0.55 + 0.45 * valueNoise(vec2(p.x * 23.0 - phase, time * 0.8));
			light += color * sheet * rays * profile * curtain.z;
		}
	}
	float horizonFade = smoothstep(0.02, 0.18, direction.y);
	return light * (7.0 / float(steps)) * intensity * horizonFade;
}

/**
* Everything but the disk of the sun
*/
vec3 skyColor(vec3 direction, int auroraSteps)
{
	vec3 color = skyGradient(direction);
	vec4 cloud = clouds(direction);
	//The polar lights shine through the gaps of the clouds
	color += aurora(direction, auroraSteps) * (1.0 - cloud.a);
	return mix(color, cloud.rgb, cloud.a);
}

#endif

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL
//Physically based lighting of the scene: the sun (with cascaded shadow maps), the sky (environment cube map) and the fog.
//Texture units: 1 shadow maps, 5 environment (BIBO/Render/Renderer.hpp)
#include "frame.glsl"

layout(binding = 1) uniform sampler2DArrayShadow shadowMaps;
layout(binding = 5) uniform samplerCube environmentMap;

const float PI = 3.14159265;
//Mip levels of the environment map (512 x 512 faces)
const float ENVIRONMENT_MIPS = 9.0;

/**
* What a point of a surface is like
*/
struct Surface
{
	vec3 albedo; //linear color
	float roughness;
	float metallic;
	vec3 normal; //world space, normalized
	float occlusion; //of the ambient light (0 to 1)
	float translucency; //thin surfaces (leaves, needles, grass) let the sun shine through
	vec3 emission;
	float specular; //1: as much reflection as the Fresnel term says, less for foliage (a clump of needles hardly reflects)
};

Surface defaultSurface(vec3 albedo, vec3 normal)
{
	return Surface(albedo, 0.8, 0.0, normal, 1.0, 0.0, vec3(0.0), 1.0);
}

/**
* @return 1 in the sun, 0 in the shadow (soft at the border)
*/
float sunShadow(vec3 position, vec3 normal)
{
	if (frame.sunDirectionShadows.w < 0.5)
	{
		return 1.0;
	}
	float depth = -(frame.view * vec4(position, 1.0)).z;
	int cascadeCount = int(frame.shadowSettings.x);
	int cascade = cascadeCount;
	for (int i = 0; i < cascadeCount; ++i)
	{
		if (depth < frame.cascadeSplits[i])
		{
			cascade = i;
			break;
		}
	}
	if (cascade >= cascadeCount)
	{
		return 1.0;
	}
	vec3 L = sunDirection();
	//Moved along the normal by about a texel: no shadow acne on surfaces that face away from the sun a bit
	float texel = frame.cascadeTexelSizes[cascade];
	float slope = clamp(1.0 - dot(normal, L), 0.0, 1.0);
	vec3 offsetPosition = position + normal * texel * (0.8 + 1.6 * slope);
	vec4 lightSpace = frame.lightSpace[cascade] * vec4(offsetPosition, 1.0);
	vec3 projected = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
	if (any(lessThan(projected, vec3(0.0))) || any(greaterThan(projected, vec3(1.0))))
	{
		return 1.0;
	}
	//3 x 3 samples, each compared with bilinear filtering by the hardware
	vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
	float lit = 0.0;
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			lit += texture(shadowMaps, vec4(projected.xy + vec2(x, y) * texelSize * 1.2, float(cascade), projected.z - 0.0002));
		}
	}
	lit /= 9.0;
	//The last cascade fades out
	if (cascade == cascadeCount - 1)
	{
		float start = cascade > 0 ? frame.cascadeSplits[cascade - 1] : 0.0;
		lit = mix(lit, 1.0, smoothstep(mix(start, frame.cascadeSplits[cascade], 0.8), frame.cascadeSplits[cascade], depth));
	}
	return lit;
}

vec3 fresnelSchlick(float cosine, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosine, 5.0);
}

float distributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

/**
* Scale and bias of F0 for the reflection of the environment (analytic approximation of the split sum, Karis 2014)
*/
vec2 environmentBRDF(float NdotV, float roughness)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	return vec2(-1.04, 1.04) * a004 + r.zw;
}

/**
* The light of the sky from a direction (blurred), used for the ambient light and the fog
*/
vec3 skyLight(vec3 direction, float blur)
{
	return textureLod(environmentMap, direction, blur * ENVIRONMENT_MIPS).rgb;
}

/**
* Light that leaves the surface towards the camera
* @param shadow 1 in the sun, 0 in the shadow (sunShadow)
*/
vec3 shade(Surface surface, vec3 position, float shadow)
{
	vec3 N = surface.normal;
	vec3 V = normalize(cameraPosition() - position);
	vec3 L = sunDirection();
	vec3 H = normalize(L + V);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);
	float roughness = clamp(surface.roughness, 0.04, 1.0);
	vec3 F0 = mix(vec3(0.04), surface.albedo, surface.metallic);
	vec3 sun = frame.sunColorNormalMapping.rgb;

	//Sun: Lambert diffuse and GGX specular (Cook-Torrance)
	vec3 F = fresnelSchlick(HdotV, F0);
	vec3 specular = distributionGGX(NdotH, roughness) * geometrySmith(NdotV, NdotL, roughness) * F / max(4.0 * NdotV * NdotL, 1e-4) * surface.specular;
	vec3 diffuse = (1.0 - F) * (1.0 - surface.metallic) * surface.albedo / PI;
	vec3 color = (diffuse + specular) * sun * NdotL * shadow;
	//Thin surfaces glow when the sun is behind them
	float through = pow(max(dot(-V, L), 0.0), 3.0) * 0.7 + 0.3 * max(-dot(N, L), 0.0);
	color += surface.translucency * surface.albedo * sun * through * shadow * 0.5;

	//Sky: the blurred environment as irradiance, its reflection blurred by the roughness
	vec3 irradiance = skyLight(N, 0.85);
	vec3 kd = (1.0 - fresnelSchlick(NdotV, F0)) * (1.0 - surface.metallic);
	color += kd * surface.albedo * irradiance * surface.occlusion;
	vec3 reflected = reflect(-V, N);
	reflected.y = max(reflected.y, -0.2); //the ground below reflects the ground color of the environment
	vec2 brdf = environmentBRDF(NdotV, roughness);
	float specularOcclusion = clamp(pow(NdotV + surface.occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + surface.occlusion, 0.0, 1.0);
	color += skyLight(reflected, roughness) * (F0 * brdf.x + brdf.y) * specularOcclusion * surface.specular;

	return color + surface.emission;
}

/**
* How much of the light between the camera and the point is replaced by the fog (exponential height fog)
*/
float fogAmount(vec3 position)
{
	vec3 ray = position - cameraPosition();
	float distance = length(ray);
	float density = frame.fogDensity.x, falloff = frame.fogDensity.y;
	float start = cameraPosition().y - frame.fogDensity.z;
	//Density along the ray: density * exp(-falloff * (height - base)), integrated
	float rise = ray.y * falloff;
	float integral = density * exp(-falloff * start) * distance * (abs(rise) > 1e-4 ? (1.0 - exp(-rise)) / rise : 1.0);
	return min(1.0 - exp(-integral), frame.fogDensity.w);
}

vec3 fogColor(vec3 direction)
{
	//The sky close to the horizon in this direction, brighter towards the sun
	vec3 horizon = normalize(vec3(direction.x, max(direction.y, 0.03), direction.z));
	vec3 color = skyLight(horizon, 0.62);
	float towardsSun = max(dot(direction, sunDirection()), 0.0);
	return color + frame.sunColorNormalMapping.rgb * pow(towardsSun, 6.0) * frame.shadowSettings.y;
}

vec3 applyFog(vec3 color, vec3 position)
{
	vec3 direction = normalize(position - cameraPosition());
	return mix(color, fogColor(direction), fogAmount(position));
}

#endif

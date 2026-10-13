#version 430 core
/*
* Shading of the vegetation and the stones: every vertex says which material it is (VegetationMeshes.hpp). Bark, needles,
* leaves and stones use the generated material textures; snow lies on the parts that face up.
*/
#include "include/lighting.glsl"
#include "include/noise.glsl"

in vec3 worldPosition;
in vec3 worldNormal;
in vec3 modelPosition;
in vec2 textureCoordinates;
in vec3 vertexColor;
in vec4 vertexParameters;
in vec2 instanceVariation;

layout(binding = 3) uniform sampler2DArray materialAlbedo;
layout(binding = 4) uniform sampler2DArray materialNormal;

layout(location = 0) out vec4 fragColor;

//Vegetation materials (VegetationMeshes.hpp)
const int PLAIN = 0, BARK = 1, NEEDLES = 2, LEAVES = 3, STONE = 4, CAP_SPOTTED = 5, STEM = 6, GLOW = 7, WOOD = 8, GRASS = 9;
//Layers of the material textures (MaterialTextures.hpp)
const float LAYER_SNOW = 0.0, LAYER_FOREST = 1.0, LAYER_ROCK = 3.0, LAYER_BARK = 5.0, LAYER_NEEDLES = 6.0, LAYER_LEAVES = 7.0;

/**
* Tangent frame from the screen space derivatives (no tangents in the vertices)
*/
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv)
{
	vec3 dp1 = dFdx(p), dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
	vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
	float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
	return mat3(T * invmax, B * invmax, N);
}

vec3 perturb(vec3 N, vec2 uv, float layer, float strength)
{
	if (frame.sunColorNormalMapping.w < 0.5)
	{
		return N;
	}
	vec2 xy = (texture(materialNormal, vec3(uv, layer)).xy * 2.0 - 1.0) * strength;
	vec3 tangentNormal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
	return normalize(cotangentFrame(N, worldPosition, uv) * tangentNormal);
}

void main()
{
	int material = int(round(vertexParameters.x * 255.0));
	vec3 N = normalize(worldNormal);
	if (!gl_FrontFacing)
	{
		N = -N; //grass and ferns are seen from both sides
	}
	vec3 geometricNormal = N;
	float tint = instanceVariation.x;
	vec3 albedo = vertexColor * (1.0 + 0.12 * tint);
	float roughness = 0.8;
	float translucency = 0.0;
	vec3 emission = vec3(0.0);
	float occlusion = vertexParameters.w;
	float specular = 1.0;
	float snowCapacity = vertexParameters.z;

	if (material == BARK)
	{
		vec4 bark = texture(materialAlbedo, vec3(textureCoordinates, LAYER_BARK));
		albedo = bark.rgb * vertexColor * 2.2;
		roughness = bark.a;
		N = perturb(N, textureCoordinates, LAYER_BARK, 1.0);
	}
	else if (material == NEEDLES || material == LEAVES)
	{
		float layer = material == NEEDLES ? LAYER_NEEDLES : LAYER_LEAVES;
		vec4 leaf = texture(materialAlbedo, vec3(textureCoordinates, layer));
		//Every tree a slightly different green
		vec3 hue = mix(vec3(0.92, 1.0, 0.88), vec3(1.08, 1.0, 1.1), 0.5 + 0.5 * tint);
		albedo = leaf.rgb * vertexColor * hue * 4.5;
		specular = 0.25;
		//Needles and leaves scatter the light, they reflect the sky only a little
		roughness = max(leaf.a, 0.82);
		translucency = material == NEEDLES ? 0.35 : 0.55;
		N = perturb(N, textureCoordinates, layer, 0.8);
	}
	else if (material == STONE)
	{
		//Rock texture projected in model space (the stone keeps its texture when it is turned or scaled)
		vec3 p = modelPosition * 1.6;
		vec3 blend = pow(abs(normalize(worldNormal)), vec3(4.0));
		blend /= blend.x + blend.y + blend.z;
		vec4 rock = texture(materialAlbedo, vec3(p.zy, LAYER_ROCK)) * blend.x + texture(materialAlbedo, vec3(p.xz, LAYER_ROCK)) * blend.y
			+ texture(materialAlbedo, vec3(p.xy, LAYER_ROCK)) * blend.z;
		albedo = rock.rgb * vertexColor * 2.4;
		roughness = rock.a;
		//Moss on the parts that face up
		float moss = smoothstep(0.5, 0.85, N.y + (valueNoise(modelPosition * 3.0 + instanceVariation.y * 10.0) - 0.5) * 0.5);
		albedo = mix(albedo, vec3(0.045, 0.075, 0.02) * (0.8 + 0.4 * valueNoise(modelPosition * 9.0)), moss * 0.75);
		roughness = mix(roughness, 0.95, moss);
	}
	else if (material == CAP_SPOTTED)
	{
		//Fly agaric: white spots on the red cap
		vec3 cells = tiledCells(textureCoordinates + instanceVariation.y, 6.0);
		float spot = 1.0 - smoothstep(0.16, 0.24, cells.x);
		albedo = mix(albedo, vec3(0.85, 0.83, 0.78), spot * step(0.25, cells.z));
		roughness = 0.45;
	}
	else if (material == STEM)
	{
		roughness = 0.7;
		translucency = 0.25;
	}
	else if (material == GLOW)
	{
		//Glowing mushrooms in the caves, slowly pulsing
		float pulse = 0.8 + 0.2 * sin(frameTime() * 1.3 + instanceVariation.y * 6.2831853);
		emission = vertexColor * vertexColor * 14.0 * pulse;
		albedo *= 0.3;
		roughness = 0.4;
	}
	else if (material == WOOD)
	{
		//Year rings on the cut
		float radius = length(textureCoordinates - 0.5) * 2.0;
		float rings = 0.5 + 0.5 * sin(radius * 42.0 + valueNoise(textureCoordinates * 8.0) * 3.0);
		albedo *= mix(0.7, 1.05, rings) * mix(0.75, 1.0, smoothstep(0.85, 0.6, radius));
		roughness = 0.85;
	}
	else if (material == GRASS)
	{
		roughness = 0.55;
		specular = 0.4;
		translucency = 0.6;
	}

	//Snow on everything that faces up and can hold it
	//In patches (more on the upper parts that face the sky), not everywhere
	float patches = valueNoise(worldPosition * 0.9) * 0.6 + valueNoise(worldPosition * 3.1) * 0.4;
	//The amount of snow is how much of the upper sides it covers, where it lies it covers them completely
	float coverage = frame.skyAmbientSnow.w;
	float snow = snowCapacity * smoothstep(0.75, 0.95, geometricNormal.y + (patches - 0.5) * 0.3) * smoothstep(1.0 - coverage - 0.06, 1.0 - coverage + 0.06, patches);
	if (snow > 0.0)
	{
		vec3 snowAlbedo = texture(materialAlbedo, vec3(worldPosition.xz * 0.3, LAYER_SNOW)).rgb;
		albedo = mix(albedo, snowAlbedo, snow);
		roughness = mix(roughness, 0.6, snow);
		translucency *= 1.0 - snow;
		N = normalize(mix(N, geometricNormal, snow));
		occlusion = mix(occlusion, 1.0, snow * 0.5);
	}

	Surface surface = Surface(albedo, roughness, 0.0, N, occlusion, translucency, emission, specular);
	float shadow = sunShadow(worldPosition, geometricNormal);
	vec3 color = shade(surface, worldPosition, shadow);
	fragColor = vec4(applyFog(color, worldPosition), 1.0);
}

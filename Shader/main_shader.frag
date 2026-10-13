#version 430 core
/*
* Physically based shading of the objects of the track (Geometry). The color comes from their texture (or, with
* materialLayer >= 0, from a generated material projected in model space, e.g. the rock of the obstacles), the roughness
* from the shininess of their material. Lit by the sun, the sky and the shadows like the rest of the world.
*/
#include "include/lighting.glsl"

in vec3 worldPosition;
in vec3 worldNormal;
in vec3 modelPosition;
in vec2 fragUV;
in vec3 tangent;
in vec3 bitangent;

uniform struct Material {
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
} material;
uniform sampler2D colorTextureSampler;
uniform sampler2D normalMap;
uniform bool normalMappingEnabled;
uniform bool textureTransparent;
//Layer of the generated materials (MaterialTextures.hpp), -1: the texture of the object
uniform int materialLayer = -1;
uniform float materialScale = 1.0;

layout(binding = 3) uniform sampler2DArray materialAlbedo;
layout(binding = 4) uniform sampler2DArray materialNormal;

layout (location = 0) out vec4 fragColor;

void main()
{
	vec3 N = normalize(worldNormal);
	vec3 albedo;
	float roughness = clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.25, 1.0);
	float alpha = 1.0;
	if (materialLayer >= 0)
	{
		vec3 p = modelPosition * materialScale;
		vec3 blend = pow(abs(normalize(worldNormal)), vec3(4.0));
		blend /= blend.x + blend.y + blend.z;
		float layer = float(materialLayer);
		vec4 color = texture(materialAlbedo, vec3(p.zy, layer)) * blend.x + texture(materialAlbedo, vec3(p.xz, layer)) * blend.y
			+ texture(materialAlbedo, vec3(p.xy, layer)) * blend.z;
		albedo = color.rgb;
		roughness = color.a;
	}
	else
	{
		vec4 textureColor = texture(colorTextureSampler, fragUV);
		//The image files are sRGB
		albedo = pow(textureColor.rgb, vec3(2.2)) * clamp(material.diffuse * 1.25, 0.0, 1.0);
		if (normalMappingEnabled)
		{
			vec3 mapped = normalize(texture(normalMap, fragUV).rgb * 2.0 - 1.0);
			N = normalize(mat3(normalize(tangent), normalize(bitangent), N) * mapped);
		}
		if (textureTransparent)
		{
			alpha = 0.5;
		}
	}
	Surface surface = defaultSurface(albedo, N);
	surface.roughness = roughness;
	vec3 color = shade(surface, worldPosition, sunShadow(worldPosition, normalize(worldNormal)));
	fragColor = vec4(applyFog(color, worldPosition), alpha);
}

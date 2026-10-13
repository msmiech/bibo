#version 430 core
//The terrain of the world (BIBO/World/TerrainRenderer.cpp): the vertices are in world space
#include "include/frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 material; //trail, sand, bare rock, snow
layout(location = 3) in float skyVisibility;

out vec3 worldPosition;
out vec3 worldNormal;
out vec4 surfaceMaterial;
out float visibility;

void main()
{
	worldPosition = position;
	worldNormal = normal;
	surfaceMaterial = material;
	visibility = skyVisibility;
	gl_Position = frame.viewProjection * vec4(position, 1.0);
}

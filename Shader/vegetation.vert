#version 430 core
//Trees, bushes, grass, mushrooms, stones: instanced models (BIBO/World/VegetationRenderer.cpp)
#include "include/vegetation.glsl"

out vec3 worldPosition;
out vec3 worldNormal;
out vec3 modelPosition;
out vec2 textureCoordinates;
out vec3 vertexColor;
out vec4 vertexParameters;
out vec2 instanceVariation;

void main()
{
	worldPosition = placeInstance(worldNormal);
	modelPosition = position;
	textureCoordinates = uv;
	vertexColor = pow(color.rgb, vec3(2.2)); //sRGB to linear
	vertexParameters = parameters;
	instanceVariation = instanceParameters.xy;
	gl_Position = frame.viewProjection * vec4(worldPosition, 1.0);
}

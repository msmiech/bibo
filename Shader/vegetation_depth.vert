#version 430 core
//The vegetation into a shadow map (depth only, with the same wind as vegetation.vert)
#include "include/vegetation.glsl"

uniform mat4 lightSpaceMatrix;

void main()
{
	vec3 worldNormal;
	gl_Position = lightSpaceMatrix * vec4(placeInstance(worldNormal), 1.0);
}

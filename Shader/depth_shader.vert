#version 430 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

layout (location = 0) out vec4 worldPosition;
layout (location = 1) out vec4 worldNormal;

void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(position, 1.0f);
    worldPosition = model * vec4(position, 1.0);
	mat4 normalMatrix = transpose(inverse(model));
    worldNormal = normalize(normalMatrix * vec4(normal, 0.0));
} 
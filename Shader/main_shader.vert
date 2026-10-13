#version 430 core
//The objects of the track (Geometry): rocks, the finish line, the crate, the monitor, ...
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;
layout (location = 3) in vec3 tangentIN;
layout (location = 4) in vec3 bitangentIN;

out vec3 worldPosition;
out vec3 worldNormal;
out vec3 modelPosition;
out vec2 fragUV;
out vec3 tangent;
out vec3 bitangent;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
	vec4 world = model * vec4(position, 1.0);
	gl_Position = projection * view * world;
	worldPosition = world.xyz;
	mat3 normalMatrix = transpose(inverse(mat3(model)));
	worldNormal = normalize(normalMatrix * normal);
	modelPosition = position;
	fragUV = uv;
	tangent = normalize(normalMatrix * tangentIN);
	bitangent = normalize(normalMatrix * bitangentIN);
}

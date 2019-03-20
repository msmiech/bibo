#version 430 core
//this code is based on http://www.learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;
layout (location = 3) in vec3 tangentIN;
layout (location = 4) in vec3 bitangentIN;

out vec4 worldPosition;
out vec4 worldNormal;
out vec2 fragUV;
out vec4 fragPosLightSpace;
out vec3 tangent;
out vec3 bitangent;

out float ratio;


uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform vec3 lightPosition;
uniform vec3 worldEye;


void main()
{
    gl_Position = projection * view * model * vec4(position, 1.0);
    worldPosition = model * vec4(position, 1.0);
	mat4 normalMatrix = transpose(inverse(model));
    worldNormal = normalize(normalMatrix * vec4(normal, 0.0));
    fragUV = uv;
    fragPosLightSpace = lightSpaceMatrix * worldPosition;

	tangent = normalize(mat3(normalMatrix) * tangentIN);
    bitangent = normalize(mat3(normalMatrix) * bitangentIN);
}
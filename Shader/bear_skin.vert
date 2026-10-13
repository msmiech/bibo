#version 430 core
//Skin of the bear (under the fur), its eyes, nose and mouth. The vertices are already skinned and in world space.
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec4 color; //rgb: color, a: fur length
layout (location = 3) in float surface; //SkinnedModel::Surface: 0 fur, 1 eye, 2 nose, 3 mouth

uniform mat4 view;
uniform mat4 projection;

out vec3 worldPosition;
out vec3 worldNormal;
out vec4 vertexColor;
flat out int surfaceType;

void main()
{
	worldPosition = position;
	worldNormal = normal;
	vertexColor = color;
	surfaceType = int(surface + 0.5);
	gl_Position = projection * view * vec4(position, 1.0);
}

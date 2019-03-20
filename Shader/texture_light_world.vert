#version 440
 
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;
//layout (location = 3) in int face;
 
out vec4 worldPosition;
out vec4 worldNormal;
out vec2 fragUV;
 
// Model, View, Projection matrix.
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
 
void main()
{
	fragUV = uv;
	mat4 normalMatrix = transpose(inverse(model));
	worldNormal = normalMatrix * vec4(normal, 0.0f);
	worldPosition = (model * vec4(position, 1.0f));
	gl_Position = projection * view * model * vec4(position, 1.0f);
}

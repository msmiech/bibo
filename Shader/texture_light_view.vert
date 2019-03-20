#version 440
 
 /*
 * Our Vertex Shader: We do Phong light calculation in world space;
 */

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;
 
out vec4 ViewPosition;
out vec4 ViewNormal;
out vec2 FragUV;
out vec3 ViewLight;
 
// Model, View, Projection matrix.
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform vec3 worldLight;
 
void main()
{
	FragUV = uv;
	mat4 model_view = view * model;
	gl_Position = projection * model_view * vec4(position, 1.0f);
	ViewPosition = (model_view * vec4(position, 1.0f));
	mat4 normalMatrix = transpose(inverse(model_view));
	ViewNormal = normalMatrix * vec4(normal, 0.0f);
	ViewLight = vec3(view * vec4(worldLight, 1.0f));

}

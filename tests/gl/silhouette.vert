#version 430 core
//Test shader: where geometry is drawn, without lighting or depth (tests/gl/SceneTest.cpp)

layout (location = 0) in vec3 position;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
	gl_Position = projection * view * model * vec4(position, 1.0);
}

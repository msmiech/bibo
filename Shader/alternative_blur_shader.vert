#version 330
//From https://github.com/cforfang/opengl-shadowmapping

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 uv;

out vec2 fragUV;

void main()
{
		gl_Position = vec4(position, 1.0);
		fragUV =  uv;
};

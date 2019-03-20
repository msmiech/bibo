#version 330 core
//Based on http://learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
layout (location = 0) out vec4 color;
layout (location = 1) out vec4 brightColor;

void main()
{
    color = vec4(3.0f, 3.0f, 3.0f, 1.0f);
	brightColor = color;
}
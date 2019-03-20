#version 330 core
//Based on http://learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
layout (location = 0) in vec3 position;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(position, 1.0f);
} 
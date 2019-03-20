#version 430 core
//Based on http://learnopengl.com/#!Advanced-Lighting/Bloom
//Used for combining HDR effects with the 
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uv;

out vec2 fragUV;

void main()
{
    gl_Position = vec4(position, 1.0f);
    fragUV = uv;
}
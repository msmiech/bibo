#version 330 core
//Based on http://learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
out vec4 color;
in vec2 fragUV;

uniform sampler2D shadowMap;
uniform float near_plane;
uniform float far_plane;

float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // Back to NDC 
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));	
}

void main()
{             
    float depthValue = texture(shadowMap, fragUV).x;
    // color = vec4(vec3(LinearizeDepth(depthValue) / far_plane), 1.0); // perspective
    color = vec4(vec3(depthValue), 1.0); // orthographic
}
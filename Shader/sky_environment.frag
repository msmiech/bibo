#version 430 core
/*
* Renders a face of the environment cube map (BIBO/World/SkyRenderer.cpp): the sky in every direction, without the disk
* of the sun. Its blurred mip levels are the ambient light of the scene, the reflections and the color of the fog.
*/
#include "include/atmosphere.glsl"

in vec2 uv;

uniform int face; //GL_TEXTURE_CUBE_MAP_POSITIVE_X + face
uniform int auroraSteps;

layout(location = 0) out vec4 fragColor;

vec3 faceDirection(int index, vec2 st)
{
	vec2 c = st * 2.0 - 1.0;
	if (index == 0) return vec3(1.0, -c.y, -c.x);
	if (index == 1) return vec3(-1.0, -c.y, c.x);
	if (index == 2) return vec3(c.x, 1.0, c.y);
	if (index == 3) return vec3(c.x, -1.0, -c.y);
	if (index == 4) return vec3(c.x, -c.y, 1.0);
	return vec3(-c.x, -c.y, -1.0);
}

void main()
{
	vec3 direction = normalize(faceDirection(face, uv));
	fragColor = vec4(skyColor(direction, auroraSteps), 1.0);
}

#ifndef FRAME_GLSL
#define FRAME_GLSL
//Data of the frame, the same for all shaders of the scene (BIBO/Render/FrameUniforms.hpp, uniform buffer binding 0)
layout(std140, binding = 0) uniform Frame
{
	mat4 view;
	mat4 projection;
	mat4 viewProjection;
	mat4 inverseViewProjection;
	mat4 lightSpace[4]; //shadow cascades: world to light space
	vec4 cascadeSplits; //view depth where every cascade ends
	vec4 cascadeTexelSizes; //size of a texel of every cascade in world units
	vec4 cameraPositionTime; //xyz: camera, w: time in seconds
	vec4 sunDirectionShadows; //xyz: towards the sun, w: 1 if shadows are enabled
	vec4 sunColorNormalMapping; //rgb: radiance of the sun, w: 1 if normal mapping is enabled
	vec4 skyAmbientSnow; //rgb: average light from the sky, w: how much snow lies on things (0 to 1)
	vec4 groundAmbient; //rgb: average light from below, w: aurora intensity
	vec4 fogDensity; //x: density, y: height falloff, z: height where the density is x, w: maximum opacity
	vec4 wind; //xy: direction, z: strength, w: unused
	vec4 viewport; //width, height, 1 / width, 1 / height
	vec4 worldMap; //xy: world position of the corner of the world map, z: its size, w: sea level
	vec4 shadowSettings; //x: number of cascades, y: strength of the sun inscattering in the fog
} frame;

vec3 cameraPosition()
{
	return frame.cameraPositionTime.xyz;
}

float frameTime()
{
	return frame.cameraPositionTime.w;
}

vec3 sunDirection()
{
	return frame.sunDirectionShadows.xyz;
}

#endif

#version 430 core
//The sea: a large plane at the sea level that moves with the camera (4 vertices as a triangle strip, no attributes)
#include "include/frame.glsl"

uniform float extent; //half the size of the plane

out vec3 worldPosition;

void main()
{
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	//Snapped to whole units, so the waves do not move with the camera
	vec2 center = floor(cameraPosition().xz);
	worldPosition = vec3(center.x + corner.x * extent, frame.worldMap.w, center.y + corner.y * extent);
	gl_Position = frame.viewProjection * vec4(worldPosition, 1.0);
}

#version 430 core
/*
* Falling snow flakes (BIBO/World/SnowRenderer.cpp): every instance is a flake in a box around the camera, its path is
* computed from its number and the time (no data per flake). The box wraps around, so the camera is always in the snow.
* Under the roof of a cave there is no snow.
*/
#include "include/frame.glsl"
#include "include/noise.glsl"

uniform vec3 boxSize;
uniform float fallSpeed;
layout(binding = 6) uniform sampler2D worldMap; //g: top of a cave roof

out vec2 corner;
out float opacity;
out vec3 worldPosition;

void main()
{
	float id = float(gl_InstanceID);
	vec3 seed = vec3(hash11(id * 1.317 + 0.1), hash11(id * 7.171 + 0.3), hash11(id * 3.733 + 0.7));
	float time = frameTime();
	vec3 wind = vec3(frame.wind.x, 0.0, frame.wind.y) * frame.wind.z * 0.7;
	float speed = fallSpeed * mix(0.7, 1.3, hash11(id * 9.13));
	vec3 position = seed * boxSize + wind * time - vec3(0.0, speed * time, 0.0);
	//Flakes tumble a bit
	float phase = hash11(id * 4.91) * 6.2831853;
	position += vec3(sin(time * 1.1 + phase), 0.0, cos(time * 0.9 + phase * 1.3)) * 0.35;
	vec3 origin = cameraPosition() - 0.5 * boxSize;
	position = origin + mod(position - origin, boxSize);

	vec2 mapPosition = (position.xz - frame.worldMap.xy) / frame.worldMap.z;
	float roof = texture(worldMap, clamp(mapPosition, 0.0, 1.0)).g;
	float distance = length(position - cameraPosition());
	opacity = (1.0 - smoothstep(0.42 * boxSize.x, 0.5 * boxSize.x, distance)) * smoothstep(1.0, 4.0, distance);
	if (position.y < roof)
	{
		opacity = 0.0;
	}

	//A square facing the camera, at least about a pixel large (smaller ones would flicker)
	float size = mix(0.012, 0.03, hash11(id * 5.37));
	float pixel = 2.0 * distance / (frame.projection[1][1] * frame.viewport.y);
	float drawnSize = max(size, 1.4 * pixel);
	opacity *= size / drawnSize;
	vec3 right = vec3(frame.view[0][0], frame.view[1][0], frame.view[2][0]);
	vec3 up = vec3(frame.view[0][1], frame.view[1][1], frame.view[2][1]);
	corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	worldPosition = position + (right * corner.x + up * corner.y) * drawnSize;
	gl_Position = opacity > 0.0 ? frame.viewProjection * vec4(worldPosition, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}

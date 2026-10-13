#version 430 core
//A snow flake: a soft white disk, lit by the sky and the sun
#include "include/lighting.glsl"

in vec2 corner;
in float opacity;
in vec3 worldPosition;

layout(location = 0) out vec4 fragColor;

void main()
{
	float radius = length(corner);
	float alpha = (1.0 - smoothstep(0.35, 1.0, radius)) * opacity;
	if (alpha < 0.01)
	{
		discard;
	}
	vec3 light = skyLight(vec3(0.0, 1.0, 0.0), 0.9) * 0.9 + frame.sunColorNormalMapping.rgb * 0.25;
	vec3 color = vec3(0.95, 0.97, 1.0) * light;
	fragColor = vec4(applyFog(color, worldPosition), alpha * 0.9);
}

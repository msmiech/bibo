#version 430 core
/*
* The sky behind everything (drawn after the opaque geometry, at the far plane): the environment cube map and the sun
*/
#include "include/frame.glsl"

in vec2 uv;

layout(binding = 5) uniform samplerCube environmentMap;

layout(location = 0) out vec4 fragColor;

void main()
{
	vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	vec4 world = frame.inverseViewProjection * clip;
	vec3 direction = normalize(world.xyz / world.w - cameraPosition());
	vec3 color = textureLod(environmentMap, direction, 0.0).rgb;
	//The disk of the sun (0.55 degrees), darker at its edge
	float cosine = dot(direction, sunDirection());
	float disk = smoothstep(0.99995, 0.99997, cosine);
	float limb = mix(0.6, 1.0, smoothstep(0.99995, 0.99999, cosine));
	color += frame.sunColorNormalMapping.rgb * 60.0 * disk * limb * smoothstep(-0.01, 0.01, direction.y);
	fragColor = vec4(color, 1.0);
}

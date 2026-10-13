#version 430 core
/*
* The bright parts of the image for the bloom, at half the resolution: what is brighter than the threshold after the
* exposure (a soft knee instead of a hard edge)
*/
in vec2 uv;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 2) uniform sampler2D exposureTexture; //1 x 1: exposure of the frame
uniform float threshold;

out vec4 fragColor;

void main()
{
	//4 bilinear samples: the average of 4 x 4 texels of the full image
	vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));
	vec3 color = 0.25 * (texture(sceneColor, uv + texel * vec2(-1.0, -1.0)).rgb + texture(sceneColor, uv + texel * vec2(1.0, -1.0)).rgb
		+ texture(sceneColor, uv + texel * vec2(-1.0, 1.0)).rgb + texture(sceneColor, uv + texel * vec2(1.0, 1.0)).rgb);
	color *= texelFetch(exposureTexture, ivec2(0), 0).r;
	float brightness = max(color.r, max(color.g, color.b));
	const float KNEE = 0.5;
	float soft = clamp(brightness - threshold + KNEE, 0.0, 2.0 * KNEE);
	soft = soft * soft / (4.0 * KNEE + 1e-4);
	float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
	fragColor = vec4(min(color * contribution, vec3(64.0)), 1.0);
}

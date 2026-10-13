#version 430 core
/*
* Eye adaptation: the exposure moves towards the one for the average luminance of the image (slowly, like the eyes when
* the bear runs into a cave). Written to a 1 x 1 texture, the previous exposure is the other one of two textures.
*/
layout(binding = 0) uniform sampler2D logLuminance; //with mip levels
layout(binding = 1) uniform sampler2D previousExposure;
uniform float timeDelta;
uniform bool reset;
//Average luminance that the exposure maps to middle gray, and the limits of the exposure
uniform float key;
uniform vec2 exposureRange;

out float exposure;

void main()
{
	int lastLevel = textureQueryLevels(logLuminance) - 1;
	float average = exp(texelFetch(logLuminance, ivec2(0), lastLevel).r);
	float target = clamp(key / max(average, 1e-4), exposureRange.x, exposureRange.y);
	float previous = texelFetch(previousExposure, ivec2(0), 0).r;
	//Faster when it gets brighter (as the eyes)
	float rate = target < previous ? 2.5 : 1.2;
	exposure = reset || !(previous > 0.0) ? target : mix(previous, target, 1.0 - exp(-timeDelta * rate));
}

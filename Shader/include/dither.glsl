#ifndef DITHER_GLSL
#define DITHER_GLSL
//Cross-fades between levels of detail without blending (screen door): every model covers a part of the pixels

/**
* Interleaved gradient noise (Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", 2014):
* a threshold from 0 to 1 for every pixel, evenly spread over every small area of the screen
*/
float ditherThreshold()
{
	return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

/**
* @param fade 1: fully there. From 0 to 1: appearing, on this part of the pixels. From -1 to 0: disappearing, on exactly
* the pixels that a model with fade + 1 does not cover (the old and the new model of a cross-fade cover every pixel once).
* @return true if the pixel is left out
*/
bool ditheredAway(float fade)
{
	float threshold = ditherThreshold();
	return fade >= 0.0 ? threshold >= fade : threshold < fade + 1.0;
}

#endif

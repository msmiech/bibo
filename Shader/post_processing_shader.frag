#version 430 core
/*
* Final image: the HDR image with its bloom, exposed (auto exposure), tone mapped (ACES filmic curve) and gamma corrected,
* with a slight vignette and dithering (no banding in the gradients of the sky).
* Without HDR only the gamma correction (bright parts are clipped).
*/
in vec2 uv;

layout(binding = 0) uniform sampler2D sceneTex;
layout(binding = 1) uniform sampler2D bloomTex;
layout(binding = 2) uniform sampler2D exposureTexture;
uniform bool hdrEnabled;
uniform bool bloomEnabled;
uniform float bloomStrength;

out vec4 fragColor;

//Fit of the ACES filmic tone mapping curve (Krzysztof Narkowicz)
vec3 aces(vec3 x)
{
	const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
	const float GAMMA = 2.2;
	vec3 color = texture(sceneTex, uv).rgb;
	if (hdrEnabled)
	{
		float exposure = texelFetch(exposureTexture, ivec2(0), 0).r;
		color *= exposure;
		if (bloomEnabled)
		{
			//The bloom is already exposed (bright pass)
			color += texture(bloomTex, uv).rgb * bloomStrength;
		}
		color = aces(color);
		//Vignette
		vec2 centered = uv - 0.5;
		color *= 1.0 - 0.28 * dot(centered, centered) * 2.0;
	}
	color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / GAMMA));
	//Dithering against banding (less than one step of 8 bit)
	float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
	color += (noise - 0.5) / 255.0;
	fragColor = vec4(color, 1.0);
}

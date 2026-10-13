#version 430 core
//Logarithm of the luminance of the image in a small texture, its smallest mip level is the average (auto exposure)
in vec2 uv;

layout(binding = 0) uniform sampler2D sceneColor;

out float logLuminance;

void main()
{
	vec3 color = texture(sceneColor, uv).rgb;
	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	logLuminance = log(max(luminance, 1e-4));
}

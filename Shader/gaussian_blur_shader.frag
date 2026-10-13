#version 430 core
//Separable Gaussian blur of the bloom (horizontal or vertical), based on http://learnopengl.com/#!Advanced-Lighting/Bloom
in vec2 uv;

layout(binding = 0) uniform sampler2D image;
uniform bool horizontal;

out vec4 fragColor;

const float WEIGHTS[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

void main()
{
	vec2 texel = 1.0 / vec2(textureSize(image, 0));
	vec2 step = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
	vec3 result = texture(image, uv).rgb * WEIGHTS[0];
	for (int i = 1; i < 5; ++i)
	{
		result += texture(image, uv + step * float(i)).rgb * WEIGHTS[i];
		result += texture(image, uv - step * float(i)).rgb * WEIGHTS[i];
	}
	fragColor = vec4(result, 1.0);
}

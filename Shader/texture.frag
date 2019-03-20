#version 440 core

in vec3 worldNormal;
in vec2 fragmentUV;

layout (location = 0) out vec4 fragColor;

uniform sampler2D diffuseSampler;

void main() {
	vec3 textureColor = texture(diffuseSampler, fragmentUV).rgb;
	fragColor = vec4(textureColor, 1.0);
}
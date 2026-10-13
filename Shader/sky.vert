#version 430 core
//A triangle that covers the screen at the far plane (the sky is behind everything)

out vec2 uv;

void main()
{
	vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	uv = position;
	gl_Position = vec4(position * 2.0 - 1.0, 1.0, 1.0);
}

#version 430 core
//A triangle that covers the whole screen (draw 3 vertices without attributes), for full screen passes

out vec2 uv;

void main()
{
	vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	uv = position;
	gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}

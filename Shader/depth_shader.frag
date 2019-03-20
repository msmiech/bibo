#version 430 core
//Based on: https://github.com/cforfang/opengl-shadowmapping/blob/master/src/vsm/shadowFragmentShader.glsl
//Based on: http://codeflow.org/entries/2013/feb/15/soft-shadow-mapping/

in vec4 worldPosition;
in vec4 worldNormal;

uniform mat4 lightView;

out vec4 outColor;

void main()
{             
	vec3 worldNormal = vec3(normalize(worldNormal));
	vec3 lightPos = (lightView * worldPosition).xyz;
	float depth = clamp(length(lightPos) / 40.0, 0.0, 1.0);

	float dx = dFdx(depth);
	float dy = dFdy(depth);
	outColor = vec4(depth, depth * depth + 0.25 * (dx * dx + dy * dy), 0.0, 1.0);

	//The following is default behaviour
    gl_FragDepth = gl_FragCoord.z;
}  
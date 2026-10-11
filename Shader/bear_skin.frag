#version 430 core
/*
* Skin of the bear: where the fur grows it is the dark ground of the fur (between the strands), the paw pads and lips are bare skin.
* Eyes: the vertex color is the direction from the center of the eye in eye space (gaze along +Z); iris, pupil and the
* white of the eye are drawn from it, the cornea is glossy and always has a small catchlight (as if there was a light next to the camera).
* Nose, inside of the mouth, tongue, teeth and claws are wet or glossy.
*/
in vec3 worldPosition;
in vec3 worldNormal;
in vec4 vertexColor;
flat in int surfaceType;
in vec4 fragPosLightSpace;

uniform struct Light {
	uint type;
	vec3 position;
	vec3 direction;
	vec3 ambientColor;
	vec3 diffuseColor;
	vec3 specularColor;
} light;
uniform sampler2D shadowMap;
uniform bool shadowMappingEnabled;
uniform vec3 worldEye;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 brightColor;

const int FUR = 0;
const int EYE = 1;
const int NOSE = 2;
const int MOUTH = 3;
const int TONGUE = 4;
const int TEETH = 5;
const int CLAW = 6;

float shadowCalculation(vec4 positionLightSpace, float bias)
{
	vec3 projected = positionLightSpace.xyz / positionLightSpace.w * 0.5 + 0.5;
	if (projected.z > 1.0)
	{
		return 0.0;
	}
	float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			float depth = texture(shadowMap, projected.xy + vec2(x, y) * texelSize).r;
			shadow += projected.z - bias > depth ? 1.0 : 0.0;
		}
	}
	return shadow / 9.0;
}

/**
* Color of the eye where it is seen from the center in the direction d (eye space)
*/
vec3 eyeColor(vec3 d)
{
	const float PUPIL = 0.28; //angular radius (radians) from the gaze
	const float IRIS = 0.95;
	float angle = acos(clamp(d.z, -1.0, 1.0));
	float azimuth = atan(d.y, d.x);
	//Iris of a young brown bear: very dark brown, a bit warmer around the pupil, radial fibres and a dark ring at its edge
	float radial = smoothstep(PUPIL, IRIS, angle);
	float fibres = 0.5 + 0.5 * sin(azimuth * 41.0 + 2.5 * sin(azimuth * 9.0) + angle * 6.0);
	fibres *= 0.7 + 0.3 * sin(angle * 55.0);
	vec3 iris = mix(vec3(0.075, 0.036, 0.014), vec3(0.018, 0.009, 0.005), smoothstep(0.0, 0.8, radial));
	iris *= 0.75 + 0.5 * fibres;
	iris = mix(iris, vec3(0.015, 0.009, 0.006), 0.85 * smoothstep(IRIS - 0.14, IRIS, angle));
	//Bears show only a little of the white of the eye, and it is not very white
	vec3 sclera = vec3(0.07, 0.045, 0.032) * (0.8 + 0.2 * smoothstep(1.4, 0.9, angle));
	vec3 color = mix(iris, sclera, smoothstep(IRIS, IRIS + 0.04, angle));
	color = mix(color, vec3(0.003), 1.0 - smoothstep(PUPIL - 0.035, PUPIL, angle));
	//The upper lid shades the eye
	return color * mix(1.0, 0.45, smoothstep(0.05, 0.75, d.y));
}

void main()
{
	vec3 N = normalize(worldNormal);
	vec3 L = normalize(-light.direction);
	vec3 V = normalize(worldEye - worldPosition);
	vec3 H = normalize(L + V);
	float NdotL = dot(N, L);

	vec3 albedo = vertexColor.rgb;
	float furLength = vertexColor.a;
	float shininess = 8.0;
	float glossiness = 0.0;
	float wrap = 0.3; //light reaching around the surface (soft skin)
	float occlusion = 1.0;
	if (surfaceType == FUR)
	{
		//Between the strands the fur is in its own shadow
		albedo *= mix(1.0, 0.45, clamp(furLength * 4.0, 0.0, 1.0));
		glossiness = furLength < 0.05 ? 0.08 : 0.0; //paw pads, lips
	}
	else if (surfaceType == EYE)
	{
		albedo = eyeColor(normalize(vertexColor.rgb * 2.0 - 1.0));
		shininess = 300.0;
		glossiness = 1.2;
		wrap = 0.1;
	}
	else if (surfaceType == NOSE)
	{
		shininess = 50.0;
		glossiness = 0.45;
	}
	else if (surfaceType == MOUTH)
	{
		shininess = 60.0;
		glossiness = 0.35;
		occlusion = 0.55; //deep in the mouth
	}
	else if (surfaceType == TONGUE)
	{
		shininess = 40.0;
		glossiness = 0.4;
		wrap = 0.6; //the light shines a bit through the tongue
	}
	else if (surfaceType == TEETH)
	{
		shininess = 30.0;
		glossiness = 0.3;
		occlusion = 0.8;
	}
	else if (surfaceType == CLAW)
	{
		shininess = 25.0;
		glossiness = 0.35; //smooth horn
	}

	float diffuse = clamp((NdotL + wrap) / (1.0 + wrap), 0.0, 1.0);
	float specular = glossiness * pow(max(dot(N, H), 0.0), shininess) * step(0.0, NdotL);
	float shadow = shadowMappingEnabled ? shadowCalculation(fragPosLightSpace, max(0.01 * (1.0 - NdotL), 0.002)) : 0.0;

	vec3 color = occlusion * (light.ambientColor * 0.25 * albedo
		+ (1.0 - shadow) * (light.diffuseColor * albedo * diffuse + light.specularColor * specular));

	//Catchlight: reflection of a light above the camera, so the eyes sparkle from every side
	if (surfaceType == EYE || surfaceType == NOSE || surfaceType == TONGUE)
	{
		vec3 cameraLight = normalize(V + vec3(0.0, 0.6, 0.0));
		float sharpness = surfaceType == EYE ? 500.0 : 120.0;
		float sparkle = pow(max(dot(N, normalize(cameraLight + V)), 0.0), sharpness);
		color += vec3(surfaceType == EYE ? 2.5 : 0.5) * sparkle * occlusion;
		//Fresnel: glossy surfaces reflect the bright sky at grazing angles
		color += glossiness * 0.15 * pow(1.0 - max(dot(N, V), 0.0), 4.0) * light.ambientColor;
	}

	fragColor = vec4(color, 1.0);
	float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
	brightColor = brightness > 1.0 ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}

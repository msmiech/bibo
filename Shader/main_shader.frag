#version 430 core
/** This shader is based on 
* http://learnopengl.com/#!Lighting/Materials
* http://www.3dgep.com/texturing-and-lighting-with-opengl-and-glsl
* http://www.ogldev.org/www/tutorial18/tutorial18.html
* http://www.learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
* http://learnopengl.com/#!Advanced-Lighting/Bloom
*/ 
in vec4 worldPosition;
in vec4 worldNormal;
in vec2 fragUV;
in vec4 fragPosLightSpace;
in vec3 tangent;
in vec3 bitangent;
uniform struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
}

 material;
uniform struct Light {
    uint type;
    //2 = point light, 1 = directional light (sun, etc.)
    vec3 position;
    //for point light (but not actually used by shadow mapping since we have a sun)
	vec3 direction;
    //for directional light
    vec3 ambientColor;
    vec3 diffuseColor;
    vec3 specularColor;
}
 light;
uniform sampler2D colorTextureSampler;
//shadow mapping
uniform sampler2D shadowMap;
//normal mapping
uniform sampler2D normalMap;
uniform vec3 worldEye;
uniform bool shadowMappingEnabled;
uniform bool varianceShadowMappingEnabled;
uniform bool normalMappingEnabled;
uniform bool bloomEnabled;
uniform bool textureTransparent;

//local vars
vec4 scPostW;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 brightColor;



float ShadowCalculation(vec4 fragPosLightSpace, vec3 N, vec3 L)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    //transform coordinates to range [0,1]
	projCoords = projCoords * 0.5 + 0.5;
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = max(0.02 * (1.0 - dot(N, L)), 0.002);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
			shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;
		}
	}
    shadow /= 9.0;
    // Keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if(projCoords.z > 1.0){
        shadow = 0.0;
    }


    return shadow;
}

// From http://fabiensanglard.net/shadowmappingVSM/index.php
float chebyshevUpperBound(float distance)
{
	vec2 moments = texture2D(shadowMap, scPostW.xy).rg;
	
	// Surface is fully lit. as the current fragment is before the light occluder
	if (distance <= moments.x)
		return 1.0;

	// The fragment is either in shadow or penumbra. We now use chebyshev's upperBound to check
	// How likely this pixel is to be lit (p_max)
	float variance = moments.y - (moments.x * moments.x);
	//variance = max(variance, 0.000002);
	variance = max(variance, 0.00002);

	float d = distance - moments.x;
	float p_max = variance / (variance + d * d);

	return p_max;
}


void main()
{
	vec4 textureColor = texture(colorTextureSampler, fragUV);
	if(textureTransparent) 
	{
		textureColor.a = 0.5;
	}

	bool blinn = true; //default true for better performance
    //ambient term
    vec3 ambient = light.ambientColor * material.ambient;
    //diffuse term
	vec3 L;
    if(light.type == 2) 
	{
        //Point light
		L = normalize(light.position - vec3(worldPosition));
        //i.e. direction from vertex position in world space towards the light source position in world space
	} 
	else if(light.type == 1) 
	{
        //Directional light
		L = normalize(-light.direction);
    }


	L = normalize(-light.direction);
    vec3 N;
    vec3 V;
    N = vec3(normalize(worldNormal));
    //normal mapping
	if(normalMappingEnabled) 
	{
        // Obtain normal from normal map in range [0,1]
		mat3 TBN = mat3(tangent, bitangent, N);
        N = texture(normalMap, fragUV).rgb;
        // Transform normal vector to range [-1,1]
		N = normalize(N * 2.0 - 1.0);
        N = normalize(TBN * N);
	} 

	V = normalize(worldEye - vec3(worldPosition));
    float NdotL = dot(N, L);
    vec3 diffuse;
    if(NdotL <= 0) 
	{
        diffuse = vec3(0.0f);
    }
	else 
	{
        diffuse = light.diffuseColor * (NdotL * material.diffuse);
    }



	//specular term
	vec3 R;
    if(blinn) 
	{
        R = normalize(L + V);
        //Halfway angle vector (approximation for better performance)
	} else {
        R = reflect(-L, N);
    }


	float VdotR = dot(V,R);
    float spec = 0.0f;
    if(VdotR > 0.0f) {
        spec = pow(VdotR, material.shininess);
    }


    vec3 specular = light.specularColor * (spec * material.specular);

	float shadow = 0.0;
	//Based on https://github.com/cforfang/opengl-shadowmapping
	if(shadowMappingEnabled) 
	{
		if(varianceShadowMappingEnabled) 
		{
			scPostW = fragPosLightSpace / fragPosLightSpace.w;
			//transform coordinates to range [0,1]
			scPostW = scPostW * 0.5 + 0.5;

			bool outsideShadowMap = fragPosLightSpace.w <= 0.0f || (scPostW.x < 0 || scPostW.y < 0) || (scPostW.x >= 1 || scPostW.y >= 1);
			if (!outsideShadowMap) 
			{
				shadow = chebyshevUpperBound(scPostW.z);
			}
		}
		else
		{
			shadow = ShadowCalculation(fragPosLightSpace, N, L);
		}
	}
    //float shadow = shadowMappingEnabled ? ShadowCalculation(fragPosLightSpace, N, L) : 0.0;
    //vec3 lightResult = (ambient + (1.0 - shadow) * (diffuse + specular)) * textureColor;    
	vec4 lightResult = vec4((ambient + (1.0 - shadow) * (diffuse + specular)), 1.0) * textureColor;
    float brightness = dot(lightResult.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
	{
        brightColor = vec4(fragColor);
    }
	//else
	//{
	//	brightColor = vec4(0.0, 0.0, 0.0, 1.0);
    //}

	fragColor = lightResult;
}


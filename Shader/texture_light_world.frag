#version 440 core

/** This shader is based on 
* http://learnopengl.com/#!Lighting/Materials
* http://www.3dgep.com/texturing-and-lighting-with-opengl-and-glsl
* http://www.ogldev.org/www/tutorial18/tutorial18.html
*/ 
uniform struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
} material;

uniform struct Light {
    vec3 position;
	vec3 direction; //for directional light
    vec3 ambientColor;
    vec3 diffuseColor;
	vec3 specularColor;
} light;


in vec4 worldPosition;
in vec4 worldNormal;
in vec2 fragUV;

layout (location=0) out vec4 fragColor;

uniform sampler2D colorTextureSampler;
uniform vec3 worldEye;


void main()
{
	bool blinn = true;

	vec3 textureColor = texture(colorTextureSampler, fragUV).rgb;

	//ambient term
    vec3 ambient = light.ambientColor * material.ambient;

	//diffuse term
    vec3 diffuse;
	vec3 L = normalize(light.position - vec3(worldPosition)); //i.e. direction from vertex position in world space towards the light source position in world space
    vec3 N = vec3(normalize(worldNormal)); //normalized normal direction in world space
    float NdotL = dot(N, L);
	if(NdotL <= 0) {
        diffuse = vec3(0.0f);
    } else {
		diffuse = light.diffuseColor * (NdotL * material.diffuse);
	}

	//specular term
	vec3 V = normalize(worldEye - vec3(worldPosition));
	vec3 R;
	if(blinn) {
		R = normalize(L + V); //Halfway angle vector (approximation for better performance)
	} else {
		R = reflect(-L, N);
	}
	float VdotR = dot(V,R);
	float spec = 0.0f;
	if(VdotR > 0.0f) {
		spec = pow(VdotR, material.shininess);
	}
    vec3 specular = light.specularColor * (spec * material.specular);  
	
	vec3 lightResult = ambient + diffuse + specular;
	fragColor = vec4(lightResult * textureColor, 1.0f);
}
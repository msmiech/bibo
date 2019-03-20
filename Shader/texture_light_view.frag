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
    //vec3 position;
    vec3 ambientColor;
    vec3 diffuseColor;
	vec3 specularColor;
} light;


in vec4 ViewPosition; //Vertex position in view space
in vec4 ViewNormal; //Vertex normal in view space
in vec2 FragUV;
in vec3 ViewLight; //Light position in view space

layout (location=0) out vec4 fragColor;

uniform sampler2D colorTextureSampler;

vec3 lambert(vec3 P, vec3 N, vec3 L, vec3 diffuseColor);
vec3 blinn(vec3 P,
            vec3 N,
			vec3 L,
            vec3 diffuseColor,
            vec3 specularColor,
            float shininess);

void main()
{
	//ambient term
    vec3 ambient = light.ambientColor * material.ambient;
	//diffuse term
    vec3 diffuse;
    vec3 N = vec3(normalize(ViewNormal));
    vec3 L = normalize(ViewLight - vec3(ViewPosition)); //i.e. direction of light
    float NdotL = dot(N, L);
	if(NdotL <= 0) {
        diffuse = vec3(0.0f);
    } else {
		diffuse = light.diffuseColor * NdotL * material.diffuse;
	}
	//specular term
	vec3 V = normalize(vec3(-ViewPosition));
	vec3 R = normalize(reflect(-L, N));
	float VdotR = dot(V,R);
	float spec;
	if(VdotR <= 0.0f) {
		spec = 0.0f;
	} else {
		spec = pow(VdotR, material.shininess);
	}
    vec3 specular = light.specularColor * (spec * material.specular);  
	
	vec3 lightResult = ambient + diffuse + specular;
	vec3 textureColor = texture(colorTextureSampler, FragUV).rgb;
	fragColor = vec4(lightResult * textureColor, 1.0f);
}

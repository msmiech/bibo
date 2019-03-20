#include "Light.hpp"
#include "../Util/Util.hpp"

using namespace bibo;
using namespace scene;

Light::Light(GLFWwindow& window, Shader* shader, aiLight* light) : SceneObject(window)
{
	convertLight(light);

	//Following based on: http://learnopengl.com/#!Lighting/Basic-Lighting
	GLfloat vertices[] = {
		-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
		0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
		0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
		0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
		-0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
		-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,

		-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
		0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
		0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
		0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
		-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
		-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,

		-0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
		-0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
		-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
		-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
		-0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
		-0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f,

		0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
		0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
		0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
		0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
		0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
		0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f,

		-0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
		0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
		0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
		0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
		-0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
		-0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,

		-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
		-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
		-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f
	};

	glGenVertexArrays(1, &lightVAO);
	glBindVertexArray(lightVAO);


	glGenBuffers(1, &lightVBO);

	glBindBuffer(GL_ARRAY_BUFFER, lightVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0); // Note that we skip over the normal vectors
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);
}


void Light::convertLight(aiLight* light)
{
	type = light->mType;

	//position.x = light->mPosition.x;
	//position.y = light->mPosition.y;
	//position.z = light->mPosition.z;
	position = vec3(80.0f, 100.0f, -75.0f);

	//direction.x = light->mDirection.x;
	//direction.y = light->mDirection.y;
	//direction.z = light->mDirection.z;
	//direction = normalize(vec3(0.0f, 0.0f, 0.0f) - position);
	direction = normalize(vec3(0.0f, -0.8f, 0.3f));

	//ambient.r = light->mColorAmbient.r;
	//ambient.g = light->mColorAmbient.g;
	//ambient.b = light->mColorAmbient.b;
	ambient = vec3(0.5f, 0.5f, 0.5f);


	//diffuse = vec3(5.f, 5.f, 5.f);
	diffuse.r = light->mColorDiffuse.r;
	diffuse.g = light->mColorDiffuse.g;
	diffuse.b = light->mColorDiffuse.b;

	//specular = vec3(6.f, 6.f, 6.f);
	specular.r = light->mColorSpecular.r;
	specular.b = light->mColorSpecular.g;
	specular.g = light->mColorSpecular.b;
}


Light::~Light()
{

}

void Light::update(float time_delta)
{

}

void Light::draw(Shader* shader)
{
	GLuint modelLoc = shader->getUniformLocation("model");
	mat4 model = mat4(1.0f);
	model = translate(model, position);
	model = scale(model, vec3(4.f));
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, value_ptr(model));

	// Draw the light object (using light's vertex attributes)
	glBindVertexArray(lightVAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

void Light::loadUniforms(Shader* shader)
{
	GLuint lightTypeLocation = shader->getUniformLocation("light.type");
	GLuint lightPositionLocation = shader->getUniformLocation("light.position");
	GLuint lightDirectionLocation = shader->getUniformLocation("light.direction");
	GLuint lightAmbientLocation = shader->getUniformLocation("light.ambientColor");
	GLuint lightDiffuseLocation = shader->getUniformLocation("light.diffuseColor");
	GLuint lightSpecularLocation = shader->getUniformLocation("light.specularColor");

	if (lightTypeLocation != 0xFFFFFFFF ||
		lightPositionLocation != 0xFFFFFFFF ||
		lightDirectionLocation != 0xFFFFFFFF ||
		lightAmbientLocation != 0xFFFFFFFF ||
		lightDiffuseLocation != 0xFFFFFFFF ||
		lightSpecularLocation != 0xFFFFFFFF)
	{
		glUniform1ui(lightTypeLocation, type);
		glUniform3f(lightPositionLocation, position.x, position.y, position.z);
		glUniform3f(lightDirectionLocation, direction.x, direction.y, direction.z);
		glUniform3f(lightAmbientLocation, ambient.r, ambient.g, ambient.b);
		glUniform3f(lightDiffuseLocation, diffuse.r, diffuse.g, diffuse.b);
		glUniform3f(lightSpecularLocation, specular.r, specular.g, specular.b);
	}
}

vec3 Light::getPosition()
{
	return position;
}

vec3 Light::getDirection()
{
	return direction;
}

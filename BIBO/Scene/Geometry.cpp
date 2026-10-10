#include "Geometry.hpp"
#include <algorithm>

using namespace bibo;
using namespace scene;


Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture) : SceneObject(modelMatrix, window)
{
	hasNormalMap = false;
	normalMap = nullptr;
	model = make_unique<Model>(shader, assimpScene, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* filePath, const char* nodeName, Texture* texture)
	: SceneObject(modelMatrix, window)
{
	hasNormalMap = false;
	normalMap = nullptr;
	model = make_unique<Model>(shader, filePath, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* filePath, const char* nodeName, Texture* texture, Texture* normalMapTexture)
	: SceneObject(modelMatrix, window), normalMap(normalMapTexture)
{
	hasNormalMap = true;
	model = make_unique<Model>(shader, filePath, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;

}

Geometry::~Geometry()
{

}

void Geometry::update(float time_delta)
{
	if (!physicsWorld || physicsBody == physics::INVALID_BODY)
		return;
	modelMatrix = physicsWorld->getTransform(physicsBody);
}

void Geometry::draw(Shader* shader)
{
	GLuint modelLocation = shader->getUniformLocation("model");
	if (modelLocation != 0xFFFFFFFF)
	{
		glUniformMatrix4fv(modelLocation, 1, GL_FALSE, value_ptr(modelMatrix));
	}
	if (hasNormalMap)
	{
		normalMap->bind();
	}
	model->draw(shader);
}

bool Geometry::isVisible(const ViewFrustum& frustum)
{
	vec3 center = vec3(modelMatrix * vec4(model->getBoundingSphereCenter(), 1.0f));
	//The model matrix can scale the model (e.g. the terrain)
	float scale = std::max({ length(vec3(modelMatrix[0])), length(vec3(modelMatrix[1])), length(vec3(modelMatrix[2])) });
	return frustum.sphereInFrustum(center, model->getBoundingSphereRadius() * scale) != ViewFrustum::OUTSIDE;
}

Model* Geometry::getModel()
{
	return model.get();
}

void Geometry::setPhysicsBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle body)
{
	this->physicsWorld = physicsWorld;
	this->physicsBody = body;
}

bool Geometry::getHasNormalMap()
{
	return hasNormalMap;
}

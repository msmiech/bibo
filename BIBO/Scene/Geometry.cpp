#include "Geometry.hpp"
#include "Scene.hpp"

using namespace bibo;
using namespace scene;


Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture, Texture* normalMapTexture)
	: SceneObject(modelMatrix, window), model(make_unique<Model>(shader, assimpScene, nodeName, texture)), normalMap(normalMapTexture)
{
	hasNormalMap = normalMapTexture != nullptr;
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::~Geometry()
{

}

void Geometry::update(float time_delta)
{
	if (!collidable || !physicsWorld || physicsBody == physics::INVALID_BODY)
		return;
	modelMatrix = physicsWorld->getTransform(physicsBody);
}

void Geometry::draw(Shader* shader)
{
	if (!drawable)
	{
		return;
	}
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

Model* Geometry::getModel()
{
	return model.get();
}

bool Geometry::isStatic()
{
	return physicsStatic;
}

void Geometry::setStatic(bool physicsStatic)
{
	this->physicsStatic = physicsStatic;
}

bool Geometry::isCollidable()
{
	return collidable;
}

void Geometry::setCollidable(bool collidable)
{
	this->collidable = collidable;
}


void Geometry::setStaticBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle staticBody)
{
	collidable = true;
	physicsStatic = true;
	this->physicsWorld = physicsWorld;
	this->physicsBody = staticBody;
}

void Geometry::setDynamicBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle dynamicBody)
{
	collidable = true;
	physicsStatic = false;
	this->physicsWorld = physicsWorld;
	this->physicsBody = dynamicBody;
}

bool Geometry::getHasNormalMap()
{
	return hasNormalMap;
}

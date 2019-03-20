#include "Geometry.hpp"
#include "Scene.hpp"

using namespace bibo;
using namespace scene;


Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture) : SceneObject(modelMatrix, window)
{
	hasNormalMap = false;
	normalMap = nullptr;
	model = new Model(shader, assimpScene, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* filePath, const char* nodeName, Texture* texture)
	: SceneObject(modelMatrix, window)
{
	hasNormalMap = false;
	normalMap = nullptr;
	model = new Model(shader, filePath, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* filePath, const char* nodeName, Texture* texture, Texture* normalMapTexture)
	: SceneObject(modelMatrix, window), normalMap(normalMapTexture)
{
	hasNormalMap = true;
	model = new Model(shader, filePath, nodeName, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;

}

Geometry::Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* filePath, Texture* texture)
	: SceneObject(modelMatrix, window)
{
	hasNormalMap = false;
	normalMap = nullptr;
	model = new Model(shader, filePath, texture);
	this->modelMatrix = model->getTranformationMat() * modelMatrix;
}

Geometry::~Geometry()
{

}

void Geometry::update(float time_delta)
{
	if (!collidable)
		return;
	if (pxStatic && pxStaticActor)
	{
		modelMatrix = util::convert(PxMat44(pxStaticActor->getGlobalPose()));
	}
	else if (!pxStatic && pxDynamicActor)
	{
		modelMatrix = util::convert(PxMat44(pxDynamicActor->getGlobalPose()));
	}
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
	return model;
}

bool Geometry::isStatic()
{
	return pxStatic;
}

void Geometry::setStatic(bool pxStatic)
{
	this->pxStatic = pxStatic;
}

bool Geometry::isCollidable()
{
	return collidable;
}

void Geometry::setCollidable(bool collidable)
{
	this->collidable = collidable;
}


void Geometry::setStaticActor(PxRigidStatic* staticActor)
{
	collidable = true;
	pxStatic = true;
	this->pxStaticActor = staticActor;
}

void Geometry::setDynamicActor(PxRigidDynamic* dynamicActor)
{
	collidable = true;
	pxStatic = false;
	this->pxDynamicActor = dynamicActor;
}

bool Geometry::getHasNormalMap()
{
	return hasNormalMap;
}

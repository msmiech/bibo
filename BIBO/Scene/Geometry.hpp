#pragma once
#include "SceneObject.hpp"

#ifndef GLM_INCLUDED
#define GLM_INCLUDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.inl>
#include <glm/gtc/type_ptr.hpp>
#endif
#include "../Resources/Model.hpp"
#include <PhysX/PxRigidStatic.h>

using namespace glm;
using namespace std;
using namespace bibo::resources;

namespace bibo
{
	namespace scene
	{
		class Geometry : public SceneObject
		{
			Model* model;
			bool collidable = false;
			bool pxStatic = true;
			physx::PxRigidStatic* pxStaticActor;
			physx::PxRigidDynamic* pxDynamicActor;
		public:
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture, Texture* normalMapTexture);

			virtual ~Geometry();

			virtual void update(float time_delta);
			virtual void draw(Shader* shader);

			Model* getModel();
			bool isStatic();
			void setStatic(bool pxStatic);
			bool isCollidable();
			void setCollidable(bool collidable);
			void setStaticActor(physx::PxRigidStatic* staticActor);
			void setDynamicActor(physx::PxRigidDynamic* dynamicActor);
			bool drawable = true;
			Texture* normalMap;
			bool hasNormalMap;
			bool getHasNormalMap();
			void setTexture(Texture* texture);
			Texture* getTexture();
		};
	}
}

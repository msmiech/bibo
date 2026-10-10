#pragma once
#include "SceneObject.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../Resources/Model.hpp"
#include "../Physics/Physics.hpp"

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
			bool physicsStatic = true;
			physics::PhysicsWorld* physicsWorld = nullptr;
			physics::BodyHandle physicsBody = physics::INVALID_BODY;
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
			void setStatic(bool physicsStatic);
			bool isCollidable();
			void setCollidable(bool collidable);
			/**
			* The geometry follows the position and rotation of the physics body from now on
			*/
			void setStaticBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle staticBody);
			void setDynamicBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle dynamicBody);
			bool drawable = true;
			Texture* normalMap;
			bool hasNormalMap;
			bool getHasNormalMap();
			void setTexture(Texture* texture);
			Texture* getTexture();
		};
	}
}

#pragma once
#include "SceneObject.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "ViewFrustum.hpp"
#include "../Resources/Model.hpp"
#include "../Physics/Physics.hpp"
#include <memory>

using namespace glm;
using namespace std;
using namespace bibo::resources;

namespace bibo
{
	namespace scene
	{
		class Geometry : public SceneObject
		{
			unique_ptr<Model> model;
			physics::PhysicsWorld* physicsWorld = nullptr;
			physics::BodyHandle physicsBody = physics::INVALID_BODY;
		public:
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture, Texture* normalMapTexture);

			virtual ~Geometry();

			virtual void update(float time_delta);
			virtual void draw(Shader* shader);
			/**
			* @return false if the geometry is completely outside of the view frustum and does not need to be drawn
			*/
			virtual bool isVisible(const ViewFrustum& frustum);

			Model* getModel();
			/**
			* The geometry follows the position and rotation of the physics body from now on
			*/
			void setPhysicsBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle body);
			Texture* normalMap;
			bool hasNormalMap;
			bool getHasNormalMap();
		};
	}
}

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
			//From the body to the model: what the pose of the body does not have (e.g. the scale of the model)
			mat4 bodyToModel = mat4(1.0f);
			//The game uses the meshes (e.g. for the physics and the materials), it cannot run without them
			void requireMeshes(const char* nodeName);
		public:
			/**
			* The program exits if the node does not exist or has no meshes.
			* @param assimpScene Loaded once for all geometries, see util::loadSceneFromFile
			* @param normalMapTexture nullptr for geometries without normal map
			*/
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, const char* nodeName, Texture* texture, Texture* normalMapTexture = nullptr);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture);
			Geometry(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const char* file, const char* nodeName, Texture* texture, Texture* normalMapTexture);

			virtual ~Geometry();
			Geometry(const Geometry&) = delete;
			Geometry& operator=(const Geometry&) = delete;

			virtual void update(float time_delta);
			virtual void draw(Shader* shader);
			/**
			* @return false if the geometry is completely outside of the view frustum and does not need to be drawn
			*/
			virtual bool isVisible(const ViewFrustum& frustum);

			Model* getModel();
			/**
			* The geometry follows the position and rotation of the physics body from now on. Its current model matrix
			* relative to the body (e.g. its scale) is kept, so the body may be anywhere in or around the model.
			*/
			void setPhysicsBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle body);
			Texture* normalMap;
			bool hasNormalMap;
			//Layer of the generated materials (world::MaterialTextures) instead of the texture, -1: the texture
			int materialLayer = -1;
			//How often the generated material repeats per unit of the model
			float materialScale = 1.0f;
			bool getHasNormalMap();
		protected:
			/**
			* For geometries that create and draw their meshes themselves (getModel returns nullptr, draw, update and isVisible must be overridden)
			*/
			Geometry(mat4 modelMatrix, GLFWwindow& window);
		};
	}
}

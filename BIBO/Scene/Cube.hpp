#pragma once

#include "SceneObject.hpp"
#include "../Resources/Texture.hpp"
#include "../Physics/Physics.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define CUBE_VERTEX_COUNT 24
#define CUBE_INDEX_COUNT 36


namespace bibo
{
	namespace scene
	{
		class Cube : public SceneObject
		{
		public:
			Cube(mat4 modelMatrix, Shader* _shader, GLFWwindow& window, Texture* texture);
			virtual ~Cube();

			void update(float time_delta) override;
			void draw(Shader* shader) override;

			/**
			* The cube follows the position and rotation of the physics body from now on
			*/
			void setPhysicsBody(physics::PhysicsWorld* physicsWorld, physics::BodyHandle body);
		private:
			physics::PhysicsWorld* physicsWorld = nullptr;
			physics::BodyHandle physicsBody = physics::INVALID_BODY;
			GLuint vao;
			GLuint positionBuffer, normalBuffer, uvBuffer,  indexBuffer;
			Shader* shader;
			Texture* texture;

			static const float positions[CUBE_VERTEX_COUNT * 3];
			static const unsigned int indices[CUBE_INDEX_COUNT];
			static const float normals[CUBE_VERTEX_COUNT * 3];
			static const float uvs[CUBE_VERTEX_COUNT * 2];

		};
	}
}

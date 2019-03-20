#pragma once

#ifndef GLM_INCLUDED
#define GLM_INCLUDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.inl>
#include <glm/gtc/type_ptr.hpp>
#endif
#ifndef GLEW_INCLUDED
#define GLEW_INCLUDED
#include <GL\glew.h>
#include <gl\wglew.h>
#include <GL\GL.h>
#endif
#ifndef GLFW_INCLUDED
#define GLFW_INCLUDED
#include <GLFW/glfw3.h>
#endif
#include "../Resources/Shader.hpp"

using namespace glm;

namespace bibo
{
	namespace scene
	{
		class SceneObject {
		public:
			SceneObject(GLFWwindow& window);
			SceneObject(mat4 modelMatrix, GLFWwindow& window);

			virtual ~SceneObject();

			virtual void update(float time_delta) = 0;
			virtual void draw(Shader* shader) = 0;

			virtual vec3 getPosition();
			virtual void setPosition(vec3 position);
			virtual vec3 getDirection();
			mat4 modelMatrix;
		protected:
			GLFWwindow* window;
		};
	}
}
#define SCENE_OBJECT
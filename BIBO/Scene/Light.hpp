#pragma once
#include "SceneObject.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include "Camera.hpp"


using namespace std;
using namespace glm;

namespace bibo
{
	namespace scene
	{
		class Light : SceneObject
		{
		public:
			Light(GLFWwindow& window, Shader* shader, aiLight* light);
			~Light();
			void update(float time_delta) override;
			void draw(Shader* shader) override;
			void loadUniforms(Shader* shader);
			vec3 getPosition() override;
			vec3 getDirection();
		private:
			void convertLight(aiLight* light);
			GLuint lightVAO, lightVBO;
			uint type;
			vec3 position;
			vec3 direction;
			vec3 ambient;
			vec3 diffuse;
			vec3 specular;
		};
	}
}

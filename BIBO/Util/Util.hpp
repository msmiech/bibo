#pragma once
#include <string>
#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include "../Scene/Light.hpp"
#include "../Resources/Shader.hpp"
#include <vector>
#include <memory>


using namespace std;
using namespace bibo::scene;

namespace bibo
{
	const bool DEBUG = true;

	namespace util
	{
		/**
		* @return The loaded scene, which is owned by the caller (delete it when no longer needed) or nullptr
		*/
		const aiScene* loadSceneFromFile(const char* file);
		vector<unique_ptr<Light>> retrieveLightFromScene(GLFWwindow& window, Shader* shader, const char* file);
		mat4 ConvertMatrix(aiMatrix4x4 in);
	}
}

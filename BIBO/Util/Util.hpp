#pragma once
#include <string>
#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/cimport.h>
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
		struct SceneDeleter
		{
			void operator()(const aiScene* scene) const { aiReleaseImport(scene); }
		};
		using ScenePtr = unique_ptr<const aiScene, SceneDeleter>;

		/**
		* Loads a model file once, so that several models can be created from its nodes
		* @return The loaded scene or nullptr
		*/
		ScenePtr loadSceneFromFile(const char* file);
		vector<unique_ptr<Light>> retrieveLightFromScene(GLFWwindow& window, Shader* shader, const aiScene* scene);
		mat4 ConvertMatrix(aiMatrix4x4 in);
	}
}

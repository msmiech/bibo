#include "Util.hpp"
#include <cstdio>
#include <assimp/postprocess.h>

using namespace bibo;
using namespace util;
using namespace scene;
using namespace std;


ScenePtr util::loadSceneFromFile(const char* file)
{
	Assimp::Importer importer;
	auto scene = importer.ReadFile(file, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
	if (!scene) {
		printf("Unable to load model %s: %s\n", file, importer.GetErrorString());
		return nullptr;
	}
	//The importer owns the scene and would delete it at the end of this function
	return ScenePtr(importer.GetOrphanedScene());
}

vector<unique_ptr<Light>> util::retrieveLightFromScene(GLFWwindow& window, Shader* shader, const aiScene* scene)
{
	vector<unique_ptr<Light>> lights;
	if (!scene->HasLights())
	{
		printf("Scene does not contain lights\n");
	}
	for (unsigned int i = 0; i < scene->mNumLights; ++i) {
		lights.push_back(make_unique<Light>(window, shader, scene->mLights[i]));
	}

	return lights;
}

mat4 util::ConvertMatrix(aiMatrix4x4 in) {
	return mat4(
		in.a1, in.b1, in.c1, in.d1,
		in.a2, in.b2, in.c2, in.d2,
		in.a3, in.b3, in.c3, in.d3,
		in.a4, in.b4, in.c4, in.d4);
}

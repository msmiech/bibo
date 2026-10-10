#include "Util.hpp"
#include <cstdio>

using namespace bibo;
using namespace util;
using namespace scene;
using namespace std;


vector<unique_ptr<Light>> util::retrieveLightFromScene(GLFWwindow& window, Shader* shader, const char* file)
{
	vector<unique_ptr<Light>> lights;
	Assimp::Importer importer;
	auto scene = importer.ReadFile(file, 0);
	if (!scene) {
		printf("Unable to laod mesh: %s\n", importer.GetErrorString());
		return lights;
	}
	if (!scene->HasLights())
	{
		printf("Scene does not contain lights: %s\n", importer.GetErrorString());
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

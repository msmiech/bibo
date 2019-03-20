#include "Util.hpp"
#include <fstream>

using namespace bibo;
using namespace util;
using namespace scene;
using namespace std;
using namespace resources;
using namespace physx;


unsigned char* util::LoadTexture(const wstring &path, unsigned int &width, unsigned int &height, unsigned int &format, unsigned int &type)
{
	if (ilGetInteger(IL_VERSION_NUM) < IL_VERSION ||
		iluGetInteger(ILU_VERSION_NUM) < ILU_VERSION)
	{
		printf("DevIL version mismatch.\n");
	}
	ilInit();
	iluInit();
	auto devilError = ilGetError();
	//TODO: DevIL Error handling

	auto path_c_str = path.c_str();
	auto result = ilLoadImage(path.c_str());
	if (!result) {
		return nullptr;
	}
	width = ilGetInteger(IL_IMAGE_WIDTH);
	height = ilGetInteger(IL_IMAGE_HEIGHT);
	format = ilGetInteger(IL_IMAGE_FORMAT);
	type = ilGetInteger(IL_IMAGE_TYPE);
	return ilGetData();
}

const aiScene* util::loadSceneFromFile(const char* file)
{
	Assimp::Importer importer;

	ifstream fin(file);
	if (!fin.fail()) {
		fin.close();
	}
	else
	{
		printf("Couldn't open file: %s\n", file);
		printf("%s\n", importer.GetErrorString());
	}

	auto scene = importer.ReadFile(file, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);


	if (!scene) {
		printf("Unable to load model: %s\n", importer.GetErrorString());
		return nullptr;
	}
	return scene;
}

vector<unique_ptr<Light>> util::retrieveLightFromScene(GLFWwindow& window, Shader* shader, const char* file)
{
	vector<unique_ptr<Light>> lights;
	Assimp::Importer importer;
	auto scene = importer.ReadFile(file, NULL);
	if (!scene) {
		printf("Unable to laod mesh: %s\n", importer.GetErrorString());
	}
	if (!scene->HasLights())
	{
		printf("Scene does not contain lights: %s\n", importer.GetErrorString());
	}
	for (auto i = 0; i < scene->mNumLights; ++i) {
		switch (scene->mLights[i]->mType)
		{
		case aiLightSource_POINT:
			break;
		case aiLightSource_DIRECTIONAL:
			break;
		case aiLightSource_UNDEFINED: break;
		case aiLightSource_SPOT: break;
		case _aiLightSource_Force32Bit: break;
		default: break;
		}
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

PxVec3 util::convert(vec3 glmVec3)
{
	return PxVec3(glmVec3.x, glmVec3.y, glmVec3.z);
}

vec3 util::convert(PxVec3 pxVec3)
{
	return vec3(pxVec3.x, pxVec3.y, pxVec3.z);
}


vec3 util::convert(PxExtendedVec3 pxVec3)
{
	return vec3(pxVec3.x, pxVec3.y, pxVec3.z);
}



PxTriangleMesh* util::convert(Mesh* mesh, PxPhysics* mPhysics, PxCooking* mCooking)
{
	PxTriangleMeshDesc t;
	t.points.count = mesh->numVertices;
	t.points.data = mesh->vertices;
	t.points.stride = 3 * sizeof(float);
	t.triangles.count = mesh->numFaces;
	t.triangles.data = mesh->indices;
	t.triangles.stride = 3 * sizeof(int);
	//t.flags = PxMeshFlag::eFLIPNORMALS;

	/*
	physx::PxTriangleMesh* createTriangleMesh32(physx::PxPhysics& physics, physx::PxCooking& cooking, physx::PxTriangleMeshDesc* meshDesc, bool insert = false);
	physx::PxTriangleMesh* createTriangleMesh32(physx::PxPhysics& physics, physx::PxCooking& cooking, const PxVec3* verts, PxU32 vertCount, const PxU32* indices32, PxU32 triCount, bool insert = false);
	physx::PxConvexMesh* createConvexMesh(physx::PxPhysics& physics, physx::PxCooking& cooking, const PxVec3* verts, PxU32 vertCount, PxConvexFlags flags);
	physx::PxConvexMesh* createConvexMeshSafe(physx::PxPhysics& physics, physx::PxCooking& cooking, const PxVec3* verts, PxU32 vertCount, PxConvexFlags flags, PxU32 vLimit = 256);
	*/
	return PxToolkit::createTriangleMesh32(*mPhysics, *mCooking, &t);
}

PxTriangleMesh* util::convert(aiMesh* mesh, PxPhysics* mPhysics, PxCooking* mCooking)
{
	PxTriangleMeshDesc t;
	t.points.count = mesh->mNumVertices;
	t.points.data = mesh->mVertices;
	t.points.stride = sizeof(aiVector3D);
	t.triangles.count = mesh->mNumFaces;
	PxU32 *tr = new PxU32[mesh->mNumFaces * 3];
	PxU32 k = 0;
	for (PxU32 i = 0; i<mesh->mNumFaces; i++)
		for (int j = 0; j<3; j++)
			tr[k++] = mesh->mFaces[i].mIndices[j];
	t.triangles.data = tr;
	t.triangles.stride = sizeof(PxU32) * 3;

	return PxToolkit::createTriangleMesh32(*mPhysics, *mCooking, &t);

}

mat4 util::convert(PxMat44 pxMat)
{
	mat4 result(0.0f);
	result[0].x = pxMat.column0.x;
	result[0].y = pxMat.column0.y;
	result[0].z = pxMat.column0.z;
	result[0].w = pxMat.column0.w;
	result[1].x = pxMat.column1.x;
	result[1].y = pxMat.column1.y;
	result[1].z = pxMat.column1.z;
	result[1].w = pxMat.column1.w;
	result[2].x = pxMat.column2.x;
	result[2].y = pxMat.column2.y;
	result[2].z = pxMat.column2.z;
	result[2].w = pxMat.column2.w;
	result[3].x = pxMat.column3.x;
	result[3].y = pxMat.column3.y;
	result[3].z = pxMat.column3.z;
	result[3].w = pxMat.column3.w;
	return result;
}

PxMat44 util::convert(mat4 glmMat)
{
	PxMat44 result(0.0f);
	result[0].x = glmMat[0].x;
	result[0].y = glmMat[0].y;
	result[0].z = glmMat[0].z;
	result[0].w = glmMat[0].w;
	result[1].x = glmMat[1].x;
	result[1].y = glmMat[1].y;
	result[1].z = glmMat[1].z;
	result[1].w = glmMat[1].w;
	result[2].x = glmMat[2].x;
	result[2].y = glmMat[2].y;
	result[2].z = glmMat[2].z;
	result[2].w = glmMat[2].w;
	result[3].x = glmMat[3].x;
	result[3].y = glmMat[3].y;
	result[3].z = glmMat[3].z;
	result[3].w = glmMat[3].w;
	return result;
}
#pragma once
#include <string>
#include <iostream>

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


#ifndef PX_INCLUDED
#define PX_INCLUDED
#include "PhysX/PxPhysicsAPI.h"
#include "PxToolkit/PxTkStream.h"
#endif

#include <IL\il.h>
#include <IL\ilu.h>
#include <IL\ilut.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include "../Scene/Light.hpp"
#include "../Resources/Shader.hpp"
#include "../Resources/Mesh.hpp"
#include <vector>
#include <memory>


using namespace std;
using namespace bibo::scene;
using namespace physx;

namespace bibo
{
	const bool DEBUG = true;

	namespace util
	{
		unsigned char* LoadTexture(const wstring &path, unsigned int &width, unsigned int &height, unsigned int &format, unsigned int &type);
		const aiScene* loadSceneFromFile(const char* file);
		vector<unique_ptr<Light>> retrieveLightFromScene(GLFWwindow& window, Shader* shader, const char* file);
		mat4 ConvertMatrix(aiMatrix4x4 in);
		PxTriangleMesh* convert(resources::Mesh* mesh, PxPhysics* mPhysics, PxCooking* mCooking);
		PxTriangleMesh* convert(aiMesh* mesh, PxPhysics* mPhysics, PxCooking* mCooking);
		PxVec3 convert(vec3 glmVec3);
		vec3 convert(PxVec3 pxVec3);
		vec3 convert(PxExtendedVec3 pxVec3);
		mat4 convert(PxMat44 pxMat);
		PxMat44 convert(mat4 glmMat);

		/**
		Code from GLM library! Included here to avoid cyclic references
		#include "glm/gtx/rotate_vector.hpp"
		**/
		template <typename T, precision P>
		GLM_FUNC_QUALIFIER tvec3<T, P> rotateY
			(
			tvec3<T, P> const & v,
			T const & angle
			)
		{
			tvec3<T, P> Result = v;
			T const Cos(cos(angle));
			T const Sin(sin(angle));

			Result.x = v.x * Cos + v.z * Sin;
			Result.z = -v.x * Sin + v.z * Cos;
			return Result;
		}
	}
}

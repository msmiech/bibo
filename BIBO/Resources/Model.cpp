#include "Model.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include "../Util/Util.hpp"

using namespace bibo;
using namespace resources;
using namespace util;


/**
* Model constructor to load model/scene information from a file
*
* @path to file which should be loaded.
* @nodeName Name of the node that should be loaded
*/
Model::Model(Shader* shader, const char* path, const char* nodeName, Texture* texture)
{
	Assimp::Importer importer;

	ifstream fin(path);
	if (!fin.fail()) {
		fin.close();
	}
	else
	{
		printf("Couldn't open file: %s\n", path);
		printf("%s\n", importer.GetErrorString());
	}

	auto scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);


	if (!scene) {
		printf("Unable to load model: %s\n", importer.GetErrorString());
		return;
	}

	init(shader, scene, nodeName, texture);

}

Model::Model(Shader* shader, const char* filename, Texture* texture) : Model(shader, filename, nullptr, texture)
{
	//Just calling the other constructor
}

Model::Model(Shader* shader, const aiScene* scene, const char* nodeName, Texture* texture)
{
	init(shader, scene, nodeName, texture);
}

void Model::init(Shader* shader, const aiScene* scene, const char* nodeName, Texture* texture)
{
	aiMatrix4x4 vertexRotationAroundUpAxis;
	aiMatrix4x4::RotationZ(radians(-180.0f), vertexRotationAroundUpAxis);


	auto node = scene->mRootNode;
	if (!node)
	{
		printf("There is no root node!");
		return;
	}
	if (nodeName)
	{
		node = node->FindNode(nodeName);
		if (!node)
		{
			printf("Could not find/load node: %s");
			return;
		}
	}


	auto transformation = node->mTransformation;
	transformNode(&transformation, node);

	tranformationMat = ConvertMatrix(transformation);

	createMeshes(shader, scene, node, texture, vertexRotationAroundUpAxis);
}

/**
*	Deletes loaded model meshes.
**/
Model::~Model()
{
	for (unsigned int i = 0; i < meshes.size(); ++i) {
		delete meshes[i];
	}
	meshes.clear();
}

/**
*	Creates model meshes
**/
void Model::createMeshes(Shader* shader, const aiScene* scene, aiNode* node, Texture* texture, aiMatrix4x4 vertexRotationAroundUpAxis)
{
	if (node->mNumChildren == 0)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
		{
			aiMesh* assimpMesh = scene->mMeshes[node->mMeshes[i]];
			Material meshMaterial;
			if (scene->HasMaterials() && assimpMesh)
			{
				aiMaterial* assimpMaterial = scene->mMaterials[assimpMesh->mMaterialIndex];
				if (assimpMaterial)
				{
					aiColor4D ambient;
					if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_AMBIENT, &ambient) == AI_SUCCESS)
					{
						meshMaterial.ambient = vec3(ambient.r, ambient.g, ambient.b);
					}

					aiColor4D diffuse;
					if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == AI_SUCCESS)
					{
						meshMaterial.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
					}


					aiColor4D specular;
					if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_SPECULAR, &specular) == AI_SUCCESS)
					{
						meshMaterial.specular = vec3(specular.r, specular.g, specular.b);

					}

					//aiColor4D emission;
					//if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_EMISSIVE, &emission) == AI_SUCCESS)
					//{
					// not relevant for us at the moment
					//}

					float shininess = 32.0f;
					//unsigned int max;
					//aiGetMaterialFloatArray(assimpMaterial, AI_MATKEY_SHININESS, &shininess, &max); //TODO ?
					meshMaterial.shininess = shininess;
				}
			}
			meshes.push_back(new Mesh(shader, assimpMesh, meshMaterial, texture, vertexRotationAroundUpAxis));
		}
	}
	else
	{
		for (unsigned int i = 0; i < node->mNumChildren; ++i){
			aiNode* currentNode = node->mChildren[i];
			for (unsigned int j = 0; j < currentNode->mNumMeshes; ++j){
				Material meshMaterial;
				aiMesh* assimpMesh = scene->mMeshes[currentNode->mMeshes[j]];
				if (assimpMesh)
				{
					aiMaterial* assimpMaterial = scene->mMaterials[assimpMesh->mMaterialIndex];
					if (assimpMaterial)
					{
						aiColor4D ambient;
						if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_AMBIENT, &ambient) == AI_SUCCESS)
						{
							meshMaterial.ambient = vec3(ambient.r, ambient.g, ambient.b);
						}

						aiColor4D diffuse;
						if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == AI_SUCCESS)
						{
							meshMaterial.diffuse = vec3(diffuse.r, diffuse.g, diffuse.b);
						}


						aiColor4D specular;
						if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_SPECULAR, &specular) == AI_SUCCESS)
						{
							meshMaterial.specular = vec3(specular.r, specular.g, specular.b);

						}

						//aiColor4D emission;
						//if (aiGetMaterialColor(assimpMaterial, AI_MATKEY_COLOR_EMISSIVE, &emission) == AI_SUCCESS)
						//{
						// not relevant for us at the moment
						//}

						float shininess = 32.0f;
						//unsigned int max;
						//aiGetMaterialFloatArray(assimpMaterial, AI_MATKEY_SHININESS, &shininess, &max); //TODO ?
						meshMaterial.shininess = shininess;
					}
				}
				meshes.push_back(new Mesh(shader, assimpMesh, meshMaterial, texture, vertexRotationAroundUpAxis));
			}
		}
	}
}


/**
*	Draws loaded model meshes
**/
void Model::draw(Shader* shader) {
	for (unsigned int i = 0; i < meshes.size(); ++i) {
		meshes[i]->draw(shader);
	}
}


//This Method was taken from: http://gamedev.stackexchange.com/questions/87239/how-to-properly-transform-vertices-for-a-model-loaded-using-assimp
void Model::transformNode(aiMatrix4x4 *result, aiNode* rootNode) {

	if (rootNode->mParent) {
		transformNode(result, rootNode->mParent);
		aiMultiplyMatrix4(result, &rootNode->mTransformation);
	}
	else {
		*result = rootNode->mTransformation;
	}
}


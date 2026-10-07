#pragma once
#include <vector>
#include <map>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/cimport.h>
#include "Shader.hpp"
#include "Texture.hpp"
#include "Mesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


using namespace std;
using namespace glm;

namespace bibo
{
	namespace resources
	{
		class Mesh;

		/**
		* Model and Mesh class is based on
		*
		* http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/
		* and
		* http://ogldev.atspace.co.uk/www/tutorial22/tutorial22.html
		*
		* Adapted for our project.
		*/
		class Model
		{
			mat4 tranformationMat;
			void createMeshes(Shader* shader, const aiScene* scene, aiNode* node, Texture* texture, aiMatrix4x4 vertexRotationAroundUpAxis);
			void init(Shader* shader, const aiScene* scene, const char* nodeName, Texture* texture);
		public:
			Model(Shader* shader, const char *filename, const char * nodeName, Texture* tex);
			Model(Shader* shader, const aiScene* scene, const char * nodeName, Texture* tex);
			Model(Shader* shader, const char *filename, Texture* tex);
			~Model();

			void transformNode(aiMatrix4x4 *result, aiNode* rootNode);
			mat4 getTranformationMat(){ return tranformationMat; }
			void draw(Shader* shader);

			//Fields
			vector<Mesh*> meshes;
		};
	}
}

#pragma once
#include "Material.hpp"
#include "Texture.hpp"
#include "Model.hpp"
#ifndef GLM_INCLUDED
#define GLM_INCLUDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.inl>
#include <glm/gtc/type_ptr.hpp>
#include <glm/detail/type_vec3.hpp>
#include <glm/detail/type_vec2.hpp>
#endif

using namespace std;
using namespace glm;

namespace bibo
{
	namespace resources
	{
		/**
		* Model and Mesh class is based on
		*
		* http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/
		* and
		* http://ogldev.atspace.co.uk/www/tutorial22/tutorial22.html
		*
		* Adapted for our project.
		*/
		class Mesh {
			const enum BUFFERS {
				POS_BUFFER = 0,
				NORMAL_BUFFER = 1,
				UV_BUFFER = 2,
				INDEX_BUFFER = 3,
				TANGENT_BUFFER = 4,
				BITANGENT_BUFFER = 5
			};

			const unsigned int MAX_FACES = 16777216;

			GLuint vao;
			GLuint vbo[4];

			Shader* shader;

			Material material;
			Texture* texture;

		public:
			Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture, aiMatrix4x4 transformation);
			~Mesh();
			void draw(Shader* shader);

			aiMesh* source;
			unsigned int numVertices;
			unsigned int numFaces;
			float* vertices;
			float* normals;
			float* texCoords;
			float* tangents;
			float* bitangents;
			unsigned int* indices;

			void setTexture(Texture* texture);
			Texture* getTexture();

			void setMaterial(Material material);
			Material getMaterial();
		};
	}
}
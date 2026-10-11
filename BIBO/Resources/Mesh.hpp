#pragma once
#include "Material.hpp"
#include "Texture.hpp"
#include "Shader.hpp"
#include <assimp/mesh.h>
#include <assimp/matrix4x4.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
			enum BUFFERS {
				POS_BUFFER = 0,
				NORMAL_BUFFER = 1,
				UV_BUFFER = 2,
				INDEX_BUFFER = 3,
				TANGENT_BUFFER = 4,
				BITANGENT_BUFFER = 5,
				BUFFER_COUNT = 6
			};

			const unsigned int MAX_FACES = 16777216;

			GLuint vao;
			GLuint vbo[BUFFER_COUNT];

			Shader* shader;

			Material material;
			Texture* texture;

		public:
			Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture, aiMatrix4x4 transformation);
			~Mesh();
			Mesh(const Mesh&) = delete;
			Mesh& operator=(const Mesh&) = delete;
			void draw(Shader* shader);

			unsigned int numVertices;
			unsigned int numFaces;
			float* vertices = nullptr;
			float* normals = nullptr;
			float* texCoords = nullptr;
			float* tangents = nullptr;
			float* bitangents = nullptr;
			unsigned int* indices = nullptr;

			void setTexture(Texture* texture);
			Texture* getTexture();

			void setMaterial(Material material);
			Material getMaterial();
		};
	}
}

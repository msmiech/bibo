#include "Mesh.hpp"

using namespace bibo;
using namespace resources;

Mesh::Mesh(Shader* shader, aiMesh* mesh, Material material, Texture* texture, aiMatrix4x4 transformation) : shader(shader), material(material), texture(texture), source(mesh)
{
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	//glGenBuffers(4, vbo);
	vbo[POS_BUFFER] = NULL;
	vbo[UV_BUFFER] = NULL;
	vbo[NORMAL_BUFFER] = NULL;
	vbo[INDEX_BUFFER] = NULL;
	vbo[TANGENT_BUFFER] = NULL;
	vbo[BITANGENT_BUFFER] = NULL;

	numFaces = mesh->mNumFaces;
	numVertices = mesh->mNumVertices;

	if (mesh->HasPositions()) {
		vertices = new float[mesh->mNumVertices * 3];
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {

			auto pos = mesh->mVertices[i];

			//aiTransformVecByMatrix4(&pos, &transformation);

			vertices[i * 3] = pos.x;
			vertices[i * 3 + 1] = pos.y;
			vertices[i * 3 + 2] = pos.z;
		}

		glGenBuffers(1, &vbo[POS_BUFFER]);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[POS_BUFFER]);
		glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat), vertices, GL_STATIC_DRAW);

		auto positionIndex = shader->getAttribLocation("position");
		glVertexAttribPointer(positionIndex, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(positionIndex);

		//delete[] vertices;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	if (mesh->HasNormals()) {
		normals = new float[mesh->mNumVertices * 3];
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {

			auto normal = mesh->mNormals[i];

			//auto inverseTransformation = transformation.Inverse();
			//uto inverseTransposed = inverseTransformation.Transpose();
			//aiTransformVecByMatrix4(&normal, &inverseTransposed);

			normals[i * 3] = normal.x;
			normals[i * 3 + 1] = normal.y;
			normals[i * 3 + 2] = normal.z;
		}

		auto normalIndex = shader->getAttribLocation("normal");
		if (normalIndex != 0xFFFFFFFF){
			glGenBuffers(1, &vbo[NORMAL_BUFFER]);
			glBindBuffer(GL_ARRAY_BUFFER, vbo[NORMAL_BUFFER]);
			glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat), normals, GL_STATIC_DRAW);

			glVertexAttribPointer(normalIndex, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
			glEnableVertexAttribArray(normalIndex);
		}
		//delete[] normals;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	if (mesh->HasTextureCoords(0)) {
		texCoords = new float[mesh->mNumVertices * 2];
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
			texCoords[i * 2] = mesh->mTextureCoords[0][i].x;
			texCoords[i * 2 + 1] = mesh->mTextureCoords[0][i].y;
		}

		glGenBuffers(1, &vbo[UV_BUFFER]);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[UV_BUFFER]);
		glBufferData(GL_ARRAY_BUFFER, 2 * mesh->mNumVertices * sizeof(GLfloat), texCoords, GL_STATIC_DRAW);

		auto uvIndex = shader->getAttribLocation("uv");
		glVertexAttribPointer(uvIndex, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
		glEnableVertexAttribArray(uvIndex);

		//delete[] texCoords;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	if (mesh->HasFaces()) {
		indices = new unsigned int[mesh->mNumFaces * 3];
		for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
			indices[i * 3] = mesh->mFaces[i].mIndices[0];
			indices[i * 3 + 1] = mesh->mFaces[i].mIndices[1];
			indices[i * 3 + 2] = mesh->mFaces[i].mIndices[2];
		}

		glGenBuffers(1, &vbo[INDEX_BUFFER]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[INDEX_BUFFER]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, 3 * mesh->mNumFaces * sizeof(GLuint), indices, GL_STATIC_DRAW);

		//auto faceIndex = mainShader->getAttribLocation("face");
		//glVertexAttribPointer(faceIndex, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		//glEnableVertexAttribArray(faceIndex);

		//delete[] indices;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	// Normal Mapping preparations TODO get this working
	if (mesh->HasPositions() && mesh->HasTextureCoords(0)){
		tangents = new float[mesh->mNumVertices * 3];
		bitangents = new float[mesh->mNumVertices * 3];
		// calculate tangent/bitangent vectors for normal mapping
		if (mesh->HasTangentsAndBitangents())
		{
			for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
			{
				auto tangent = mesh->mTangents[i];
				auto biTangent = mesh->mBitangents[i];

				tangents[i * 3] = tangent.x;
				tangents[i * 3 + 1] = tangent.y;
				tangents[i * 3 + 2] = tangent.z;

				bitangents[i * 3] = biTangent.x;
				bitangents[i * 3 + 1] = biTangent.y;
				bitangents[i * 3 + 2] = biTangent.z;

			}
		}
		else
		{
			aiVector3D v0;
			aiVector3D v1;
			aiVector3D v2;

			aiVector3D Edge1;
			aiVector3D Edge2;

			aiVector3D uv0;
			aiVector3D uv1;
			aiVector3D uv2;

			aiVector3D Tangent;
			aiVector3D Bitangent;

			for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
			{
				v0 = mesh->mVertices[i];
				v1 = mesh->mVertices[i + 1];
				v2 = mesh->mVertices[i + 2];

				Edge1 = v1 - v0;
				Edge2 = v2 - v0;

				uv0 = mesh->mTextureCoords[0][i];
				uv1 = mesh->mTextureCoords[0][i + 1];
				uv2 = mesh->mTextureCoords[0][i + 2];

				float DeltaU1 = uv1.x - uv0.x;
				float DeltaV1 = uv1.y - uv0.y;
				float DeltaU2 = uv2.x - uv0.x;
				float DeltaV2 = uv2.y - uv0.y;

				float f = 1.0f / (DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1);


				Tangent.x = f * (DeltaV2 * Edge1.x - DeltaV1 * Edge2.x);
				Tangent.y = f * (DeltaV2 * Edge1.y - DeltaV1 * Edge2.y);
				Tangent.z = f * (DeltaV2 * Edge1.z - DeltaV1 * Edge2.z);


				Bitangent.x = f * (-DeltaU2 * Edge1.x - DeltaU1 * Edge2.x);
				Bitangent.y = f * (-DeltaU2 * Edge1.y - DeltaU1 * Edge2.y);
				Bitangent.z = f * (-DeltaU2 * Edge1.z - DeltaU1 * Edge2.z);

				tangents[i * 3] = Tangent.x;
				tangents[i * 3 + 1] = Tangent.y;
				tangents[i * 3 + 2] = Tangent.z;

				bitangents[i * 3] = Bitangent.x;
				bitangents[i * 3 + 1] = Bitangent.y;
				bitangents[i * 3 + 2] = Bitangent.z;

			}
		}

		auto tangentLocation = shader->getAttribLocation("tangentIN");
		if (tangentLocation != 0xFFFFFFFF){
			glGenBuffers(1, &vbo[TANGENT_BUFFER]);
			glBindBuffer(GL_ARRAY_BUFFER, vbo[TANGENT_BUFFER]);
			glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat), tangents, GL_STATIC_DRAW);

			glVertexAttribPointer(tangentLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
			glEnableVertexAttribArray(tangentLocation);

			//delete[] tangents;
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		auto bitangentLocation = shader->getAttribLocation("bitangentIN");
		if (bitangentLocation != 0xFFFFFFFF){
			glGenBuffers(1, &vbo[BITANGENT_BUFFER]);
			glBindBuffer(GL_ARRAY_BUFFER, vbo[BITANGENT_BUFFER]);
			glBufferData(GL_ARRAY_BUFFER, 3 * mesh->mNumVertices * sizeof(GLfloat), bitangents, GL_STATIC_DRAW);


			glVertexAttribPointer(bitangentLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
			glEnableVertexAttribArray(bitangentLocation);
		}
		//delete[] bitangents;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	glBindVertexArray(0);
}


/**
* Frees up graphics memory (deletes buffers)
*/
Mesh::~Mesh() {
	if (vbo[POS_BUFFER]) {
		glDeleteBuffers(1, &vbo[POS_BUFFER]);
	}
	if (vbo[NORMAL_BUFFER]) {
		glDeleteBuffers(1, &vbo[NORMAL_BUFFER]);
	}
	if (vbo[UV_BUFFER]) {
		glDeleteBuffers(1, &vbo[UV_BUFFER]);
	}
	if (vbo[INDEX_BUFFER]) {
		glDeleteBuffers(1, &vbo[INDEX_BUFFER]);
	}
	glDeleteVertexArrays(1, &vao);
}

/**
*	Draws model content (which is in vertex array in graphics memory)
**/
void Mesh::draw(Shader* shader) {
	GLuint materialAmbientLocation = shader->getUniformLocation("material.ambient");
	GLuint materialDiffuseLocation = shader->getUniformLocation("material.diffuse");
	GLuint materialSpecularLocation = shader->getUniformLocation("material.specular");
	GLuint materialShininessLocation = shader->getUniformLocation("material.shininess");

	if (materialAmbientLocation != 0xFFFFFFFF &&
		materialDiffuseLocation != 0xFFFFFFFF &&
		materialSpecularLocation != 0xFFFFFFFF &&
		materialShininessLocation != 0xFFFFFFFF)
	{
		glUniform3f(materialAmbientLocation, 0.01f, 0.01f, 0.01f);
		glUniform3f(materialDiffuseLocation, material.diffuse.r, material.diffuse.g, material.diffuse.b);
		glUniform3f(materialSpecularLocation, material.specular.r, material.specular.g, material.specular.b);
		glUniform1f(materialShininessLocation, material.shininess);
	}

	GLuint textureLocation = shader->getUniformLocation("colorTextureSampler");

	if (textureLocation != 0xFFFFFFFF && texture)
	{
		texture->bind();
		glUniform1i(textureLocation, 0);
	}

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, numVertices, GL_UNSIGNED_INT, nullptr); //previously numberOfElements = numFaces * 3
	glBindVertexArray(0);
	if (texture)
	{
		texture->unbind();
	}
}

void Mesh::setTexture(Texture* texture)
{
	this->texture = texture;
}

Texture* Mesh::getTexture()
{
	return texture;
}

void Mesh::setMaterial(Material material)
{
	this->material = material;
}

Material Mesh::getMaterial()
{
	return material;
}
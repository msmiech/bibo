#include "BearRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include "Light.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	//Up to this distance from the camera all strands are drawn
	const float FULL_DETAIL_DISTANCE = 14.0f;
	const float MIN_DETAIL = 0.12f;
	const std::uint32_t STRAND_SEED = 1234;

	void setMatrix(Shader* shader, const char* name, const glm::mat4& matrix)
	{
		glUniformMatrix4fv(shader->getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
	}
}

BearRenderer::BearRenderer(const resources::SkinnedModel& model, std::size_t strandCount)
	: skinShader(std::make_unique<Shader>("Shader/bear_skin.vert", "Shader/bear_skin.frag")),
	furShader(std::make_unique<Shader>("Shader/bear_fur.vert", "Shader/bear_fur.frag")),
	vertexCount(model.positions.size())
{
	staging.resize(vertexCount * 4);

	glGenVertexArrays(1, &vertexArray);
	glBindVertexArray(vertexArray);

	glGenBuffers(1, &skinBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, skinBuffer);
	glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(glm::vec4), nullptr, GL_STREAM_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), nullptr); //position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), reinterpret_cast<void*>(sizeof(glm::vec4))); //normal
	glEnableVertexAttribArray(1);

	glGenBuffers(1, &colorBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
	glBufferData(GL_ARRAY_BUFFER, model.colors.size() * sizeof(glm::vec4), model.colors.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(2);

	glGenBuffers(1, &surfaceBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, surfaceBuffer);
	glBufferData(GL_ARRAY_BUFFER, model.surfaces.size() * sizeof(float), model.surfaces.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(3);

	glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(unsigned int), model.indices.data(), GL_STATIC_DRAW);
	indexCount = static_cast<GLsizei>(model.indices.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	std::vector<fur::Strand> strands = fur::generateStrands(model.positions, model.colors, model.triangles(resources::SkinnedModel::FUR), strandCount, STRAND_SEED);
	this->strandCount = strands.size();
	glGenBuffers(1, &strandBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, strandBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, std::max<std::size_t>(strands.size(), 1) * sizeof(fur::Strand), strands.empty() ? nullptr : strands.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glGenVertexArrays(1, &strandVertexArray);
}

BearRenderer::~BearRenderer()
{
	GLuint buffers[] = { skinBuffer, colorBuffer, surfaceBuffer, indexBuffer, strandBuffer };
	glDeleteBuffers(5, buffers);
	glDeleteVertexArrays(1, &vertexArray);
	glDeleteVertexArrays(1, &strandVertexArray);
}

void BearRenderer::upload(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
	const std::vector<glm::vec3>& furOffsets, const std::vector<glm::vec3>& combDirections)
{
	for (std::size_t i = 0; i < vertexCount; ++i)
	{
		staging[i * 4] = glm::vec4(positions[i], 1.0f);
		staging[i * 4 + 1] = glm::vec4(normals[i], 0.0f);
		staging[i * 4 + 2] = glm::vec4(i < furOffsets.size() ? furOffsets[i] : glm::vec3(0.0f), 0.0f);
		staging[i * 4 + 3] = glm::vec4(i < combDirections.size() ? combDirections[i] : normals[i], 0.0f);
	}
	glBindBuffer(GL_ARRAY_BUFFER, skinBuffer);
	//Orphaning: the driver can give us new memory while the GPU still draws the previous frame
	glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(glm::vec4), nullptr, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, staging.size() * sizeof(glm::vec4), staging.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void BearRenderer::drawSurface(Shader* shader)
{
	GLint modelLocation = shader->getUniformLocation("model");
	if (modelLocation >= 0)
	{
		setMatrix(shader, "model", glm::mat4(1.0f)); //the vertices are in world space
	}
	glBindVertexArray(vertexArray);
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
}

void BearRenderer::drawShaded(const BearRenderContext& context, glm::vec3 center)
{
	auto setCommonUniforms = [&](Shader* shader) {
		shader->enable();
		setMatrix(shader, "view", context.view);
		setMatrix(shader, "projection", context.projection);
		setMatrix(shader, "lightSpaceMatrix", context.lightSpaceMatrix);
		glUniform3fv(shader->getUniformLocation("worldEye"), 1, glm::value_ptr(context.eye));
		glUniform1i(shader->getUniformLocation("shadowMap"), 1); //bound by the game (Main.cpp draw)
		glUniform1i(shader->getUniformLocation("shadowMappingEnabled"), context.shadowMappingEnabled);
		if (context.light)
		{
			context.light->loadUniforms(shader);
		}
	};

	setCommonUniforms(skinShader.get());
	glBindVertexArray(vertexArray);
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

	if (strandCount == 0)
	{
		glBindVertexArray(0);
		return;
	}
	//Level of detail: the strands are in random order, so the first ones are spread over the whole body
	float distance = glm::distance(context.eye, center);
	float detail = std::clamp(std::pow(FULL_DETAIL_DISTANCE / std::max(distance, 0.01f), 2.0f), MIN_DETAIL, 1.0f);
	drawnStrandCount = std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<float>(strandCount) * detail));
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	float pixelSize = 2.0f / (context.projection[1][1] * static_cast<float>(std::max(viewport[3], 1)));

	setCommonUniforms(furShader.get());
	glUniform1i(furShader->getUniformLocation("segments"), STRAND_SEGMENTS);
	glUniform1f(furShader->getUniformLocation("maxFurLength"), fur::MAX_LENGTH);
	glUniform1f(furShader->getUniformLocation("strandWidth"), STRAND_WIDTH / std::sqrt(detail)); //fewer strands are wider
	glUniform1f(furShader->getUniformLocation("pixelSize"), pixelSize);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, skinBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, colorBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, strandBuffer);

	GLboolean blend = glIsEnabled(GL_BLEND), cullFace = glIsEnabled(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE); //the ribbons face the camera anyway
	glBindVertexArray(strandVertexArray);
	const GLsizei verticesPerStrand = 2 * (STRAND_SEGMENTS + 1) + 2;
	glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(drawnStrandCount) * verticesPerStrand);
	glBindVertexArray(0);
	if (!blend)
	{
		glDisable(GL_BLEND);
	}
	if (cullFace)
	{
		glEnable(GL_CULL_FACE);
	}
	for (GLuint binding = 0; binding < 3; ++binding)
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
	}
}

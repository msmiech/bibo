#include "TerrainRenderer.hpp"
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

using namespace bibo;
using namespace bibo::world;

TerrainRenderer::TerrainRenderer()
	: shader(std::make_unique<Shader>("Shader/terrain.vert", "Shader/terrain.frag")),
	depthShader(std::make_unique<Shader>("Shader/depth_shader.vert", "Shader/depth_shader.frag"))
{
}

TerrainRenderer::~TerrainRenderer()
{
	for (auto& entry : chunks)
	{
		release(entry.second);
	}
}

void TerrainRenderer::release(GpuChunk& chunk)
{
	glDeleteVertexArrays(1, &chunk.vertexArray);
	GLuint buffers[] = { chunk.vertexBuffer, chunk.indexBuffer };
	glDeleteBuffers(2, buffers);
}

void TerrainRenderer::apply(const WorldStreamer::MeshUpdate& update)
{
	auto existing = chunks.find(update.key);
	if (existing != chunks.end())
	{
		release(existing->second);
		chunks.erase(existing);
	}
	if (!update.mesh || update.mesh->empty())
	{
		return;
	}
	const ChunkMesh& mesh = *update.mesh;
	GpuChunk chunk;
	chunk.bounds = mesh.bounds;
	chunk.lod = mesh.lod;
	chunk.indexCount = static_cast<GLsizei>(mesh.indices.size());
	glGenVertexArrays(1, &chunk.vertexArray);
	glBindVertexArray(chunk.vertexArray);
	glGenBuffers(1, &chunk.vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, chunk.vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(TerrainVertex), mesh.vertices.data(), GL_STATIC_DRAW);
	const GLsizei stride = sizeof(TerrainVertex);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(TerrainVertex, material)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(TerrainVertex, skyVisibility)));
	glEnableVertexAttribArray(3);
	glGenBuffers(1, &chunk.indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(std::uint32_t), mesh.indices.data(), GL_STATIC_DRAW);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	chunks[update.key] = chunk;
}

int TerrainRenderer::draw(const scene::ViewFrustum* frustum)
{
	shader->enable();
	int drawn = 0;
	drawnTriangles = 0;
	for (auto& entry : chunks)
	{
		const GpuChunk& chunk = entry.second;
		glm::vec3 center = 0.5f * (chunk.bounds.minimum + chunk.bounds.maximum);
		float radius = 0.5f * glm::length(chunk.bounds.maximum - chunk.bounds.minimum);
		if (frustum && frustum->sphereInFrustum(center, radius) == scene::ViewFrustum::OUTSIDE)
		{
			continue;
		}
		glBindVertexArray(chunk.vertexArray);
		glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, nullptr);
		++drawn;
		drawnTriangles += static_cast<std::size_t>(chunk.indexCount) / 3;
	}
	glBindVertexArray(0);
	return drawn;
}

void TerrainRenderer::drawShadow(const render::ShadowCascade& cascade)
{
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(cascade.lightSpace));
	glUniformMatrix4fv(depthShader->getUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	for (auto& entry : chunks)
	{
		const GpuChunk& chunk = entry.second;
		glm::vec3 center = 0.5f * (chunk.bounds.minimum + chunk.bounds.maximum);
		float radius = 0.5f * glm::length(chunk.bounds.maximum - chunk.bounds.minimum);
		if (!cascade.contains(center, radius))
		{
			continue;
		}
		glBindVertexArray(chunk.vertexArray);
		glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, nullptr);
	}
	glBindVertexArray(0);
}

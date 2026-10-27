#include "TerrainRenderer.hpp"
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

using namespace bibo;
using namespace bibo::world;

TerrainRenderer::TerrainRenderer()
	: shader(std::make_unique<Shader>("Shader/terrain.vert", "Shader/terrain.frag")),
	ditherShader(std::make_unique<Shader>("Shader/terrain.vert", "Shader/terrain.frag", "LOD_DITHER")),
	depthShader(std::make_unique<Shader>("Shader/depth_shader.vert", "Shader/depth_shader.frag"))
{
}

TerrainRenderer::~TerrainRenderer()
{
	for (auto& entry : chunks)
	{
		release(entry.second);
	}
	for (auto& entry : fading)
	{
		release(entry.second.old);
	}
}

void TerrainRenderer::release(GpuChunk& chunk)
{
	glDeleteVertexArrays(1, &chunk.vertexArray);
	GLuint buffers[] = { chunk.vertexBuffer, chunk.indexBuffer };
	glDeleteBuffers(2, buffers);
}

void TerrainRenderer::apply(const WorldStreamer::MeshUpdate& update, float time)
{
	bool replaced = update.mesh && !update.mesh->empty();
	//A mesh that was still fading out is gone now, the current one fades out instead (unless it just came, e.g. while loading)
	auto previous = fading.find(update.key);
	if (previous != fading.end())
	{
		release(previous->second.old);
		fading.erase(previous);
	}
	auto existing = chunks.find(update.key);
	if (existing != chunks.end())
	{
		if (replaced && existing->second.uploadTime < time)
		{
			fading[update.key] = { existing->second, time };
		}
		else
		{
			release(existing->second);
		}
		chunks.erase(existing);
	}
	if (!replaced)
	{
		return;
	}
	const ChunkMesh& mesh = *update.mesh;
	GpuChunk chunk;
	chunk.bounds = mesh.bounds;
	chunk.lod = mesh.lod;
	chunk.uploadTime = time;
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

bool TerrainRenderer::drawChunk(const GpuChunk& chunk, const scene::ViewFrustum* frustum)
{
	glm::vec3 center = 0.5f * (chunk.bounds.minimum + chunk.bounds.maximum);
	float radius = 0.5f * glm::length(chunk.bounds.maximum - chunk.bounds.minimum);
	if (frustum && frustum->sphereInFrustum(center, radius) == scene::ViewFrustum::OUTSIDE)
	{
		return false;
	}
	glBindVertexArray(chunk.vertexArray);
	glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, nullptr);
	drawnTriangles += static_cast<std::size_t>(chunk.indexCount) / 3;
	return true;
}

int TerrainRenderer::draw(const scene::ViewFrustum* frustum, float time)
{
	for (auto it = fading.begin(); it != fading.end();)
	{
		if (time - it->second.start >= FADE_TIME || time < it->second.start)
		{
			release(it->second.old);
			it = fading.erase(it);
		}
		else
		{
			++it;
		}
	}
	int drawn = 0;
	drawnTriangles = 0;
	shader->enable();
	for (auto& entry : chunks)
	{
		if (!fading.contains(entry.first) && drawChunk(entry.second, frustum))
		{
			++drawn;
		}
	}
	//The fading chunks: the old and the new mesh each on their part of the pixels
	if (!fading.empty())
	{
		ditherShader->enable();
		GLint fadeLocation = ditherShader->getUniformLocation("lodFade");
		for (auto& entry : fading)
		{
			float fade = (time - entry.second.start) / FADE_TIME;
			glUniform1f(fadeLocation, fade - 1.0f);
			bool visible = drawChunk(entry.second.old, frustum);
			auto current = chunks.find(entry.first);
			if (current != chunks.end())
			{
				glUniform1f(fadeLocation, fade);
				visible = drawChunk(current->second, frustum) || visible;
			}
			drawn += visible ? 1 : 0;
		}
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

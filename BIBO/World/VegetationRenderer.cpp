#include "VegetationRenderer.hpp"
#include <algorithm>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

using namespace bibo;
using namespace bibo::world;

namespace
{
	const int MAX_LODS = 3;

	//Distances of the levels of detail, the largest distance, the distance up to which it casts shadows (0: never),
	//how much it sways in the wind, whether it is seen from both sides and whether the distances grow with its scale
	const struct
	{
		float lod1, lod2, maximum, shadow, wind;
		bool twoSided, scaled;
	} MODEL_SETTINGS[MODEL_COUNT] = {
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, true, false }, //SPRUCE_TALL
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, true, false }, //SPRUCE_WIDE
		{ 40.0f, 120.0f, 900.0f, 300.0f, 0.3f, true, false }, //SPRUCE_YOUNG
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.4f, true, false }, //PINE
		{ 30.0f, 1e9f, 220.0f, 60.0f, 0.08f, false, false }, //BUSH
		{ 30.0f, 1e9f, 220.0f, 60.0f, 0.06f, true, false }, //JUNIPER
		{ 1e9f, 1e9f, 42.0f, 0.0f, 0.12f, true, false }, //FERN
		{ 1e9f, 1e9f, 40.0f, 0.0f, 0.12f, true, false }, //GRASS
		{ 1e9f, 1e9f, 40.0f, 0.0f, 0.12f, true, false }, //GRASS_DRY
		{ 1e9f, 1e9f, 28.0f, 0.0f, 0.02f, false, false }, //MUSHROOM_RED
		{ 1e9f, 1e9f, 28.0f, 0.0f, 0.02f, false, false }, //MUSHROOM_BROWN
		{ 1e9f, 1e9f, 60.0f, 0.0f, 0.02f, false, false }, //MUSHROOM_GLOW
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, false, true }, //STONE_ROUND
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, false, true }, //STONE_FLAT
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, false, true }, //STONE_SHARP
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, false, true }, //STONE_STANDING
		{ 35.0f, 1e9f, 200.0f, 120.0f, 0.0f, false, false }, //LOG
		{ 35.0f, 1e9f, 160.0f, 80.0f, 0.0f, false, false } }; //STUMP
}

void VegetationRenderer::ColumnData::updateBounds()
{
	glm::vec3 low(1e9f), high(-1e9f);
	float reach = 0.0f;
	for (const std::vector<Instance>& layer : layers)
	{
		for (const Instance& instance : layer)
		{
			low = glm::min(low, glm::vec3(instance.positionScale));
			high = glm::max(high, glm::vec3(instance.positionScale));
			reach = std::max(reach, (modelHeight(instance.model()) + modelRadius(instance.model())) * instance.positionScale.w);
		}
	}
	if (low.x > high.x)
	{
		center = glm::vec3(0.0f);
		radius = 0.0f;
		return;
	}
	center = 0.5f * (low + high);
	radius = 0.5f * glm::length(high - low) + reach;
}

VegetationRenderer::VegetationRenderer(float detail)
	: shader(std::make_unique<Shader>("Shader/vegetation.vert", "Shader/vegetation.frag")),
	depthShader(std::make_unique<Shader>("Shader/vegetation_depth.vert", "Shader/depth_shader.frag")),
	detail(detail)
{
	glGenBuffers(1, &instanceBuffer);
	models.resize(MODEL_COUNT);
	sorted.resize(MODEL_COUNT * MAX_LODS);
	for (int m = 0; m < MODEL_COUNT; ++m)
	{
		ModelData& model = models[m];
		model.lodDistances[0] = MODEL_SETTINGS[m].lod1;
		model.lodDistances[1] = MODEL_SETTINGS[m].lod2;
		model.maxDistance = MODEL_SETTINGS[m].maximum;
		model.shadowDistance = MODEL_SETTINGS[m].shadow;
		model.windAmplitude = MODEL_SETTINGS[m].wind;
		model.twoSided = MODEL_SETTINGS[m].twoSided;
		model.scaleDistances = MODEL_SETTINGS[m].scaled;
		for (const VegetationMesh& mesh : buildModel(static_cast<Model>(m)))
		{
			GpuMesh gpu;
			gpu.indexCount = static_cast<GLsizei>(mesh.indices.size());
			gpu.radius = mesh.radius;
			glGenVertexArrays(1, &gpu.vertexArray);
			glBindVertexArray(gpu.vertexArray);
			glGenBuffers(1, &gpu.vertexBuffer);
			glBindBuffer(GL_ARRAY_BUFFER, gpu.vertexBuffer);
			glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(VegetationVertex), mesh.vertices.data(), GL_STATIC_DRAW);
			glGenBuffers(1, &gpu.indexBuffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.indexBuffer);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(std::uint32_t), mesh.indices.data(), GL_STATIC_DRAW);
			//Binding 0: the vertices of the model, binding 1: the instances (set for every batch)
			glBindVertexBuffer(0, gpu.vertexBuffer, 0, sizeof(VegetationVertex));
			glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, offsetof(VegetationVertex, position));
			glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE, offsetof(VegetationVertex, normal));
			glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE, offsetof(VegetationVertex, uv));
			glVertexAttribFormat(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(VegetationVertex, color));
			glVertexAttribFormat(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(VegetationVertex, parameters));
			for (GLuint attribute = 0; attribute < 5; ++attribute)
			{
				glVertexAttribBinding(attribute, 0);
				glEnableVertexAttribArray(attribute);
			}
			glBindVertexBuffer(1, instanceBuffer, 0, sizeof(Instance));
			glVertexBindingDivisor(1, 1);
			glVertexAttribFormat(5, 4, GL_FLOAT, GL_FALSE, offsetof(Instance, positionScale));
			glVertexAttribFormat(6, 4, GL_FLOAT, GL_FALSE, offsetof(Instance, rotation));
			glVertexAttribFormat(7, 4, GL_FLOAT, GL_FALSE, offsetof(Instance, parameters));
			for (GLuint attribute = 5; attribute < 8; ++attribute)
			{
				glVertexAttribBinding(attribute, 1);
				glEnableVertexAttribArray(attribute);
			}
			glBindVertexArray(0);
			model.lods.push_back(gpu);
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

VegetationRenderer::~VegetationRenderer()
{
	for (ModelData& model : models)
	{
		for (GpuMesh& mesh : model.lods)
		{
			glDeleteVertexArrays(1, &mesh.vertexArray);
			GLuint buffers[] = { mesh.vertexBuffer, mesh.indexBuffer };
			glDeleteBuffers(2, buffers);
		}
	}
	glDeleteBuffers(1, &instanceBuffer);
}

void VegetationRenderer::apply(const WorldStreamer::VegetationUpdate& update)
{
	ColumnData& column = columns[update.key];
	std::vector<Instance>& layer = column.layers[update.layer == Layer::LARGE ? 0 : 1];
	if (update.instances)
	{
		layer = *update.instances;
	}
	else
	{
		layer.clear();
	}
	column.updateBounds();
}

std::size_t VegetationRenderer::getInstanceCount() const
{
	std::size_t count = 0;
	for (const auto& entry : columns)
	{
		count += entry.second.layers[0].size() + entry.second.layers[1].size();
	}
	return count;
}

void VegetationRenderer::collect(const scene::ViewFrustum* frustum, const render::ShadowCascade* shadowCascade, glm::vec3 camera)
{
	for (std::vector<Instance>& list : sorted)
	{
		list.clear();
	}
	const float reach = 1100.0f * detail;
	for (const auto& entry : columns)
	{
		const ColumnData& column = entry.second;
		if (column.radius <= 0.0f || glm::distance(column.center, camera) - column.radius > reach)
		{
			continue;
		}
		if (shadowCascade ? !shadowCascade->contains(column.center, column.radius)
			: frustum->sphereInFrustum(column.center, column.radius) == scene::ViewFrustum::OUTSIDE)
		{
			continue;
		}
		//Nothing small casts a shadow
		for (int layer = 0; layer < (shadowCascade ? 1 : 2); ++layer)
		{
			for (const Instance& instance : column.layers[layer])
			{
				int m = static_cast<int>(instance.model());
				const ModelData& model = models[m];
				glm::vec3 position(instance.positionScale);
				float scale = instance.positionScale.w;
				float distance = glm::distance(position, camera);
				float factor = detail * (model.scaleDistances ? std::max(scale, 0.3f) : 1.0f);
				if (distance > model.maxDistance * factor)
				{
					continue;
				}
				float radius = model.lods[0].radius * scale;
				glm::vec3 center = position + glm::vec3(0.0f, 0.5f * modelHeight(instance.model()) * scale, 0.0f);
				if (shadowCascade)
				{
					if (model.shadowDistance <= 0.0f || distance > model.shadowDistance * factor || (model.scaleDistances && scale < 0.7f)
						|| !shadowCascade->contains(center, radius))
					{
						continue;
					}
				}
				else if (radius > 2.0f && frustum->sphereInFrustum(center, radius) == scene::ViewFrustum::OUTSIDE)
				{
					continue;
				}
				int lod = distance < model.lodDistances[0] * factor ? 0 : distance < model.lodDistances[1] * factor ? 1 : 2;
				if (shadowCascade && lod == 0 && distance > 25.0f)
				{
					lod = 1; //the shadow of a coarser model looks the same
				}
				lod = std::min(lod, static_cast<int>(model.lods.size()) - 1);
				sorted[m * MAX_LODS + lod].push_back(instance);
			}
		}
	}
	staging.clear();
	batches.clear();
	for (int m = 0; m < MODEL_COUNT; ++m)
	{
		for (int lod = 0; lod < MAX_LODS; ++lod)
		{
			const std::vector<Instance>& list = sorted[m * MAX_LODS + lod];
			if (!list.empty())
			{
				batches.push_back({ m, lod, staging.size(), list.size() });
				staging.insert(staging.end(), list.begin(), list.end());
			}
		}
	}
	if (!staging.empty())
	{
		glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
		//New memory, the graphics card may still read the previous instances
		glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(Instance), nullptr, GL_STREAM_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, staging.size() * sizeof(Instance), staging.data());
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

void VegetationRenderer::drawBatches(Shader& program, bool shadowPass)
{
	GLint windLocation = program.getUniformLocation("windAmplitude");
	GLint fadeLocation = program.getUniformLocation("fadeDistances");
	for (const Batch& batch : batches)
	{
		const ModelData& model = models[batch.model];
		const GpuMesh& mesh = model.lods[batch.lod];
		if (!shadowPass)
		{
			model.twoSided ? glDisable(GL_CULL_FACE) : glEnable(GL_CULL_FACE);
		}
		glUniform1f(windLocation, model.windAmplitude);
		float maximum = model.maxDistance * detail;
		if (model.scaleDistances)
		{
			glUniform2f(fadeLocation, 1e9f, 2e9f); //the distance depends on the size of the stone
		}
		else
		{
			glUniform2f(fadeLocation, maximum * 0.75f, maximum);
		}
		glBindVertexArray(mesh.vertexArray);
		glBindVertexBuffer(1, instanceBuffer, static_cast<GLintptr>(batch.first * sizeof(Instance)), sizeof(Instance));
		glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(batch.count));
	}
	glBindVertexArray(0);
}

int VegetationRenderer::draw(const scene::ViewFrustum& frustum, glm::vec3 camera)
{
	collect(&frustum, nullptr, camera);
	GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
	shader->enable();
	drawBatches(*shader, false);
	cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
	return static_cast<int>(staging.size());
}

void VegetationRenderer::drawShadow(const render::ShadowCascade& cascade, glm::vec3 camera)
{
	collect(nullptr, &cascade, camera);
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(cascade.lightSpace));
	drawBatches(*depthShader, true);
}

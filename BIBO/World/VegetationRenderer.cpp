#include "VegetationRenderer.hpp"
#include <algorithm>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

using namespace bibo;
using namespace bibo::world;

namespace
{
	const int MAX_LODS = 3;
	//Width of the cross-fade between two levels of detail (relative to the distance where they change)
	const float LOD_BAND = 0.2f;
	//The stones dissolve over this last part of their largest distance
	const float DISSOLVE_BAND = 0.15f;

	//Distances of the levels of detail, the largest distance, the distance up to which it casts shadows (0: never),
	//how much it sways in the wind, how far the bear and the animals push it aside (relative to its height), whether it
	//is seen from both sides and whether the distances grow with its scale
	const struct
	{
		float lod1, lod2, maximum, shadow, wind, interaction;
		bool twoSided, scaled;
	} MODEL_SETTINGS[MODEL_COUNT] = {
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, 0.0f, true, false }, //SPRUCE_TALL
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, 0.0f, true, false }, //SPRUCE_WIDE
		{ 40.0f, 120.0f, 900.0f, 300.0f, 0.3f, 0.0f, true, false }, //SPRUCE_YOUNG
		{ 50.0f, 150.0f, 1100.0f, 450.0f, 0.4f, 0.0f, true, false }, //PINE
		{ 30.0f, 1e9f, 220.0f, 60.0f, 0.08f, 0.35f, false, false }, //BUSH
		{ 30.0f, 1e9f, 220.0f, 60.0f, 0.06f, 0.4f, true, false }, //JUNIPER
		{ 1e9f, 1e9f, 42.0f, 0.0f, 0.12f, 0.8f, true, false }, //FERN
		{ 1e9f, 1e9f, 40.0f, 0.0f, 0.12f, 0.9f, true, false }, //GRASS
		{ 1e9f, 1e9f, 40.0f, 0.0f, 0.12f, 0.9f, true, false }, //GRASS_DRY
		{ 1e9f, 1e9f, 28.0f, 0.0f, 0.02f, 0.5f, false, false }, //MUSHROOM_RED
		{ 1e9f, 1e9f, 28.0f, 0.0f, 0.02f, 0.5f, false, false }, //MUSHROOM_BROWN
		{ 1e9f, 1e9f, 60.0f, 0.0f, 0.02f, 0.5f, false, false }, //MUSHROOM_GLOW
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true }, //STONE_ROUND
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true }, //STONE_FLAT
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true }, //STONE_SHARP
		{ 12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true }, //STONE_STANDING
		{ 35.0f, 1e9f, 200.0f, 120.0f, 0.0f, 0.0f, false, false }, //LOG
		{ 35.0f, 1e9f, 160.0f, 80.0f, 0.0f, 0.0f, false, false } }; //STUMP

	Instance withFade(const Instance& instance, float fade)
	{
		Instance faded = instance;
		faded.parameters.w = fade;
		return faded;
	}
}

LodBlend bibo::world::blendLod(float distance, const float* lodDistances, int lodCount, float band)
{
	for (int i = 0; i < lodCount - 1; ++i)
	{
		float start = lodDistances[i] * (1.0f - 0.5f * band), end = lodDistances[i] * (1.0f + 0.5f * band);
		if (distance < start)
		{
			return { i, 0.0f };
		}
		if (distance < end)
		{
			return { i, (distance - start) / (end - start) };
		}
	}
	return { std::max(lodCount - 1, 0), 0.0f };
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
	ditherShader(std::make_unique<Shader>("Shader/vegetation.vert", "Shader/vegetation.frag", "LOD_DITHER")),
	depthShader(std::make_unique<Shader>("Shader/vegetation_depth.vert", "Shader/depth_shader.frag")),
	detail(detail)
{
	glGenBuffers(1, &instanceBuffer);
	models.resize(MODEL_COUNT);
	sorted.resize(MODEL_COUNT * MAX_LODS);
	sortedTouched.resize(MODEL_COUNT * MAX_LODS);
	sortedDithered.resize(MODEL_COUNT * MAX_LODS);
	for (int m = 0; m < MODEL_COUNT; ++m)
	{
		ModelData& model = models[m];
		model.lodDistances[0] = MODEL_SETTINGS[m].lod1;
		model.lodDistances[1] = MODEL_SETTINGS[m].lod2;
		model.maxDistance = MODEL_SETTINGS[m].maximum;
		model.shadowDistance = MODEL_SETTINGS[m].shadow;
		model.windAmplitude = MODEL_SETTINGS[m].wind;
		model.interaction = MODEL_SETTINGS[m].interaction;
		model.height = modelHeight(static_cast<Model>(m));
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

bool VegetationRenderer::isTouched(const Instance& instance, const ModelData& model) const
{
	if (model.interaction <= 0.0f)
	{
		return false;
	}
	//Like pushAside in Shader/include/vegetation.glsl: a part of the plant within the reach of the body, the body not above it
	glm::vec3 foot(instance.positionScale);
	float scale = instance.positionScale.w;
	float spread = modelRadius(instance.model()) * scale, height = model.height * scale;
	for (const FoliageInteraction::Imprint& body : interactors)
	{
		glm::vec2 away(foot.x - body.center.x, foot.z - body.center.z);
		float reach = body.radius * 1.15f + spread;
		if (glm::dot(away, away) < reach * reach && body.center.y - body.radius < foot.y + height + 0.1f
			&& foot.y - body.center.y < body.radius + 1.0f)
		{
			return true;
		}
	}
	return false;
}

void VegetationRenderer::collect(const scene::ViewFrustum* frustum, const render::ShadowCascade* shadowCascade, glm::vec3 camera)
{
	for (std::vector<Instance>& list : sorted)
	{
		list.clear();
	}
	for (std::vector<Instance>& list : sortedTouched)
	{
		list.clear();
	}
	for (std::vector<Instance>& list : sortedDithered)
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
				float maximum = model.maxDistance * factor;
				if (distance > maximum)
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
				int lodCount = std::min(static_cast<int>(model.lods.size()), MAX_LODS);
				const float lodDistances[2] = { model.lodDistances[0] * factor, model.lodDistances[1] * factor };
				LodBlend blend = blendLod(distance, lodDistances, lodCount, LOD_BAND);
				int nearest = blend.next < 0.5f ? blend.lod : blend.lod + 1;
				std::vector<std::vector<Instance>>& opaque = isTouched(instance, model) ? sortedTouched : sorted;
				if (shadowCascade)
				{
					//One level is enough for the shadow, the shadow of a coarser model looks the same
					if (nearest == 0 && distance > 25.0f)
					{
						nearest = std::min(1, lodCount - 1);
					}
					opaque[m * MAX_LODS + nearest].push_back(instance);
					continue;
				}
				//The stones dissolve before they are left out (they do not shrink, their distance depends on their size)
				float dissolve = model.scaleDistances ? (distance - maximum * (1.0f - DISSOLVE_BAND)) / (maximum * DISSOLVE_BAND) : 0.0f;
				if (dissolve > 0.0f)
				{
					sortedDithered[m * MAX_LODS + nearest].push_back(withFade(instance, 1.0f - dissolve));
				}
				else if (blend.next > 0.0f)
				{
					//The finer level gives way to the coarser one on the same pixels
					sortedDithered[m * MAX_LODS + blend.lod].push_back(withFade(instance, blend.next - 1.0f));
					sortedDithered[m * MAX_LODS + blend.lod + 1].push_back(withFade(instance, blend.next));
				}
				else
				{
					opaque[m * MAX_LODS + blend.lod].push_back(instance);
				}
			}
		}
	}
	staging.clear();
	batches.clear();
	//The fading instances are few, they are bent without checking whether a body is close
	const struct
	{
		const std::vector<std::vector<Instance>>& lists;
		bool dithered, interactive;
	} sets[] = { { sorted, false, false }, { sortedTouched, false, true }, { sortedDithered, true, true } };
	for (const auto& set : sets)
	{
		for (int m = 0; m < MODEL_COUNT; ++m)
		{
			for (int lod = 0; lod < MAX_LODS; ++lod)
			{
				const std::vector<Instance>& list = set.lists[m * MAX_LODS + lod];
				if (!list.empty())
				{
					batches.push_back({ m, lod, staging.size(), list.size(), set.dithered, set.interactive });
					staging.insert(staging.end(), list.begin(), list.end());
				}
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

void VegetationRenderer::drawBatches(Shader& program, bool shadowPass, bool dithered)
{
	GLint windLocation = program.getUniformLocation("windAmplitude");
	GLint fadeLocation = program.getUniformLocation("fadeDistances");
	GLint interactionLocation = program.getUniformLocation("interaction");
	for (const Batch& batch : batches)
	{
		if (batch.dithered != dithered)
		{
			continue;
		}
		const ModelData& model = models[batch.model];
		const GpuMesh& mesh = model.lods[batch.lod];
		if (!shadowPass)
		{
			model.twoSided ? glDisable(GL_CULL_FACE) : glEnable(GL_CULL_FACE);
		}
		glUniform1f(windLocation, model.windAmplitude);
		glUniform2f(interactionLocation, batch.interactive ? model.interaction : 0.0f, model.height);
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
	drawBatches(*shader, false, false);
	//The fading instances with the shader that leaves out pixels (the others keep the early depth test)
	ditherShader->enable();
	drawBatches(*ditherShader, false, true);
	cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
	return static_cast<int>(staging.size());
}

void VegetationRenderer::drawShadow(const render::ShadowCascade& cascade, glm::vec3 camera)
{
	collect(nullptr, &cascade, camera);
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(cascade.lightSpace));
	drawBatches(*depthShader, true, false);
}

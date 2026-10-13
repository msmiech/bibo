#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include <GL/glew.h>
#include "Vegetation.hpp"
#include "VegetationMeshes.hpp"
#include "WorldStreamer.hpp"
#include "../Render/ShadowCascades.hpp"
#include "../Resources/Shader.hpp"
#include "../Scene/ViewFrustum.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* Draws the instances of the vegetation. Every frame (and every shadow map) it collects the visible instances of the
		* columns in the view, picks the level of detail of every instance by its distance from the camera and draws each
		* model and level with one instanced draw call. Far away the models shrink to nothing before they are left out.
		*/
		class VegetationRenderer
		{
		public:
			/**
			* @param detail 1: normal, smaller: models are left out and get coarser closer to the camera
			*/
			explicit VegetationRenderer(float detail);
			~VegetationRenderer();
			VegetationRenderer(const VegetationRenderer&) = delete;
			VegetationRenderer& operator=(const VegetationRenderer&) = delete;

			void apply(const WorldStreamer::VegetationUpdate& update);
			/**
			* @return Number of drawn instances
			*/
			int draw(const scene::ViewFrustum& frustum, glm::vec3 camera);
			void drawShadow(const render::ShadowCascade& cascade, glm::vec3 camera);
			void setDetail(float newDetail) { detail = newDetail; }
			std::size_t getInstanceCount() const;

		private:
			struct GpuMesh
			{
				GLuint vertexArray = 0, vertexBuffer = 0, indexBuffer = 0;
				GLsizei indexCount = 0;
				float radius = 0.0f;
			};
			struct ModelData
			{
				std::vector<GpuMesh> lods;
				float lodDistances[2] = { 1e9f, 1e9f };
				float maxDistance = 100.0f;
				float shadowDistance = 0.0f; //0: no shadow
				float windAmplitude = 0.0f;
				bool twoSided = false;
				bool scaleDistances = false; //the distances grow with the scale (stones)
			};
			struct ColumnData
			{
				std::vector<Instance> layers[2]; //large, small
				glm::vec3 center = glm::vec3(0.0f);
				float radius = 0.0f;
				void updateBounds();
			};
			struct Batch
			{
				int model, lod;
				std::size_t first, count;
			};

			/**
			* Sorts the instances into batches (model and level of detail) and uploads them
			* @param shadowCascade nullptr: for the camera
			*/
			void collect(const scene::ViewFrustum* frustum, const render::ShadowCascade* shadowCascade, glm::vec3 camera);
			void drawBatches(Shader& shader, bool shadowPass);

			std::unique_ptr<Shader> shader;
			std::unique_ptr<Shader> depthShader;
			std::vector<ModelData> models;
			std::unordered_map<ColumnKey, ColumnData, ColumnKeyHash> columns;
			GLuint instanceBuffer = 0;
			std::vector<std::vector<Instance>> sorted; //per model and level of detail
			std::vector<Instance> staging;
			std::vector<Batch> batches;
			float detail;
		};
	}
}

#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "BearRenderer.hpp"
#include "HedgehogModel.hpp"
#include "ViewFrustum.hpp"
#include "../Resources/Shader.hpp"

namespace bibo
{
	namespace scene
	{
		/**
		* Draws the hedgehogs like the bear (the shaders of the BearRenderer): the skin with the eyes and the nose, the fur on the
		* face and the belly and the spines (stiff, banded strands). The vertices of all hedgehogs are moved on the CPU (setPose)
		* and uploaded once per frame, the strands are generated on the GPU. Far away fewer, wider strands are drawn.
		* Needs an OpenGL context.
		*/
		class HedgehogRenderer
		{
		public:
			static constexpr std::size_t SPINE_COUNT = 4500;
			static constexpr std::size_t FUR_COUNT = 16000;
			//Length of the longest spines (world units), the fur is as long as the fur of the bear
			static constexpr float SPINE_LENGTH = 0.24f;
			static constexpr float SPINE_WIDTH = 0.016f;

			/**
			* @param count Number of hedgehogs
			*/
			explicit HedgehogRenderer(std::size_t count);
			~HedgehogRenderer();
			HedgehogRenderer(const HedgehogRenderer&) = delete;
			HedgehogRenderer& operator=(const HedgehogRenderer&) = delete;

			/**
			* Where a hedgehog is in this frame
			* @param body Model matrix of the body, feet: of the feet (with the scale)
			* @param bristle 0 to 1: the spines stand up (when it is startled)
			*/
			void setPose(std::size_t index, const glm::mat4& body, const std::array<glm::mat4, 4>& feet, float bristle);
			/**
			* Only the surfaces (e.g. into the shadow map), the shader needs a "position" attribute at location 0
			*/
			void drawSurfaces(Shader* shader);
			/**
			* Skin, eyes, fur and spines with lighting and shadows (the hedgehogs outside of the frustum are left out)
			*/
			void drawShaded(const BearRenderContext& context, const ViewFrustum& frustum);
			/**
			* @return Number of hedgehogs drawn by the last drawShaded
			*/
			int getDrawnCount() const { return drawnCount; }
			const HedgehogModel& getModel() const { return model; }

		private:
			void upload();
			void bindVertices(std::size_t index);

			HedgehogModel model;
			std::size_t count;
			std::size_t vertexCount;
			GLsizeiptr blockSize; //bytes of the vertices of one hedgehog in the skin buffer (aligned for the shader storage buffer)
			std::unique_ptr<Shader> skinShader;
			std::unique_ptr<Shader> furShader;
			GLuint vertexArray = 0, strandVertexArray = 0;
			GLuint skinBuffer = 0, colorBuffer = 0, surfaceBuffer = 0, indexBuffer = 0;
			GLuint spineBuffer = 0, furBuffer = 0;
			std::size_t spineCount = 0, furCount = 0;
			GLsizei indexCount = 0;
			std::vector<glm::vec4> staging; //position, normal, bend, comb per vertex, a block per hedgehog
			std::vector<glm::vec3> centers;
			std::vector<float> scales;
			bool uploaded = false;
			int drawnCount = 0;
		};
	}
}

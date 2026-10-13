#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

namespace bibo
{
	namespace render
	{
		constexpr int MAX_SHADOW_CASCADES = 4;

		/**
		* The data of a frame that all shaders of the scene use (uniform buffer, layout std140, see Shader/include/frame.glsl)
		*/
		struct FrameUniforms
		{
			glm::mat4 view = glm::mat4(1.0f);
			glm::mat4 projection = glm::mat4(1.0f);
			glm::mat4 viewProjection = glm::mat4(1.0f);
			glm::mat4 inverseViewProjection = glm::mat4(1.0f);
			glm::mat4 lightSpace[MAX_SHADOW_CASCADES];
			glm::vec4 cascadeSplits = glm::vec4(0.0f);
			glm::vec4 cascadeTexelSizes = glm::vec4(0.0f);
			glm::vec4 cameraPositionTime = glm::vec4(0.0f);
			glm::vec4 sunDirectionShadows = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
			glm::vec4 sunColorNormalMapping = glm::vec4(1.0f);
			glm::vec4 skyAmbientSnow = glm::vec4(0.0f);
			glm::vec4 groundAmbient = glm::vec4(0.0f);
			glm::vec4 fogDensity = glm::vec4(0.0f);
			glm::vec4 wind = glm::vec4(0.0f);
			glm::vec4 viewport = glm::vec4(1.0f);
			glm::vec4 worldMap = glm::vec4(0.0f);
			glm::vec4 shadowSettings = glm::vec4(0.0f);
		};
		static_assert(sizeof(FrameUniforms) == 8 * 64 + 12 * 16, "FrameUniforms must match the uniform block Frame (std140)");

		/**
		* Uniform buffer with the FrameUniforms, bound to binding point 0
		*/
		class FrameUniformBuffer
		{
		public:
			static const GLuint BINDING = 0;
			FrameUniformBuffer();
			~FrameUniformBuffer();
			FrameUniformBuffer(const FrameUniformBuffer&) = delete;
			FrameUniformBuffer& operator=(const FrameUniformBuffer&) = delete;

			void upload(const FrameUniforms& uniforms);
			void bind() const;

		private:
			GLuint buffer = 0;
		};
	}
}

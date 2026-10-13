#pragma once
#include <memory>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "FrameUniforms.hpp"
#include "RenderUtil.hpp"
#include "ShadowCascades.hpp"
#include "../Resources/Shader.hpp"

namespace bibo
{
	namespace render
	{
		/**
		* Graphics options of a frame (the settings of the game)
		*/
		struct RenderSettings
		{
			bool hdr = true; //tone mapping and auto exposure
			bool bloom = true;
			bool shadows = true;
			bool normalMapping = true;
			bool transparency = true;
			bool wireframe = false;
		};

		/**
		* The camera the frame is rendered from
		*/
		struct CameraView
		{
			glm::mat4 view = glm::mat4(1.0f);
			glm::mat4 projection = glm::mat4(1.0f);
			glm::vec3 position = glm::vec3(0.0f);
			float fieldOfView = 60.0f; //vertical, degrees
			float aspectRatio = 1.0f;
			float nearPlane = 0.1f;
		};

		/**
		* What the renderer needs from the scene
		*/
		class RenderableScene
		{
		public:
			virtual ~RenderableScene() = default;
			virtual CameraView cameraView() = 0;
			/**
			* Sun, sky, fog, wind, time (the renderer sets the camera, the shadows and the settings)
			*/
			virtual void fillFrameUniforms(FrameUniforms& uniforms) = 0;
			/**
			* Before the scene is drawn, after the frame uniforms are uploaded (e.g. the sky cube map)
			*/
			virtual void prepareFrame() = 0;
			/**
			* Everything that casts a shadow, into the shadow map of the cascade (depth only)
			*/
			virtual void drawShadowCasters(const ShadowCascade& cascade) = 0;
			/**
			* Everything, shaded, into the HDR image (the depth buffer is cleared)
			*/
			virtual void drawMain(const RenderSettings& settings) = 0;
		};

		/**
		* Renders a frame: the cascaded shadow maps of the sun, the scene into an HDR image, then the bloom, the auto exposure
		* (eye adaptation) and the tone mapping into the window
		*/
		class Renderer
		{
		public:
			static constexpr int CASCADE_COUNT = 4;

			Renderer(int width, int height);
			~Renderer();
			Renderer(const Renderer&) = delete;
			Renderer& operator=(const Renderer&) = delete;

			/**
			* Size of the window (framebuffer) in pixels
			*/
			void resize(int width, int height);
			/**
			* Renders the scene into the framebuffer that is bound (the window); the HUD and menus are drawn afterwards
			* @param timeDelta For the eye adaptation
			*/
			void render(RenderableScene& scene, const RenderSettings& settings, float timeDelta);
			/**
			* The next frame does not adapt the exposure slowly (e.g. after a jump of the camera)
			*/
			void resetExposure() { exposureReset = true; }
			const std::vector<ShadowCascade>& getCascades() const { return cascades; }
			int getShadowMapSize() const { return shadowSize; }

		private:
			void createTargets();
			void releaseTargets();
			void renderShadows(RenderableScene& scene);
			void postProcess(const RenderSettings& settings, float timeDelta);

			int width, height;
			int shadowSize;
			FrameUniformBuffer frameUniforms;
			std::vector<ShadowCascade> cascades;
			FullscreenTriangle triangle;

			std::unique_ptr<Shader> brightPassShader, blurShader, luminanceShader, exposureShader, compositeShader;
			GLuint shadowMaps = 0, shadowFramebuffer = 0;
			GLuint hdrFramebuffer = 0, hdrColor = 0, hdrDepth = 0;
			GLuint bloomFramebuffers[2] = {}, bloomTextures[2] = {};
			GLuint luminanceFramebuffer = 0, luminanceTexture = 0;
			GLuint exposureFramebuffers[2] = {}, exposureTextures[2] = {};
			int exposureIndex = 0;
			bool exposureReset = true;
		};
	}
}

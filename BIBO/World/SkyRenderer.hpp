#pragma once
#include <memory>
#include <GL/glew.h>
#include "../Render/RenderUtil.hpp"
#include "../Resources/Shader.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* The sky: a cube map of the sky in all directions (clouds, polar lights), one face is rendered again per frame,
		* so the polar lights and clouds move. Its blurred mip levels light the scene (ambient light, reflections, fog).
		* The background is the cube map and the disk of the sun.
		*/
		class SkyRenderer
		{
		public:
			/**
			* @param lowDetail Smaller cube map and fewer samples of the polar lights (software rendering)
			*/
			explicit SkyRenderer(bool lowDetail);
			~SkyRenderer();
			SkyRenderer(const SkyRenderer&) = delete;
			SkyRenderer& operator=(const SkyRenderer&) = delete;

			/**
			* Renders the next face of the cube map (all of them the first time). Needs the uniforms of the frame (sun, time).
			*/
			void update();
			/**
			* Binds the cube map to its texture unit (render::TextureUnit::ENVIRONMENT)
			*/
			void bind() const;
			/**
			* Draws the sky where nothing was drawn yet (at the far plane)
			*/
			void draw();
			GLuint getEnvironment() const { return environment; }

		private:
			void renderFace(int face);

			std::unique_ptr<Shader> environmentShader;
			std::unique_ptr<Shader> skyShader;
			render::FullscreenTriangle triangle;
			GLuint environment = 0;
			GLuint framebuffer = 0;
			int size;
			int auroraSteps;
			int nextFace = 0;
			bool complete = false;
		};
	}
}

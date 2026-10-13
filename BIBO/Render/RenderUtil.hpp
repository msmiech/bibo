#pragma once
#include <GL/glew.h>

namespace bibo
{
	namespace render
	{
		/**
		* Texture units of the scene (the shaders declare them with layout(binding = ...))
		*/
		namespace TextureUnit
		{
			const int COLOR = 0; //legacy geometries: their texture
			const int SHADOW_MAPS = 1; //cascaded shadow maps (sampler2DArrayShadow)
			const int NORMAL_MAP = 2; //legacy geometries: their normal map
			const int MATERIAL_ALBEDO = 3; //generated materials (sampler2DArray): color and roughness
			const int MATERIAL_NORMAL = 4; //generated materials: normal, height and occlusion
			const int ENVIRONMENT = 5; //sky cube map
			const int WORLD_MAP = 6; //top view of the island: height of the ground and of the cave roofs
		}

		/**
		* @return true for a renderer without a graphics card (e.g. Mesa's llvmpipe on servers without GPU): the world is
		* drawn with less detail, so that it is not too slow (e.g. the tests and the recording of the README GIF)
		*/
		bool isSoftwareRenderer();

		/**
		* Draws a triangle that covers the viewport (for full screen passes with Shader/fullscreen.vert)
		*/
		class FullscreenTriangle
		{
		public:
			FullscreenTriangle();
			~FullscreenTriangle();
			FullscreenTriangle(const FullscreenTriangle&) = delete;
			FullscreenTriangle& operator=(const FullscreenTriangle&) = delete;
			void draw() const;
		private:
			GLuint vertexArray = 0; //the core profile needs one, even without attributes
		};

		/**
		* Saves the OpenGL state that the passes of the renderer change and restores it when it goes out of scope
		*/
		class StateGuard
		{
		public:
			StateGuard();
			~StateGuard();
			StateGuard(const StateGuard&) = delete;
			StateGuard& operator=(const StateGuard&) = delete;
		private:
			GLint framebuffer = 0, program = 0, viewport[4] = {}, vertexArray = 0, activeTexture = 0;
			GLboolean depthTest = GL_FALSE, blend = GL_FALSE, cullFace = GL_FALSE, depthMask = GL_TRUE;
		};
	}
}

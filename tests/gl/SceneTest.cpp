#include <vector>

#include <GL/glew.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>

#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;
using bibo::Shader;
using bibo::physics::PhysicsWorld;
using bibo::scene::Scene;

namespace
{
	/**
	* Offscreen framebuffer (the window is hidden, its pixels are not defined)
	*/
	class Framebuffer
	{
	public:
		Framebuffer(int width, int height) : width(width), height(height)
		{
			glGenFramebuffers(1, &framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
			glGenRenderbuffers(2, renderbuffers);
			glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffers[0]);
			glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[1]);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderbuffers[1]);
			REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			glViewport(0, 0, width, height);
		}

		~Framebuffer()
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDeleteRenderbuffers(2, renderbuffers);
			glDeleteFramebuffers(1, &framebuffer);
		}

		void clear()
		{
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}

		std::vector<unsigned char> pixels()
		{
			std::vector<unsigned char> rgba(width * height * 4);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
			return rgba;
		}

	private:
		int width, height;
		GLuint framebuffer = 0;
		GLuint renderbuffers[2] = {};
	};
}

TEST_CASE("The bear waits at the start of the race without losing", "[gl][scene]")
{
	GlContext gl;
	AssetWorkingDirectory assets; //the scene loads its files relative to the working directory
	Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
	PhysicsWorld physicsWorld;
	Scene scene(&gl.window(), &mainShader, &physicsWorld);

	//5 seconds without input
	for (int i = 0; i < 5 * 60; ++i)
	{
		scene.update(1.0f / 60.0f);
	}

	CHECK_FALSE(scene.isGameOver());
	CHECK_FALSE(scene.isGameWon());

	SECTION("A frame is rendered without OpenGL errors")
	{
		Shader depthShader("Shader/depth_shader.vert", "Shader/depth_shader.frag");
		Shader lightShader("Shader/light_source_shader.vert", "Shader/light_source_shader.frag");

		//The passes of Main.cpp draw
		depthShader.enable();
		scene.renderFromLightSource(&depthShader);
		mainShader.enable();
		scene.renderFromCamera(&mainShader);
		lightShader.enable();
		scene.renderLightSourcesOnly(&lightShader);

		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
	}

	SECTION("Graphics options can be switched off")
	{
		scene.setShadowMappingEnabled(false);
		scene.setNormalMappingEnabled(false);
		scene.setTransparencyEnabled(false);
		scene.setViewFrustumCullingEnabled(false);
		CHECK_FALSE(scene.isShadowMappingEnabled());
		CHECK_FALSE(scene.isNormalMappingEnabled());
		CHECK_FALSE(scene.isTransparencyEnabled());
		CHECK_FALSE(scene.isViewFrustumCullingEnabled());

		mainShader.enable();
		scene.renderFromCamera(&mainShader);
		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
	}

	SECTION("View frustum culling only skips geometries that would not cover any pixel")
	{
		//Every geometry is drawn in white without depth test: the image shows all covered pixels.
		//(A shaded image is not exactly reproducible: where surfaces fight for the same depth, the driver may decide differently in two frames.)
		Shader silhouetteShader(assetPath("tests/gl/silhouette.vert").string(), assetPath("tests/gl/silhouette.frag").string());
		Framebuffer framebuffer(800, 600); //size of the window, the camera uses its aspect ratio
		glDisable(GL_DEPTH_TEST);
		silhouetteShader.enable();
		REQUIRE(scene.isViewFrustumCullingEnabled()); //default

		framebuffer.clear();
		scene.renderFromCamera(&silhouetteShader);
		const int drawnWithCulling = scene.getDrawnGeometryCount();
		const std::vector<unsigned char> withCulling = framebuffer.pixels();

		scene.setViewFrustumCullingEnabled(false);
		framebuffer.clear();
		scene.renderFromCamera(&silhouetteShader);
		const int drawnWithoutCulling = scene.getDrawnGeometryCount();
		const std::vector<unsigned char> withoutCulling = framebuffer.pixels();

		CAPTURE(drawnWithCulling, drawnWithoutCulling, scene.getGeometryCount());
		CHECK(drawnWithoutCulling == scene.getGeometryCount());
		CHECK(drawnWithCulling > 0);
		CHECK(drawnWithCulling < drawnWithoutCulling);
		REQUIRE(withCulling.size() == withoutCulling.size());
		int differentPixels = 0;
		for (std::size_t i = 0; i < withCulling.size(); i += 4)
		{
			if (withCulling[i] != withoutCulling[i] && ++differentPixels <= 10)
			{
				WARN("Pixel " << (i / 4) % 800 << ", " << (i / 4) / 800 << " is only covered without culling");
			}
		}
		CHECK(differentPixels == 0);
		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
	}
}

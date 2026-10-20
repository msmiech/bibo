#include <vector>

#include <GL/glew.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/constants.hpp>
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
		//Like Main.cpp draw: shadow maps, the scene in HDR, bloom, exposure and tone mapping into the framebuffer
		bibo::render::Renderer renderer(800, 600);
		Framebuffer framebuffer(800, 600);
		framebuffer.clear();
		renderer.render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());

		//A bright day: the image is neither black nor white
		std::vector<unsigned char> pixels = framebuffer.pixels();
		double sum = 0.0;
		for (std::size_t i = 0; i < pixels.size(); i += 4)
		{
			sum += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3.0;
		}
		double average = sum / static_cast<double>(pixels.size() / 4);
		CAPTURE(average);
		CHECK(average > 40.0);
		CHECK(average < 230.0);
	}

	SECTION("Graphics options can be switched off")
	{
		bibo::render::RenderSettings settings;
		settings.hdr = false;
		settings.bloom = false;
		settings.shadows = false;
		settings.normalMapping = false;
		settings.transparency = false;
		scene.setViewFrustumCullingEnabled(false);
		CHECK_FALSE(scene.isViewFrustumCullingEnabled());
		bibo::render::Renderer renderer(800, 600);
		Framebuffer framebuffer(800, 600);
		renderer.render(scene, settings, 1.0f / 60.0f);
		settings.wireframe = true;
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		renderer.render(scene, settings, 1.0f / 60.0f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
		//Without culling the whole world is drawn
		bibo::world::World::Statistics statistics = scene.getWorld().getStatistics();
		CHECK(statistics.drawnChunks == statistics.meshes);
	}

	SECTION("The world is only drawn where the camera looks")
	{
		bibo::render::Renderer renderer(800, 600);
		Framebuffer framebuffer(800, 600);
		renderer.render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
		bibo::world::World::Statistics statistics = scene.getWorld().getStatistics();
		CAPTURE(statistics.drawnChunks, statistics.meshes, statistics.drawnInstances, statistics.instances);
		CHECK(statistics.drawnChunks > 0);
		CHECK(statistics.drawnChunks < statistics.meshes);
		CHECK(statistics.drawnInstances > 0);
		CHECK(statistics.drawnInstances < static_cast<int>(statistics.instances));
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
		//Looking back over the start: the stone ring far ahead on the track is behind the camera
		scene.getCamera().rotate(glm::pi<float>(), 0.0f);

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

TEST_CASE("The camera turns around the bear on the track without going into the terrain", "[gl][scene][camera]")
{
	GlContext gl;
	AssetWorkingDirectory assets;
	Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
	PhysicsWorld physicsWorld;
	Scene scene(&gl.window(), &mainShader, &physicsWorld);
	const float frame = 1.0f / 60.0f;
	scene.update(frame);

	bibo::input::PlayerInput input;
	SECTION("Standing at the start") {}
	SECTION("Running along the track")
	{
		input.move = glm::vec2(0.0f, 1.0f);
	}
	SECTION("Jumping")
	{
		input.move = glm::vec2(0.2f, 1.0f);
		input.jump = true;
	}

	int checked = 0;
	for (int i = 0; i < 4 * 60 && !scene.isGameOver(); ++i)
	{
		//Round and round, low (looking up from the ground) and high
		input.look = glm::vec2(0.04f, (i / 60) % 2 == 0 ? -0.05f : 0.05f);
		if (input.jump)
		{
			input.jump = (i / 20) % 2 == 0; //presses again and again
		}
		scene.update(frame, input);
		bibo::scene::Camera& camera = scene.getCamera();
		glm::vec3 position = camera.getPosition();
		bibo::physics::RayHit hit;
		//Nothing static (terrain, rocks) above the camera, so it is not below the surface
		if (physicsWorld.castRay(position + glm::vec3(0.0f, 100.0f, 0.0f), glm::vec3(0.0f, -100.0f, 0.0f), hit, true))
		{
			CAPTURE(i, position, hit.position, camera.getPitch());
			CHECK(hit.position.y < position.y);
		}
		//And not in the middle of something
		CHECK_FALSE(physicsWorld.castSphere(position, glm::vec3(0.0f, 0.01f, 0.0f), 0.1f, hit, true));
		++checked;
	}
	CHECK(checked > 60);
}

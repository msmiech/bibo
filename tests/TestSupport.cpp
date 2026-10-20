#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <catch2/catch_test_macros.hpp>

#include "Resources/Model.hpp"
#include "Resources/SkinnedModel.hpp"
#include "Util/Util.hpp"
#include "World/TrackTerrain.hpp"
#include "World/WorldGenerator.hpp"

namespace bibo::test
{
	std::filesystem::path assetPath(const std::string& relativePath)
	{
		return std::filesystem::path(BIBO_ASSET_DIR) / relativePath;
	}

	AssetWorkingDirectory::AssetWorkingDirectory() : previous(std::filesystem::current_path())
	{
		std::filesystem::current_path(BIBO_ASSET_DIR);
	}

	AssetWorkingDirectory::~AssetWorkingDirectory()
	{
		std::filesystem::current_path(previous);
	}

	const std::vector<ShaderProgram>& gameShaderPrograms()
	{
		static const std::vector<ShaderProgram> programs = {
			{ "Shader/depth_shader.vert", "Shader/depth_shader.frag" },
			{ "Shader/main_shader.vert", "Shader/main_shader.frag" },
			{ "Shader/text_shader.vert", "Shader/text_shader.frag" },
			{ "Shader/light_source_shader.vert", "Shader/light_source_shader.frag" },
			{ "Shader/bear_skin.vert", "Shader/bear_skin.frag" },
			{ "Shader/bear_fur.vert", "Shader/bear_fur.frag" },
			//The world (BIBO/World)
			{ "Shader/fullscreen.vert", "Shader/material_generator.frag" },
			{ "Shader/fullscreen.vert", "Shader/material_normals.frag" },
			{ "Shader/fullscreen.vert", "Shader/sky_environment.frag" },
			{ "Shader/sky.vert", "Shader/sky.frag" },
			{ "Shader/terrain.vert", "Shader/terrain.frag" },
			{ "Shader/vegetation.vert", "Shader/vegetation.frag" },
			{ "Shader/vegetation_depth.vert", "Shader/depth_shader.frag" },
			{ "Shader/water.vert", "Shader/water.frag" },
			{ "Shader/snow.vert", "Shader/snow.frag" },
			//Post processing (BIBO/Render/Renderer.cpp)
			{ "Shader/fullscreen.vert", "Shader/bright_pass.frag" },
			{ "Shader/fullscreen.vert", "Shader/gaussian_blur_shader.frag" },
			{ "Shader/fullscreen.vert", "Shader/luminance.frag" },
			{ "Shader/fullscreen.vert", "Shader/exposure.frag" },
			{ "Shader/fullscreen.vert", "Shader/post_processing_shader.frag" } };
		return programs;
	}

	const std::vector<std::string>& gameNodeNames()
	{
		static const std::vector<std::string> names = {
			"BearBody",
			"Boden", "FinishLine", "Torus", "Water", "Cube",
			"Rock1", "Rock2", "Rock3", "Rock4", "Rock5", "Rock6", "Rock7", "Rock8", "Rock9", "Rock10", "Rock11", "Rock12",
			"Pfeil0", "Pfeil1", "Pfeil2", "Pfeil3" };
		return names;
	}

	const std::vector<std::string>& normalMappedNodeNames()
	{
		static const std::vector<std::string> names = { "Cube" };
		return names;
	}

	const aiScene& trackScene()
	{
		static Assimp::Importer importer;
		//Same post processing as Model
		static const aiScene* scene = importer.ReadFile(assetPath(TRACK_FILE).string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
		if (!scene)
		{
			FAIL("Could not load " << TRACK_FILE << ": " << importer.GetErrorString());
		}
		return *scene;
	}

	const resources::SkinnedModel& bearModel()
	{
		static std::string error;
		static std::unique_ptr<resources::SkinnedModel> model = resources::SkinnedModel::load(assetPath(BEAR_FILE).string(), error);
		if (!model)
		{
			FAIL("Could not load " << BEAR_FILE << ": " << error);
		}
		return *model;
	}

	const world::WorldGenerator& worldGenerator()
	{
		static std::unique_ptr<world::WorldGenerator> generator = [] {
			std::vector<glm::vec3> vertices;
			std::vector<unsigned int> indices;
			REQUIRE(world::TrackTerrain::loadNodeMesh(trackScene(), "Boden", vertices, indices));
			world::WorldGenerator::Description description;
			description.track = std::make_unique<world::TrackTerrain>(vertices, indices);
			glm::vec3 start = glm::vec3(nodeModelMatrix(trackNode("BearBody"))[3]);
			description.start = glm::vec2(start.x, start.z);
			description.startDirection = glm::vec2(0.0f, -1.0f);
			description.checkpoints.assign(std::begin(scene::rules::CHECKPOINTS), std::end(scene::rules::CHECKPOINTS));
			glm::vec3 water;
			float waterRadius;
			REQUIRE(world::TrackTerrain::nodeSphere(trackScene(), "Water", water, waterRadius));
			description.seaLevel = water.y;
			std::vector<std::string> keep = { "FinishLine", "Cube", "Torus", "Pfeil0", "Pfeil1", "Pfeil2", "Pfeil3" };
			for (int i = 1; i <= 12; ++i)
			{
				keep.push_back("Rock" + std::to_string(i));
			}
			for (const std::string& name : keep)
			{
				world::KeepClear object;
				REQUIRE(world::TrackTerrain::nodeSphere(trackScene(), name.c_str(), object.center, object.radius));
				description.keepClear.push_back(object);
			}
			return std::make_unique<world::WorldGenerator>(std::move(description));
		}();
		return *generator;
	}

	aiNode& trackNode(const std::string& name)
	{
		aiNode* node = trackScene().mRootNode->FindNode(name.c_str());
		if (!node)
		{
			FAIL("The track has no node " << name);
		}
		return *node;
	}

	glm::mat4 nodeModelMatrix(aiNode& node)
	{
		aiMatrix4x4 transformation = node.mTransformation;
		resources::Model::transformNode(&transformation, &node);
		return util::ConvertMatrix(transformation);
	}

	scene::rules::FinishLine trackFinishLine()
	{
		aiNode& node = trackNode("FinishLine");
		REQUIRE(node.mNumMeshes > 0);
		const aiMesh* mesh = trackScene().mMeshes[node.mMeshes[0]];
		float halfLength = 0.0f;
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
		{
			halfLength = std::max(halfLength, std::abs(mesh->mVertices[i].x));
		}
		return scene::rules::finishLine(nodeModelMatrix(node), halfLength);
	}

	Clipped clip(const glm::mat4& viewProjection, glm::vec3 point)
	{
		glm::dvec4 clipped = glm::dmat4(viewProjection) * glm::dvec4(point, 1.0);
		if (clipped.w <= 0.0)
		{
			return Clipped::OUTSIDE; //behind the camera
		}
		//Normalized device coordinates: the visible volume is -1 to 1 on all axes
		glm::dvec3 ndc = glm::abs(glm::dvec3(clipped) / clipped.w);
		double largest = std::max({ ndc.x, ndc.y, ndc.z });
		if (std::abs(largest - 1.0) < 1e-4)
		{
			return Clipped::BORDER;
		}
		return largest < 1.0 ? Clipped::INSIDE : Clipped::OUTSIDE;
	}

	namespace
	{
		std::string lastGlfwError;

		void storeGlfwError(int code, const char* description)
		{
			lastGlfwError = description;
		}

		bool initGlfw()
		{
			static const bool initialized = [] {
				glfwSetErrorCallback(storeGlfwError);
				if (!glfwInit())
				{
					return false;
				}
				std::atexit(glfwTerminate);
				return true;
			}();
			return initialized;
		}

		void GLAPIENTRY collectErrors(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
		{
			if (type == GL_DEBUG_TYPE_ERROR)
			{
				auto errors = static_cast<std::vector<std::string>*>(const_cast<void*>(userParam));
				errors->push_back(message);
			}
		}
	}

	GlContext::GlContext(int width, int height)
	{
		if (!initGlfw())
		{
			SKIP("GLFW could not be initialized (no display?): " << lastGlfwError);
		}

		//Same context as the game (Main.cpp), but invisible
		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
		glfwWindow = glfwCreateWindow(width, height, "BIBO tests", nullptr, nullptr);
		if (!glfwWindow)
		{
			SKIP("No OpenGL 4.3 core context: " << lastGlfwError);
		}
		glfwMakeContextCurrent(glfwWindow);

		glewExperimental = GL_TRUE;
		GLenum glewStatus = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
		//On Wayland there is no GLX display, but the OpenGL functions were loaded nevertheless
		if (glewStatus == GLEW_ERROR_NO_GLX_DISPLAY)
		{
			glewStatus = GLEW_OK;
		}
#endif
		if (glewStatus != GLEW_OK)
		{
			glfwDestroyWindow(glfwWindow);
			FAIL("Could not load the OpenGL functions: " << glewGetErrorString(glewStatus));
		}
		glGetError(); //glewInit can leave an error behind

		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(collectErrors, &reportedErrors);
	}

	GlContext::~GlContext()
	{
		glDebugMessageCallback(nullptr, nullptr);
		glfwMakeContextCurrent(nullptr);
		glfwDestroyWindow(glfwWindow);
	}
}

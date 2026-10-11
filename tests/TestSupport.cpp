#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <catch2/catch_test_macros.hpp>

#include "Resources/Model.hpp"
#include "Util/Util.hpp"

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
			{ "Shader/gaussian_blur_shader.vert", "Shader/gaussian_blur_shader.frag" },
			{ "Shader/post_processing_shader.vert", "Shader/post_processing_shader.frag" } };
		return programs;
	}

	const std::vector<std::string>& gameNodeNames()
	{
		static const std::vector<std::string> names = {
			"BearBody", "LeftHand", "RightHand", "LeftLeg", "RightLeg",
			"Boden", "FinishLine", "Torus", "Water", "Cube", "Monitor1",
			"Rock1", "Rock2", "Rock3", "Rock4", "Rock5", "Rock6", "Rock7", "Rock8", "Rock9", "Rock10", "Rock11", "Rock12",
			"Pfeil0", "Pfeil1", "Pfeil2", "Pfeil3" };
		return names;
	}

	const std::vector<std::string>& normalMappedNodeNames()
	{
		static const std::vector<std::string> names = {
			"Cube", "Rock1", "Rock2", "Rock3", "Rock4", "Rock5", "Rock6", "Rock7", "Rock8", "Rock9", "Rock10", "Rock11", "Rock12" };
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

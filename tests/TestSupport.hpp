#pragma once
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/ext/matrix_relational.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/gtx/string_cast.hpp>

#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>

struct GLFWwindow;
struct aiNode;
struct aiScene;

namespace bibo::test
{
	/**
	* @return Absolute path of a game asset in the source tree, e.g. assetPath("Model/SnowTerrain/track1.dae")
	*/
	std::filesystem::path assetPath(const std::string& relativePath);

	/**
	* The game loads its assets relative to the working directory: changes into the asset folder and back again
	*/
	class AssetWorkingDirectory
	{
	public:
		AssetWorkingDirectory();
		~AssetWorkingDirectory();
		AssetWorkingDirectory(const AssetWorkingDirectory&) = delete;
		AssetWorkingDirectory& operator=(const AssetWorkingDirectory&) = delete;
	private:
		std::filesystem::path previous;
	};

	struct ShaderProgram
	{
		std::string vertexShader;
		std::string fragmentShader;
	};

	/**
	* Shader programs of the game (Main.cpp initShaders)
	*/
	const std::vector<ShaderProgram>& gameShaderPrograms();

	const char* const TRACK_FILE = "Model/SnowTerrain/track1.dae";

	/**
	* Nodes of the track file that the game loads (Scene and Bear)
	*/
	const std::vector<std::string>& gameNodeNames();

	/**
	* Nodes that the game renders with a normal map (Scene)
	*/
	const std::vector<std::string>& normalMappedNodeNames();

	/**
	* The track file loaded with Assimp like the game does (Model), only loaded once per test run
	*/
	const aiScene& trackScene();

	/**
	* @return The node with this name in the track, fails the test if there is none
	*/
	aiNode& trackNode(const std::string& name);

	/**
	* Model matrix of a node like the game computes it (Geometry, Model): the global transformation of the node
	*/
	glm::mat4 nodeModelMatrix(aiNode& node);

	enum class Clipped { INSIDE, OUTSIDE, BORDER };

	/**
	* Reference for view frustum culling: is the point inside of the volume the view and projection matrix map onto the screen?
	* @return BORDER if the point is so close to the border of the volume that rounding errors decide
	*/
	Clipped clip(const glm::mat4& viewProjection, glm::vec3 point);

	/**
	* Hidden window with an OpenGL 4.3 core context, like the window of the game.
	* Skips the test if no context can be created (no display or OpenGL 4.3 not supported, e.g. on CI servers without GPU).
	* Errors reported by the driver (debug output) are collected and can be checked with errors().
	*/
	class GlContext
	{
	public:
		GlContext(int width = 800, int height = 600);
		~GlContext();
		GlContext(const GlContext&) = delete;
		GlContext& operator=(const GlContext&) = delete;

		GLFWwindow& window() const { return *glfwWindow; }
		/**
		* Errors reported by the OpenGL driver since the context was created
		*/
		const std::vector<std::string>& errors() const { return reportedErrors; }

	private:
		GLFWwindow* glfwWindow = nullptr;
		std::vector<std::string> reportedErrors;
	};

	/**
	* Matches GLM vectors and matrices whose components differ by at most margin
	*/
	template <typename T>
	class IsNearMatcher : public Catch::Matchers::MatcherBase<T>
	{
	public:
		IsNearMatcher(T expected, float margin) : expected(expected), margin(margin) {}

		bool match(const T& actual) const override
		{
			return glm::all(glm::equal(actual, expected, margin));
		}

		std::string describe() const override
		{
			std::ostringstream description;
			description << "is within " << margin << " of " << glm::to_string(expected);
			return description.str();
		}

	private:
		T expected;
		float margin;
	};

	template <typename T>
	IsNearMatcher<T> IsNear(T expected, float margin)
	{
		return IsNearMatcher<T>(expected, margin);
	}
}

// Readable GLM values in the messages of failed assertions
namespace Catch
{
	template <glm::length_t L, typename T, glm::qualifier Q>
	struct StringMaker<glm::vec<L, T, Q>>
	{
		static std::string convert(const glm::vec<L, T, Q>& value) { return glm::to_string(value); }
	};

	template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
	struct StringMaker<glm::mat<C, R, T, Q>>
	{
		static std::string convert(const glm::mat<C, R, T, Q>& value) { return glm::to_string(value); }
	};
}

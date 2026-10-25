#include <algorithm>
#include <fstream>
#include <iterator>

#include <GL/glew.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>

#include "Render/FrameUniforms.hpp"
#include "Resources/Shader.hpp"
#include "TestSupport.hpp"

using namespace bibo::test;

namespace
{
	std::string infoLog(GLuint object, bool isProgram)
	{
		GLint length = 0;
		isProgram ? glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length) : glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
		std::string log(std::max(length, 1), '\0');
		isProgram ? glGetProgramInfoLog(object, length, nullptr, log.data()) : glGetShaderInfoLog(object, length, nullptr, log.data());
		return log;
	}

	/**
	* Compiles like Shader::loadShader, but reports the error instead of exiting
	*/
	GLuint compile(const std::string& file, GLenum type, const std::string& defines = "")
	{
		//With the files it includes, like Shader::loadShader
		std::string error;
		std::string code = bibo::Shader::addDefines(bibo::Shader::preprocess(assetPath(file).string(), error), defines);
		REQUIRE(error.empty());
		const char* codePtr = code.c_str();

		GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &codePtr, nullptr);
		glCompileShader(shader);
		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled)
		{
			FAIL("Could not compile " << file << ":\n" << infoLog(shader, false));
		}
		return shader;
	}
}

TEST_CASE("Every shader program of the game compiles and links", "[gl][shader]")
{
	GlContext gl;
	const ShaderProgram program = GENERATE(from_range(gameShaderPrograms()));
	CAPTURE(program.vertexShader, program.fragmentShader, program.defines);

	GLuint vertexShader = compile(program.vertexShader, GL_VERTEX_SHADER, program.defines);
	GLuint fragmentShader = compile(program.fragmentShader, GL_FRAGMENT_SHADER, program.defines);
	GLuint handle = glCreateProgram();
	glAttachShader(handle, vertexShader);
	glAttachShader(handle, fragmentShader);
	glLinkProgram(handle);

	GLint linked = GL_FALSE;
	glGetProgramiv(handle, GL_LINK_STATUS, &linked);
	CHECK(linked == GL_TRUE);
	if (!linked)
	{
		FAIL_CHECK("Could not link:\n" << infoLog(handle, true));
	}

	glDeleteProgram(handle);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("The main shader has the inputs the game sets", "[gl][shader]")
{
	GlContext gl;
	bibo::Shader shader(assetPath("Shader/main_shader.vert").string(), assetPath("Shader/main_shader.frag").string());

	SECTION("Matrices (Scene::drawGeometries, Geometry::draw)")
	{
		CHECK(shader.getUniformLocation("model") != -1);
		CHECK(shader.getUniformLocation("view") != -1);
		CHECK(shader.getUniformLocation("projection") != -1);
	}

	SECTION("Material (Mesh::draw, Geometry::draw)")
	{
		CHECK(shader.getUniformLocation("material.diffuse") != -1);
		CHECK(shader.getUniformLocation("material.shininess") != -1);
		CHECK(shader.getUniformLocation("materialLayer") != -1);
	}

	SECTION("Sun, sky and shadows come from the uniform buffer of the frame (render::FrameUniforms)")
	{
		GLuint block = glGetUniformBlockIndex(shader.programHandle, "Frame");
		REQUIRE(block != GL_INVALID_INDEX);
		GLint size = 0;
		glGetActiveUniformBlockiv(shader.programHandle, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
		CHECK(size == static_cast<GLint>(sizeof(bibo::render::FrameUniforms)));
	}

	SECTION("Vertex attributes (Mesh)")
	{
		CHECK(shader.getAttribLocation("position") != -1);
		CHECK(shader.getAttribLocation("normal") != -1);
		CHECK(shader.getAttribLocation("uv") != -1);
	}

	SECTION("Unknown names are not found")
	{
		CHECK(shader.getUniformLocation("doesNotExist") == -1);
	}

	CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("The vegetation and terrain shaders have the inputs their renderers set", "[gl][shader]")
{
	GlContext gl;
	SECTION("The plants sway in the wind and are pushed aside, also in the shadow maps (VegetationRenderer::drawBatches)")
	{
		for (const char* defines : { "", "LOD_DITHER" })
		{
			bibo::Shader shader(assetPath("Shader/vegetation.vert").string(), assetPath("Shader/vegetation.frag").string(), defines);
			CAPTURE(defines);
			CHECK(shader.getUniformLocation("windAmplitude") != -1);
			CHECK(shader.getUniformLocation("fadeDistances") != -1);
			CHECK(shader.getUniformLocation("interaction") != -1);
		}
		bibo::Shader depth(assetPath("Shader/vegetation_depth.vert").string(), assetPath("Shader/depth_shader.frag").string());
		CHECK(depth.getUniformLocation("interaction") != -1);
	}

	SECTION("A chunk fades between its levels of detail (TerrainRenderer::draw)")
	{
		bibo::Shader dithered(assetPath("Shader/terrain.vert").string(), assetPath("Shader/terrain.frag").string(), "LOD_DITHER");
		CHECK(dithered.getUniformLocation("lodFade") != -1);
		bibo::Shader plain(assetPath("Shader/terrain.vert").string(), assetPath("Shader/terrain.frag").string());
		CHECK(plain.getUniformLocation("lodFade") == -1);
	}

	CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

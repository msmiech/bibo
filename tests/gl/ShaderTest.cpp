#include <algorithm>
#include <fstream>
#include <iterator>

#include <GL/glew.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>

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
	GLuint compile(const std::string& file, GLenum type)
	{
		std::ifstream stream(assetPath(file));
		REQUIRE(stream.good());
		std::string code((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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
	CAPTURE(program.vertexShader, program.fragmentShader);

	GLuint vertexShader = compile(program.vertexShader, GL_VERTEX_SHADER);
	GLuint fragmentShader = compile(program.fragmentShader, GL_FRAGMENT_SHADER);
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

	SECTION("Matrices (Scene::draw, Geometry::draw)")
	{
		CHECK(shader.getUniformLocation("model") != -1);
		CHECK(shader.getUniformLocation("view") != -1);
		CHECK(shader.getUniformLocation("projection") != -1);
		CHECK(shader.getUniformLocation("lightSpaceMatrix") != -1);
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

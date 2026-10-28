#include <GL/glew.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <fstream>
#include <iterator>

#include "Render/FrameUniforms.hpp"
#include "Resources/Shader.hpp"
#include "TestSupport.hpp"

using bibo::test::AssetPath;
using bibo::test::GameShaderPrograms;
using bibo::test::GlContext;
using bibo::test::ShaderProgram;

namespace {
std::string InfoLog(GLuint object, bool is_program) {
  GLint length = 0;
  is_program ? glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length)
             : glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
  std::string log(std::max(length, 1), '\0');
  is_program ? glGetProgramInfoLog(object, length, nullptr, log.data())
             : glGetShaderInfoLog(object, length, nullptr, log.data());
  return log;
}

/**
 * Compiles like Shader::LoadShader, but reports the error instead of exiting
 */
GLuint Compile(const std::string& file, GLenum type,
               const std::string& defines = "") {
  // With the files it includes, like Shader::LoadShader
  std::string error;
  std::string code = bibo::Shader::AddDefines(
      bibo::Shader::Preprocess(AssetPath(file).string(), error), defines);
  REQUIRE(error.empty());
  const char* code_ptr = code.c_str();

  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &code_ptr, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    FAIL("Could not compile " << file << ":\n" << InfoLog(shader, false));
  }
  return shader;
}
}  // namespace

TEST_CASE("Every shader program of the game compiles and links",
          "[gl][shader]") {
  GlContext gl;
  const ShaderProgram program = GENERATE(from_range(GameShaderPrograms()));
  CAPTURE(program.vertex_shader, program.fragment_shader, program.defines);

  GLuint vertex_shader =
      Compile(program.vertex_shader, GL_VERTEX_SHADER, program.defines);
  GLuint fragment_shader =
      Compile(program.fragment_shader, GL_FRAGMENT_SHADER, program.defines);
  GLuint handle = glCreateProgram();
  glAttachShader(handle, vertex_shader);
  glAttachShader(handle, fragment_shader);
  glLinkProgram(handle);

  GLint linked = GL_FALSE;
  glGetProgramiv(handle, GL_LINK_STATUS, &linked);
  CHECK(linked == GL_TRUE);
  if (!linked) {
    FAIL_CHECK("Could not link:\n" << InfoLog(handle, true));
  }

  glDeleteProgram(handle);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("The main shader has the inputs the game sets", "[gl][shader]") {
  GlContext gl;
  bibo::Shader shader(AssetPath("Shader/main_shader.vert").string(),
                      AssetPath("Shader/main_shader.frag").string());

  SECTION("Matrices (Scene::drawGeometries, Geometry::draw)") {
    CHECK(shader.GetUniformLocation("model") != -1);
    CHECK(shader.GetUniformLocation("view") != -1);
    CHECK(shader.GetUniformLocation("projection") != -1);
  }

  SECTION("Material (Mesh::draw, Geometry::draw)") {
    CHECK(shader.GetUniformLocation("material.diffuse") != -1);
    CHECK(shader.GetUniformLocation("material.shininess") != -1);
    CHECK(shader.GetUniformLocation("materialLayer") != -1);
  }

  SECTION(
      "Sun, sky and shadows come from the uniform buffer of the frame "
      "(render::FrameUniforms)") {
    GLuint block = glGetUniformBlockIndex(shader.program_handle, "Frame");
    REQUIRE(block != GL_INVALID_INDEX);
    GLint size = 0;
    glGetActiveUniformBlockiv(shader.program_handle, block,
                              GL_UNIFORM_BLOCK_DATA_SIZE, &size);
    CHECK(size == static_cast<GLint>(sizeof(bibo::render::FrameUniforms)));
  }

  SECTION("Vertex attributes (Mesh)") {
    CHECK(shader.GetAttribLocation("position") != -1);
    CHECK(shader.GetAttribLocation("normal") != -1);
    CHECK(shader.GetAttribLocation("uv") != -1);
  }

  SECTION("Unknown names are not found") {
    CHECK(shader.GetUniformLocation("doesNotExist") == -1);
  }

  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE(
    "The vegetation and terrain shaders have the inputs their renderers set",
    "[gl][shader]") {
  GlContext gl;
  SECTION(
      "The plants sway in the wind and are pushed aside, also in the shadow "
      "maps (VegetationRenderer::drawBatches)") {
    for (const char* defines : {"", "LOD_DITHER"}) {
      bibo::Shader shader(AssetPath("Shader/vegetation.vert").string(),
                          AssetPath("Shader/vegetation.frag").string(),
                          defines);
      CAPTURE(defines);
      CHECK(shader.GetUniformLocation("windAmplitude") != -1);
      CHECK(shader.GetUniformLocation("fadeDistances") != -1);
      CHECK(shader.GetUniformLocation("interaction") != -1);
    }
    bibo::Shader depth(AssetPath("Shader/vegetation_depth.vert").string(),
                       AssetPath("Shader/depth_shader.frag").string());
    CHECK(depth.GetUniformLocation("interaction") != -1);
  }

  SECTION(
      "A chunk fades between its levels of detail (TerrainRenderer::draw)") {
    bibo::Shader dithered(AssetPath("Shader/terrain.vert").string(),
                          AssetPath("Shader/terrain.frag").string(),
                          "LOD_DITHER");
    CHECK(dithered.GetUniformLocation("lodFade") != -1);
    bibo::Shader plain(AssetPath("Shader/terrain.vert").string(),
                       AssetPath("Shader/terrain.frag").string());
    CHECK(plain.GetUniformLocation("lodFade") == -1);
  }

  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

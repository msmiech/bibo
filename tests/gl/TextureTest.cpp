#include <GL/glew.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <tuple>
#include <vector>

#include "Resources/Texture.hpp"
#include "TestSupport.hpp"

using bibo::test::AssetPath;
using bibo::test::GlContext;

using bibo::Texture;

namespace {
GLint Parameter(Texture& texture, GLenum name) {
  GLint value = 0;
  glBindTexture(GL_TEXTURE_2D, texture.GetHandle());
  glGetTexParameteriv(GL_TEXTURE_2D, name, &value);
  return value;
}

GLint LevelParameter(Texture& texture, GLint level, GLenum name) {
  GLint value = 0;
  glBindTexture(GL_TEXTURE_2D, texture.GetHandle());
  glGetTexLevelParameteriv(GL_TEXTURE_2D, level, name, &value);
  return value;
}

std::vector<unsigned char> ReadPixels(Texture& texture, int width, int height) {
  std::vector<unsigned char> pixels(width * height * 4);
  glBindTexture(GL_TEXTURE_2D, texture.GetHandle());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  return pixels;
}
}  // namespace

TEST_CASE("Generated textures are uploaded with mipmaps", "[gl][texture]") {
  GlContext gl;
  auto checkerboard = Texture::CreateCheckerboard(64, 8, 0);

  CHECK(LevelParameter(*checkerboard, 0, GL_TEXTURE_WIDTH) == 64);
  CHECK(LevelParameter(*checkerboard, 0, GL_TEXTURE_HEIGHT) == 64);
  CHECK(LevelParameter(*checkerboard, 1, GL_TEXTURE_WIDTH) == 32);  // mipmap
  CHECK(ReadPixels(*checkerboard, 64, 64) ==
        Texture::CheckerboardPixels(64, 8));
  CHECK(checkerboard->repeat);  // the flag is repeated on the finish line

  auto noise = Texture::CreateNoise(32, 7, 0);
  CHECK(ReadPixels(*noise, 32, 32) == Texture::NoisePixels(32, 7));
  CHECK_FALSE(noise->repeat);

  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Image files are loaded with their size", "[gl][texture]") {
  GlContext gl;
  Texture snow(AssetPath("Texture/snow.jpg").string(), 0);

  CHECK(LevelParameter(snow, 0, GL_TEXTURE_WIDTH) > 1);
  CHECK(LevelParameter(snow, 0, GL_TEXTURE_HEIGHT) > 1);
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("A missing image file is replaced by a magenta placeholder",
          "[gl][texture]") {
  GlContext gl;
  Texture missing(AssetPath("Texture/does-not-exist.png").string(), 0);

  REQUIRE(LevelParameter(missing, 0, GL_TEXTURE_WIDTH) == 1);
  REQUIRE(LevelParameter(missing, 0, GL_TEXTURE_HEIGHT) == 1);
  CHECK(ReadPixels(missing, 1, 1) ==
        std::vector<unsigned char>{255, 0, 255, 255});
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Binding a texture applies the filter settings", "[gl][texture]") {
  GlContext gl;
  auto texture = Texture::CreateCheckerboard(16, 2, 0);

  // F-key settings of the game: bilinear filtering on/off and mipmapping
  // off/nearest/linear
  auto [bilinear, mip_mapping_quality, min_filter, mag_filter] =
      GENERATE(table<bool, unsigned int, GLint, GLint>(
          {{false, 0, GL_NEAREST, GL_NEAREST},
           {true, 0, GL_LINEAR, GL_LINEAR},
           {false, 1, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
           {true, 1, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
           {false, 2, GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
           {true, 2, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}}));
  CAPTURE(bilinear, mip_mapping_quality);

  texture->Bind(1, bilinear, mip_mapping_quality);

  GLint active_texture = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
  CHECK(active_texture == GL_TEXTURE1);
  CHECK(Parameter(*texture, GL_TEXTURE_MIN_FILTER) == min_filter);
  CHECK(Parameter(*texture, GL_TEXTURE_MAG_FILTER) == mag_filter);
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Binding a texture applies the wrap mode", "[gl][texture]") {
  GlContext gl;
  auto texture = Texture::CreateCheckerboard(16, 2, 0);
  const bool repeat = GENERATE(true, false);
  texture->repeat = repeat;

  texture->Bind();

  GLint expected = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  CHECK(Parameter(*texture, GL_TEXTURE_WRAP_S) == expected);
  CHECK(Parameter(*texture, GL_TEXTURE_WRAP_T) == expected);
  CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
}

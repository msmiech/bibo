#include <GL/glew.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <tuple>
#include <vector>

#include "Resources/Texture.hpp"
#include "TestSupport.hpp"

using bibo::Texture;
using namespace bibo::test;

namespace {
GLint parameter(Texture& texture, GLenum name) {
  GLint value = 0;
  glBindTexture(GL_TEXTURE_2D, texture.getHandle());
  glGetTexParameteriv(GL_TEXTURE_2D, name, &value);
  return value;
}

GLint levelParameter(Texture& texture, GLint level, GLenum name) {
  GLint value = 0;
  glBindTexture(GL_TEXTURE_2D, texture.getHandle());
  glGetTexLevelParameteriv(GL_TEXTURE_2D, level, name, &value);
  return value;
}

std::vector<unsigned char> readPixels(Texture& texture, int width, int height) {
  std::vector<unsigned char> pixels(width * height * 4);
  glBindTexture(GL_TEXTURE_2D, texture.getHandle());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  return pixels;
}
}  // namespace

TEST_CASE("Generated textures are uploaded with mipmaps", "[gl][texture]") {
  GlContext gl;
  auto checkerboard = Texture::createCheckerboard(64, 8, 0);

  CHECK(levelParameter(*checkerboard, 0, GL_TEXTURE_WIDTH) == 64);
  CHECK(levelParameter(*checkerboard, 0, GL_TEXTURE_HEIGHT) == 64);
  CHECK(levelParameter(*checkerboard, 1, GL_TEXTURE_WIDTH) == 32);  // mipmap
  CHECK(readPixels(*checkerboard, 64, 64) ==
        Texture::checkerboardPixels(64, 8));
  CHECK(checkerboard->repeat);  // the flag is repeated on the finish line

  auto noise = Texture::createNoise(32, 7, 0);
  CHECK(readPixels(*noise, 32, 32) == Texture::noisePixels(32, 7));
  CHECK_FALSE(noise->repeat);

  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Image files are loaded with their size", "[gl][texture]") {
  GlContext gl;
  Texture snow(assetPath("Texture/snow.jpg").string(), 0);

  CHECK(levelParameter(snow, 0, GL_TEXTURE_WIDTH) > 1);
  CHECK(levelParameter(snow, 0, GL_TEXTURE_HEIGHT) > 1);
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("A missing image file is replaced by a magenta placeholder",
          "[gl][texture]") {
  GlContext gl;
  Texture missing(assetPath("Texture/does-not-exist.png").string(), 0);

  REQUIRE(levelParameter(missing, 0, GL_TEXTURE_WIDTH) == 1);
  REQUIRE(levelParameter(missing, 0, GL_TEXTURE_HEIGHT) == 1);
  CHECK(readPixels(missing, 1, 1) ==
        std::vector<unsigned char>{255, 0, 255, 255});
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Binding a texture applies the filter settings", "[gl][texture]") {
  GlContext gl;
  auto texture = Texture::createCheckerboard(16, 2, 0);

  // F-key settings of the game: bilinear filtering on/off and mipmapping
  // off/nearest/linear
  auto [bilinear, mipMappingQuality, minFilter, magFilter] =
      GENERATE(table<bool, unsigned int, GLint, GLint>(
          {{false, 0, GL_NEAREST, GL_NEAREST},
           {true, 0, GL_LINEAR, GL_LINEAR},
           {false, 1, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
           {true, 1, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
           {false, 2, GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
           {true, 2, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}}));
  CAPTURE(bilinear, mipMappingQuality);

  texture->bind(1, bilinear, mipMappingQuality);

  GLint activeTexture = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
  CHECK(activeTexture == GL_TEXTURE1);
  CHECK(parameter(*texture, GL_TEXTURE_MIN_FILTER) == minFilter);
  CHECK(parameter(*texture, GL_TEXTURE_MAG_FILTER) == magFilter);
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Binding a texture applies the wrap mode", "[gl][texture]") {
  GlContext gl;
  auto texture = Texture::createCheckerboard(16, 2, 0);
  const bool repeat = GENERATE(true, false);
  texture->repeat = repeat;

  texture->bind();

  GLint expected = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  CHECK(parameter(*texture, GL_TEXTURE_WRAP_S) == expected);
  CHECK(parameter(*texture, GL_TEXTURE_WRAP_T) == expected);
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

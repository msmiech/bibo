#include <stb_image.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cctype>
#include <fstream>
#include <memory>

#include "TestSupport.hpp"

using bibo::test::AssetPath;
using bibo::test::GameShaderPrograms;
using bibo::test::ShaderProgram;

namespace fs = std::filesystem;

namespace {
struct Image {
  int width = 0, height = 0;
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> rgba{
      nullptr, stbi_image_free};

  // Decoded like the game does it (Texture)
  explicit Image(const fs::path& file) {
    int channels;
    rgba.reset(stbi_load(file.string().c_str(), &width, &height, &channels, 4));
  }

  std::array<double, 3> AverageColor() const {
    std::array<double, 3> sum{};
    for (int i = 0; i < width * height; ++i) {
      for (int c = 0; c < 3; ++c) {
        sum[c] += rgba.get()[i * 4 + c];
      }
    }
    for (double& channel : sum) {
      channel /= width * height;
    }
    return sum;
  }
};

std::vector<std::string> ImageFilesIn(const std::string& folder) {
  std::vector<std::string> files;
  for (const auto& entry :
       fs::recursive_directory_iterator(AssetPath(folder))) {
    std::string extension = entry.path().extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (entry.is_regular_file() &&
        (extension == ".png" || extension == ".jpg" || extension == ".tga")) {
      files.push_back(
          fs::relative(entry.path(), AssetPath("")).generic_string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}
}  // namespace

TEST_CASE("Every texture file can be decoded", "[assets]") {
  const std::string file = GENERATE(from_range(ImageFilesIn("Texture")));
  CAPTURE(file);

  Image image(AssetPath(file));

  REQUIRE(image.rgba != nullptr);
  CHECK(image.width > 0);
  CHECK(image.height > 0);
}

TEST_CASE("All textures of the scene exist", "[assets]") {
  // Scene::Scene, a missing file would be replaced by a magenta placeholder
  // (the materials of the world are generated)
  const std::string file = GENERATE(
      values<std::string>({"Texture/wood16.jpg", "Texture/wood16n.jpg"}));
  CAPTURE(file);

  CHECK(Image(AssetPath(file)).rgba != nullptr);
}

TEST_CASE("The window icon is a square image with a transparent background",
          "[assets]") {
  // display::SetWindowIcon (rendered from Icon/bibo.svg by tools/build_icon.py)
  Image icon(AssetPath("Icon/bibo.png"));
  REQUIRE(icon.rgba != nullptr);
  CHECK(icon.width == icon.height);
  CHECK(icon.width >= 48);

  // The tile has rounded corners
  CHECK(icon.rgba.get()[3] == 0);
  CHECK(icon.rgba.get()[((icon.height / 2) * icon.width + icon.width / 2) * 4 +
                        3] == 255);
}

TEST_CASE("Normal maps contain tangent space normals", "[assets]") {
  // Mostly pointing away from the surface (+Z, stored in the blue channel)
  const std::string file = GENERATE(
      values<std::string>({"Texture/snow-normal.jpg", "Texture/wood16n.jpg",
                           "Texture/ground07n.jpg"}));
  CAPTURE(file);
  Image normal_map(AssetPath(file));
  REQUIRE(normal_map.rgba != nullptr);

  auto [red, green, blue] = normal_map.AverageColor();
  CAPTURE(red, green, blue);
  CHECK(blue > 200.0);
  CHECK(blue > red);
  CHECK(blue > green);
}

TEST_CASE("The shader sources of the game exist and declare a GLSL version",
          "[assets]") {
  const ShaderProgram program = GENERATE(from_range(GameShaderPrograms()));
  const std::string file =
      GENERATE_COPY(program.vertex_shader, program.fragment_shader);
  CAPTURE(file);

  std::ifstream source(AssetPath(file));
  REQUIRE(source.good());
  std::string first_line;
  std::getline(source, first_line);
  // The game needs OpenGL 4.3, newer versions are not supported by every driver
  CHECK_THAT(first_line,
             Catch::Matchers::Matches(R"(#version (330|4[0-3]0)( core)?\s*)"));
}

TEST_CASE("The font of the HUD exists", "[assets]") {
  // Main.cpp InitFreeType
  std::ifstream font(AssetPath("Font/DejaVuSans.ttf"), std::ios::binary);
  REQUIRE(font.good());
  char magic[4] = {};
  font.read(magic, 4);
  CHECK(std::string(magic, 4) == std::string("\0\1\0\0", 4));  // TrueType
}

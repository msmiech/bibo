#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <stb_image.h>

#include "TestSupport.hpp"

using namespace bibo::test;
namespace fs = std::filesystem;

namespace
{
	struct Image
	{
		int width = 0, height = 0;
		std::unique_ptr<unsigned char, decltype(&stbi_image_free)> rgba{ nullptr, stbi_image_free };

		//Decoded like the game does it (Texture)
		explicit Image(const fs::path& file)
		{
			int channels;
			rgba.reset(stbi_load(file.string().c_str(), &width, &height, &channels, 4));
		}

		std::array<double, 3> averageColor() const
		{
			std::array<double, 3> sum{};
			for (int i = 0; i < width * height; ++i)
			{
				for (int c = 0; c < 3; ++c)
				{
					sum[c] += rgba.get()[i * 4 + c];
				}
			}
			for (double& channel : sum)
			{
				channel /= width * height;
			}
			return sum;
		}
	};

	std::vector<std::string> imageFilesIn(const std::string& folder)
	{
		std::vector<std::string> files;
		for (const auto& entry : fs::recursive_directory_iterator(assetPath(folder)))
		{
			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (entry.is_regular_file() && (extension == ".png" || extension == ".jpg" || extension == ".tga"))
			{
				files.push_back(fs::relative(entry.path(), assetPath("")).generic_string());
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}

TEST_CASE("Every texture file can be decoded", "[assets]")
{
	const std::string file = GENERATE(from_range(imageFilesIn("Texture")));
	CAPTURE(file);

	Image image(assetPath(file));

	REQUIRE(image.rgba != nullptr);
	CHECK(image.width > 0);
	CHECK(image.height > 0);
}

TEST_CASE("All textures of the scene exist", "[assets]")
{
	//Scene::Scene, a missing file would be replaced by a magenta placeholder (the materials of the world are generated)
	const std::string file = GENERATE(values<std::string>({ "Texture/wood16.jpg", "Texture/wood16n.jpg" }));
	CAPTURE(file);

	CHECK(Image(assetPath(file)).rgba != nullptr);
}

TEST_CASE("Normal maps contain tangent space normals", "[assets]")
{
	//Mostly pointing away from the surface (+Z, stored in the blue channel)
	const std::string file = GENERATE(values<std::string>({ "Texture/snow-normal.jpg", "Texture/wood16n.jpg", "Texture/ground07n.jpg" }));
	CAPTURE(file);
	Image normalMap(assetPath(file));
	REQUIRE(normalMap.rgba != nullptr);

	auto [red, green, blue] = normalMap.averageColor();
	CAPTURE(red, green, blue);
	CHECK(blue > 200.0);
	CHECK(blue > red);
	CHECK(blue > green);
}

TEST_CASE("The shader sources of the game exist and declare a GLSL version", "[assets]")
{
	const ShaderProgram program = GENERATE(from_range(gameShaderPrograms()));
	const std::string file = GENERATE_COPY(program.vertexShader, program.fragmentShader);
	CAPTURE(file);

	std::ifstream source(assetPath(file));
	REQUIRE(source.good());
	std::string firstLine;
	std::getline(source, firstLine);
	//The game needs OpenGL 4.3, newer versions are not supported by every driver
	CHECK_THAT(firstLine, Catch::Matchers::Matches(R"(#version (330|4[0-3]0)( core)?\s*)"));
}

TEST_CASE("The font of the HUD exists", "[assets]")
{
	//Main.cpp initFreeType
	std::ifstream font(assetPath("Font/DejaVuSans.ttf"), std::ios::binary);
	REQUIRE(font.good());
	char magic[4] = {};
	font.read(magic, 4);
	CHECK(std::string(magic, 4) == std::string("\0\1\0\0", 4)); //TrueType
}

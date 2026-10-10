#include <algorithm>
#include <set>
#include <tuple>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Resources/Texture.hpp"

using bibo::Texture;
using Catch::Matchers::WithinAbs;

namespace
{
	struct Image
	{
		int size;
		std::vector<unsigned char> pixels;

		const unsigned char* pixel(int x, int y) const { return &pixels[(y * size + x) * 4]; }
		int brightness(int x, int y) const { return pixel(x, y)[0]; }

		bool isOpaqueGrey() const
		{
			for (std::size_t i = 0; i < pixels.size(); i += 4)
			{
				if (pixels[i] != pixels[i + 1] || pixels[i] != pixels[i + 2] || pixels[i + 3] != 255)
				{
					return false;
				}
			}
			return true;
		}

		double averageBrightness(int firstRow, int rowStep) const
		{
			double sum = 0.0;
			int count = 0;
			for (int y = firstRow; y < size; y += rowStep)
			{
				for (int x = 0; x < size; ++x)
				{
					sum += brightness(x, y);
					++count;
				}
			}
			return sum / count;
		}
	};
}

TEST_CASE("The checkerboard has alternating light and dark squares", "[texture]")
{
	//256 / 8 is the chequered flag of the finish line (Scene)
	auto [size, squares] = GENERATE(table<int, int>({ { 256, 8 }, { 64, 4 }, { 16, 2 } }));
	CAPTURE(size, squares);
	const Image image{ size, Texture::checkerboardPixels(size, squares) };

	REQUIRE(image.pixels.size() == static_cast<std::size_t>(size * size * 4));
	CHECK(image.isOpaqueGrey());

	const int squareSize = size / squares;
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			bool light = (x / squareSize + y / squareSize) % 2 == 0;
			if (image.brightness(x, y) != (light ? 240 : 15))
			{
				FAIL("Pixel (" << x << ", " << y << ") has brightness " << image.brightness(x, y));
			}
		}
	}

	SECTION("Starts with a light square and changes color at every square border")
	{
		CHECK(image.brightness(0, 0) == 240);
		CHECK(image.brightness(squareSize - 1, 0) == 240);
		CHECK(image.brightness(squareSize, 0) == 15);
		CHECK(image.brightness(0, squareSize) == 15);
		CHECK(image.brightness(squareSize, squareSize) == 240);
	}
}

TEST_CASE("The TV static is random grey noise with darker scan lines", "[texture]")
{
	const int size = 128; //like the monitor (Scene)
	const unsigned int seed = GENERATE(range(1u, 15u));
	CAPTURE(seed);
	const Image image{ size, Texture::noisePixels(size, seed) };

	REQUIRE(image.pixels.size() == static_cast<std::size_t>(size * size * 4));
	CHECK(image.isOpaqueGrey());

	SECTION("The same seed always gives the same frame")
	{
		CHECK(Texture::noisePixels(size, seed) == image.pixels);
	}

	SECTION("Every other row is a darker scan line")
	{
		int brightestScanLinePixel = 0;
		int brightestOtherPixel = 0;
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				int& brightest = y % 2 == 1 ? brightestScanLinePixel : brightestOtherPixel;
				brightest = std::max(brightest, image.brightness(x, y));
			}
		}
		CHECK(brightestScanLinePixel <= 255 * 3 / 4);
		CHECK(brightestOtherPixel > 255 * 3 / 4);

		//Uniformly distributed brightness: the mean of 64 rows of 128 pixels is within a few units of the expected value
		CHECK_THAT(image.averageBrightness(0, 2), WithinAbs(127.5, 5.0));
		CHECK_THAT(image.averageBrightness(1, 2), WithinAbs(127.5 * 3.0 / 4.0, 5.0));
	}
}

TEST_CASE("All frames of the TV static animation are different", "[texture]")
{
	//Scene::initAnimatedTextures uses the seeds 1 to 14
	std::set<std::vector<unsigned char>> frames;
	for (unsigned int seed = 1; seed <= 14; ++seed)
	{
		frames.insert(Texture::noisePixels(128, seed));
	}
	CHECK(frames.size() == 14);
}

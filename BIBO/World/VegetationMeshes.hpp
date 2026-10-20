#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Vegetation.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* How a part of a model is shaded (the vegetation shader uses the same numbers)
		*/
		enum VegetationMaterial : std::uint8_t
		{
			VEGETATION_PLAIN, //vertex color
			VEGETATION_BARK, //bark texture along the uv (u around, v along)
			VEGETATION_NEEDLES, //needle texture, translucent
			VEGETATION_LEAVES, //leaf texture, translucent
			VEGETATION_STONE, //rock texture projected in model space, moss and snow on top
			VEGETATION_CAP_SPOTTED, //mushroom cap with white spots
			VEGETATION_STEM, //mushroom stem
			VEGETATION_GLOW, //glowing mushroom
			VEGETATION_WOOD, //cut wood with year rings (uv: center at 0.5, 0.5)
			VEGETATION_GRASS, //blades of grass, translucent
			VEGETATION_GLOSSY //wet and shiny: eyes, noses, the scales of a fish
		};

		/**
		* Vertex of a vegetation model (40 bytes)
		*/
		struct VegetationVertex
		{
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec2 uv;
			std::uint8_t color[4]; //sRGB color (rgb), a: unused
			std::uint8_t parameters[4]; //material, wind weight (how much it sways), snow (how much it can hold), ambient occlusion
		};

		struct VegetationMesh
		{
			std::vector<VegetationVertex> vertices;
			std::vector<std::uint32_t> indices;
			float radius = 0.0f; //of the bounding sphere around the origin
		};

		/**
		* The levels of detail of a model (1 to 3, the first is the most detailed). Generated, deterministic.
		*/
		std::vector<VegetationMesh> buildModel(Model model);
	}
}

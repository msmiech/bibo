#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace bibo
{
	namespace scene
	{
		//Where the feet of the hedgehog are under its body (model space, front left; the others mirrored)
		constexpr float HEDGEHOG_FOOT_SIDE = 0.16f;
		constexpr float HEDGEHOG_FOOT_FRONT = 0.13f;
		constexpr float HEDGEHOG_FOOT_BACK = -0.2f;
		//Center and radius of the round body (without the spines), for the collisions
		constexpr float HEDGEHOG_BODY_HEIGHT = 0.25f;
		constexpr float HEDGEHOG_BODY_RADIUS = 0.33f;

		/**
		* The hedgehog for the renderer of the bear (the same skin shading, eyes and fur strands): a round body with a
		* pointed snout, spines on the back and the sides, short fur on the face and the belly, small dark eyes, a wet nose,
		* round ears and four feet with claws. Model space: +Z forward, +Y up, the origin on the ground under the body,
		* about 1 long (scaled up in the world, Hedgehog::SCALE). Generated, the same every time.
		*/
		struct HedgehogModel
		{
			enum Part { BODY = 0, FOOT_FRONT_LEFT = 1, FOOT_FRONT_RIGHT, FOOT_HIND_LEFT, FOOT_HIND_RIGHT };

			//Per vertex. The feet are in the space of their foot (origin on the ground under it), the rest in the space of the body.
			std::vector<glm::vec3> positions;
			std::vector<glm::vec3> normals;
			std::vector<glm::vec4> colors; //like SkinnedModel: linear color, fur length (0 to 1); for the eyes the direction in eye space
			std::vector<float> surfaces; //SkinnedModel::Surface
			std::vector<int> parts; //Part
			std::vector<glm::vec3> combs; //direction of the fur and the spines (model space)
			std::vector<bool> spines; //the strands of this vertex are spines (else fur)
			std::vector<unsigned int> indices; //3 per triangle
			std::vector<unsigned int> spineTriangles; //where the spines grow (3 indices per triangle)
			std::vector<unsigned int> furTriangles; //where the fur grows

			static HedgehogModel build();
		};
	}
}

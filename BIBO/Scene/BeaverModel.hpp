#pragma once
#include <glm/glm.hpp>
#include "FurryModel.hpp"

namespace bibo
{
	namespace scene
	{
		//Joints of the parts of the beaver (model space of the body, the jaw: of the head)
		constexpr glm::vec3 BEAVER_NECK(0.0f, 0.74f, 0.04f);
		constexpr glm::vec3 BEAVER_JAW(0.0f, 0.02f, 0.13f);
		constexpr glm::vec3 BEAVER_TAIL(0.0f, 0.1f, -0.2f);
		constexpr glm::vec3 BEAVER_SHOULDER(0.16f, 0.6f, 0.13f); //left, the right one mirrored
		constexpr glm::vec3 BEAVER_FOOT(0.15f, 0.0f, 0.2f); //left hind foot on the ground
		//Height of the top of the head when it sits upright
		constexpr float BEAVER_HEIGHT = 1.05f;

		/**
		* The beaver for the renderer of the bear (FurryModel), sitting upright on its hind legs: a pear shaped body with thick
		* brown fur, a round head with a blunt muzzle, big orange front teeth, a black nose, small dark eyes and round ears,
		* a lower jaw that opens when it talks, short arms with clawed hands in front of the chest, large webbed hind feet and
		* the flat, scaly tail lying on the ground behind it.
		* Every part is in its own space with the origin at its joint (see the constants above). Model space: +Z forward, +Y up,
		* +X left, the origin on the ground under the body, about 1 high (scaled up in the world, Beaver::SCALE).
		* Generated, the same every time.
		*/
		struct BeaverModel : FurryModel
		{
			enum Part { BODY = 0, HEAD, JAW, TAIL, ARM_LEFT, ARM_RIGHT, FOOT_LEFT, FOOT_RIGHT, PART_COUNT };

			static BeaverModel build();
		};
	}
}

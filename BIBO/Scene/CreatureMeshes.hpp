#pragma once
#include "../World/VegetationMeshes.hpp"

namespace bibo
{
	namespace scene
	{
		/**
		* The salmon and the splashes of water as parts that move separately, and the glowing exclamation mark above the beaver
		* that offers a quest (drawn by the CreatureRenderer, the hedgehogs and the beaver are drawn like the bear, see FurryModel).
		* In model space +Z is forward and +Y up, the origin is in the middle of the fish, which is 1 long.
		*/
		enum class CreatureModel
		{
			SALMON_HEAD, //from the gills to the snout (eaten first)
			SALMON_BODY,
			SALMON_TAIL, //origin at the joint to the body (z = SALMON_TAIL_JOINT of the whole fish), it wiggles
			DROPLET, //splash of water, radius 1
			QUEST_MARKER, //glowing golden exclamation mark, 1 high, the origin at its bottom
			QUEST_MARKER_REPEATABLE, //the same in blue: a quest that was completed and can be done again
			COUNT
		};
		constexpr int CREATURE_MODEL_COUNT = static_cast<int>(CreatureModel::COUNT);

		//Where the parts of the salmon meet (along its length, -0.5 tail to 0.5 snout)
		constexpr float SALMON_HEAD_JOINT = 0.2f;
		constexpr float SALMON_TAIL_JOINT = -0.15f;

		/**
		* Generated, the same every time
		*/
		world::VegetationMesh buildCreatureModel(CreatureModel model);
	}
}

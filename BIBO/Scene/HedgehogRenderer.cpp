#include "HedgehogRenderer.hpp"
#include <vector>

using namespace bibo;
using namespace bibo::scene;

namespace
{
	FurryRenderer::Strands hedgehogStrands()
	{
		return { .furCount = HedgehogRenderer::FUR_COUNT, .spineCount = HedgehogRenderer::SPINE_COUNT, .spineLength = HedgehogRenderer::SPINE_LENGTH,
			.spineWidth = HedgehogRenderer::SPINE_WIDTH, .furSeed = 78, .spineSeed = 77 };
	}

	FurryRenderer::Bounds hedgehogBounds()
	{
		return { .center = glm::vec3(0.0f, HEDGEHOG_BODY_HEIGHT, 0.0f), .radius = 0.75f };
	}
}

HedgehogRenderer::HedgehogRenderer(std::size_t count) : FurryRenderer(HedgehogModel::build(), count, hedgehogStrands(), hedgehogBounds())
{
}

void HedgehogRenderer::setPose(std::size_t index, const glm::mat4& body, const std::array<glm::mat4, 4>& feet, float bristle)
{
	std::vector<glm::mat4> parts = { body, feet[0], feet[1], feet[2], feet[3] };
	FurryRenderer::setPose(index, parts, bristle);
}

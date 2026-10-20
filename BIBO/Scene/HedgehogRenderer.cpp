#include "HedgehogRenderer.hpp"
#include <vector>

using namespace bibo;
using namespace bibo::scene;

namespace
{
	FurryRenderer::Strands hedgehogStrands()
	{
		FurryRenderer::Strands strands;
		strands.furCount = HedgehogRenderer::FUR_COUNT;
		strands.spineCount = HedgehogRenderer::SPINE_COUNT;
		strands.spineLength = HedgehogRenderer::SPINE_LENGTH;
		strands.spineWidth = HedgehogRenderer::SPINE_WIDTH;
		strands.furSeed = 78;
		strands.spineSeed = 77;
		return strands;
	}

	FurryRenderer::Bounds hedgehogBounds()
	{
		FurryRenderer::Bounds bounds;
		bounds.center = glm::vec3(0.0f, HEDGEHOG_BODY_HEIGHT, 0.0f);
		bounds.radius = 0.75f;
		return bounds;
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

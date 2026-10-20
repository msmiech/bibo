#include "BeaverModel.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

using namespace bibo;
using namespace bibo::scene;
using resources::SkinnedModel;

namespace
{
	const glm::vec3 UP(0.0f, 1.0f, 0.0f);
	const glm::vec3 BACKWARDS(0.0f, 0.0f, -1.0f);
	//The body is a surface around this point: a pear, wide at the bottom where it sits, narrow at the neck
	const glm::vec3 BODY_CENTER(0.0f, 0.41f, -0.02f);
	const glm::vec3 BODY_RADII(0.29f, 0.4f, 0.26f);
	const float SEAT_HEIGHT = 0.02f;
	//The head around this point (in the space of the head, the origin is the neck), the muzzle points forward and down
	const glm::vec3 HEAD_CENTER(0.0f, 0.14f, 0.07f);
	const glm::vec3 HEAD_RADII(0.16f, 0.15f, 0.17f);
	const glm::vec3 MUZZLE = glm::normalize(glm::vec3(0.0f, -0.35f, 1.0f));

	//Colors (linear)
	const glm::vec3 BACK_COLOR(0.12f, 0.062f, 0.03f);
	const glm::vec3 BELLY_COLOR(0.19f, 0.11f, 0.055f);
	const glm::vec3 HEAD_COLOR(0.14f, 0.075f, 0.036f);
	const glm::vec3 MUZZLE_COLOR(0.09f, 0.05f, 0.028f);
	const glm::vec3 SKIN_COLOR(0.045f, 0.032f, 0.028f); //hands and feet
	const glm::vec3 TAIL_COLOR(0.05f, 0.042f, 0.038f);
	const glm::vec3 NOSE_COLOR(0.015f, 0.012f, 0.011f);
	const glm::vec3 TEETH_COLOR(0.62f, 0.2f, 0.035f); //the enamel of beaver teeth is orange
	const glm::vec3 MOUTH_COLOR(0.1f, 0.018f, 0.015f);
	const glm::vec3 TONGUE_COLOR(0.42f, 0.12f, 0.11f);
	const glm::vec3 CLAW_COLOR(0.035f, 0.03f, 0.025f);

	float smoothMin(float a, float b, float k)
	{
		float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
		return glm::mix(b, a, h) - k * h * (1.0f - h);
	}

	/**
	* Direction of the fur on a surface with this normal: down the surface (along the wanted direction as far as possible)
	*/
	glm::vec3 combAlong(glm::vec3 normal, glm::vec3 wanted)
	{
		glm::vec3 tangent = wanted - normal * glm::dot(wanted, normal);
		return glm::length(tangent) > 1e-4f ? glm::normalize(tangent) : glm::normalize(glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f)) + glm::vec3(0.0f, -1e-3f, 0.0f));
	}

	float bodyRadius(glm::vec3 direction)
	{
		//Wider at the bottom, narrower towards the neck, the belly bulges forward
		float width = 1.0f + 0.16f * glm::smoothstep(0.1f, -0.8f, direction.y) - 0.3f * glm::smoothstep(0.25f, 0.95f, direction.y);
		float radius = 1.0f / glm::length(direction / (BODY_RADII * glm::vec3(width, 1.0f, width)));
		radius += 0.05f * std::max(direction.z, 0.0f) * glm::smoothstep(0.5f, -0.3f, direction.y);
		if (direction.y < 0.0f)
		{
			radius = smoothMin(radius, (BODY_CENTER.y - SEAT_HEIGHT) / std::max(-direction.y, 1e-3f), 0.05f);
		}
		return radius;
	}

	glm::vec3 bodyPoint(glm::vec3 direction)
	{
		return BODY_CENTER + direction * bodyRadius(direction);
	}

	float headRadius(glm::vec3 direction)
	{
		//The muzzle is narrower than the skull and blunt
		float taper = 1.0f - 0.3f * glm::smoothstep(0.3f, 0.95f, direction.z);
		float radius = 1.0f / glm::length(direction / (HEAD_RADII * glm::vec3(taper, taper, 1.0f)));
		float muzzle = glm::smoothstep(0.72f, 1.0f, glm::dot(direction, MUZZLE));
		return radius + 0.085f * std::pow(muzzle, 1.2f);
	}

	glm::vec3 headPoint(glm::vec3 direction)
	{
		return HEAD_CENTER + direction * headRadius(direction);
	}

	/**
	* Small dark eye, shaded like the eyes of the bear (the color is the direction in eye space, gaze along +z)
	*/
	void addEye(FurryModelBuilder& builder, glm::vec3 center, glm::vec3 gaze, float radius, int part)
	{
		glm::vec3 x = glm::normalize(glm::cross(UP, gaze));
		glm::vec3 y = glm::cross(gaze, x);
		builder.sphere(16, 12, [&](glm::vec3 d) {
			glm::vec4 color(glm::vec3(glm::dot(d, x), glm::dot(d, y), glm::dot(d, gaze)) * 0.5f + 0.5f, 0.0f);
			return builder.add(center + d * radius, d, color, SkinnedModel::EYE, part);
		});
	}

	/**
	* Ellipsoid with fur, combed along a direction
	*/
	void addFurryEllipsoid(FurryModelBuilder& builder, glm::vec3 center, glm::vec3 radii, glm::vec3 color, float furLength, glm::vec3 comb, int part)
	{
		std::size_t first = builder.sphere(16, 12, [&](glm::vec3 d) {
			glm::vec3 normal = glm::normalize(d / radii);
			return builder.add(center + d * radii, normal, glm::vec4(color, furLength), SkinnedModel::FUR, part, false, combAlong(normal, comb));
		});
		builder.growFur(first, builder.triangleCount());
	}

	void addClaws(FurryModelBuilder& builder, glm::vec3 center, float spacing, float length, glm::vec3 direction, int part)
	{
		glm::vec3 side = glm::normalize(glm::cross(UP, direction));
		for (int claw = 0; claw < 4; ++claw)
		{
			glm::vec3 position = center + side * ((static_cast<float>(claw) - 1.5f) * spacing);
			glm::vec3 radii = glm::abs(direction) * length + glm::vec3(0.006f);
			builder.ellipsoid(position, radii, 6, 4, glm::vec4(CLAW_COLOR, 0.0f), SkinnedModel::CLAW, part);
		}
	}
}

BeaverModel BeaverModel::build()
{
	BeaverModel model;
	FurryModelBuilder builder(model);

	//Body: thick fur, darker on the back, lighter on the belly, combed down
	std::size_t firstBodyTriangle = builder.sphere(56, 40, [&](glm::vec3 direction) {
		float belly = glm::smoothstep(-0.1f, 0.6f, direction.z) * glm::smoothstep(0.7f, 0.2f, direction.y);
		glm::vec3 color = glm::mix(BACK_COLOR, BELLY_COLOR, belly);
		float length = glm::mix(0.75f, 0.55f, belly) * glm::smoothstep(-0.98f, -0.8f, direction.y); //short where it sits
		return builder.add(bodyPoint(direction), direction, glm::vec4(color, length), SkinnedModel::FUR, BODY, false, glm::vec3(0.0f));
	});
	std::size_t lastBodyTriangle = builder.triangleCount();
	builder.smoothNormals(firstBodyTriangle, lastBodyTriangle);
	builder.growFur(firstBodyTriangle, lastBodyTriangle);

	//Head: short fur, darker on the muzzle, combed from the nose to the back of the head
	std::size_t firstHeadTriangle = builder.sphere(48, 36, [&](glm::vec3 direction) {
		float muzzle = glm::smoothstep(0.8f, 1.0f, glm::dot(direction, MUZZLE));
		glm::vec3 color = glm::mix(HEAD_COLOR, MUZZLE_COLOR, muzzle);
		return builder.add(headPoint(direction), direction, glm::vec4(color, glm::mix(0.45f, 0.18f, muzzle)), SkinnedModel::FUR, HEAD, false, glm::vec3(0.0f));
	});
	std::size_t lastHeadTriangle = builder.triangleCount();
	builder.smoothNormals(firstHeadTriangle, lastHeadTriangle);
	builder.growFur(firstHeadTriangle, lastHeadTriangle);
	//The fur follows the smoothed normals: down on the body, backwards on the head
	for (std::size_t v = 0; v < model.positions.size(); ++v)
	{
		model.combs[v] = combAlong(model.normals[v], model.parts[v] == BODY ? -UP + 0.15f * BACKWARDS : BACKWARDS - 0.3f * UP);
	}

	//Wet black nose on top of the tip of the muzzle
	glm::vec3 noseDirection = glm::normalize(glm::vec3(0.0f, -0.12f, 1.0f));
	builder.ellipsoid(headPoint(noseDirection) - noseDirection * 0.004f, glm::vec3(0.038f, 0.026f, 0.026f), 14, 10, glm::vec4(NOSE_COLOR, 0.0f), SkinnedModel::NOSE, HEAD);

	//The mouth under the muzzle (covered by the lower jaw when it is closed)
	glm::vec3 lip = headPoint(glm::normalize(glm::vec3(0.0f, -0.62f, 0.78f)));
	builder.ellipsoid(glm::vec3(0.0f, lip.y + 0.005f, lip.z - 0.05f), glm::vec3(0.055f, 0.03f, 0.065f), 14, 10, glm::vec4(MOUTH_COLOR, 0.0f), SkinnedModel::MOUTH, HEAD);

	//The famous orange front teeth, from under the nose down over the lower lip
	glm::vec3 teeth = headPoint(glm::normalize(glm::vec3(0.0f, -0.5f, 0.86f)));
	for (float side : { 1.0f, -1.0f })
	{
		builder.ellipsoid(glm::vec3(0.0165f * side, teeth.y - 0.03f, teeth.z - 0.006f), glm::vec3(0.0155f, 0.045f, 0.009f), 10, 8, glm::vec4(TEETH_COLOR, 0.0f), SkinnedModel::TEETH, HEAD);
	}

	for (float side : { 1.0f, -1.0f })
	{
		//Small dark eyes high on the sides of the head
		glm::vec3 eyeDirection = glm::normalize(glm::vec3(0.6f * side, 0.38f, 0.7f));
		const float EYE_RADIUS = 0.024f;
		addEye(builder, headPoint(eyeDirection) - eyeDirection * 0.012f, glm::normalize(glm::vec3(0.5f * side, 0.08f, 0.86f)), EYE_RADIUS, HEAD);

		//Small round ears
		glm::vec3 earDirection = glm::normalize(glm::vec3(0.72f * side, 0.6f, -0.2f));
		addFurryEllipsoid(builder, headPoint(earDirection), glm::vec3(0.022f, 0.036f, 0.032f), HEAD_COLOR * 0.8f, 0.15f, -UP, HEAD);
	}

	//Lower jaw: the chin with the lower teeth and the tongue (in the space of the jaw, the origin is its hinge)
	glm::vec3 chinCenter = glm::vec3(lip.x, lip.y - 0.012f, lip.z - 0.035f) - BEAVER_JAW;
	addFurryEllipsoid(builder, chinCenter, glm::vec3(0.06f, 0.03f, 0.065f), MUZZLE_COLOR, 0.15f, BACKWARDS - UP, JAW);
	builder.ellipsoid(chinCenter + glm::vec3(0.0f, 0.02f, -0.01f), glm::vec3(0.04f, 0.014f, 0.05f), 12, 8, glm::vec4(TONGUE_COLOR, 0.0f), SkinnedModel::TONGUE, JAW);
	for (float side : { 1.0f, -1.0f })
	{
		builder.ellipsoid(chinCenter + glm::vec3(0.009f * side, 0.026f, 0.05f), glm::vec3(0.009f, 0.02f, 0.007f), 8, 6, glm::vec4(TEETH_COLOR, 0.0f), SkinnedModel::TEETH, JAW);
	}

	//Arms: hanging down in front of the chest, the hands with bare palms and claws (in the space of the shoulder)
	for (int part : { ARM_LEFT, ARM_RIGHT })
	{
		addFurryEllipsoid(builder, glm::vec3(0.0f, -0.09f, 0.045f), glm::vec3(0.06f, 0.12f, 0.065f), BACK_COLOR, 0.5f, -UP, part);
		builder.ellipsoid(glm::vec3(0.0f, -0.2f, 0.095f), glm::vec3(0.048f, 0.032f, 0.052f), 12, 8, glm::vec4(SKIN_COLOR, 0.0f), SkinnedModel::FUR, part);
		addClaws(builder, glm::vec3(0.0f, -0.212f, 0.14f), 0.022f, 0.018f, glm::vec3(0.0f, 0.0f, 1.0f), part);
	}

	//Large webbed hind feet in front of the body (in the space of the foot, the origin on the ground under the heel)
	for (int part : { FOOT_LEFT, FOOT_RIGHT })
	{
		builder.ellipsoid(glm::vec3(0.0f, 0.028f, 0.09f), glm::vec3(0.075f, 0.028f, 0.15f), 16, 8, glm::vec4(SKIN_COLOR, 0.0f), SkinnedModel::FUR, part);
		addClaws(builder, glm::vec3(0.0f, 0.016f, 0.235f), 0.034f, 0.02f, glm::vec3(0.0f, 0.0f, 1.0f), part);
	}

	//Tail: a furry root and the flat paddle with scales lying on the ground behind (in the space of its root)
	addFurryEllipsoid(builder, glm::vec3(0.0f, -0.035f, -0.07f), glm::vec3(0.1f, 0.065f, 0.12f), BACK_COLOR, 0.5f, BACKWARDS, TAIL);
	const glm::vec3 PADDLE_CENTER(0.0f, -0.07f, -0.36f), PADDLE_RADII(0.155f, 0.024f, 0.27f);
	builder.sphere(32, 16, [&](glm::vec3 d) {
		glm::vec3 position = PADDLE_CENTER + d * PADDLE_RADII;
		//Rows of scales: darker grooves in a diamond pattern
		float u = (position.x + position.z) * 55.0f, v = (position.x - position.z) * 55.0f;
		float grooves = std::min(std::abs(std::sin(u)), std::abs(std::sin(v)));
		glm::vec3 color = TAIL_COLOR * glm::mix(0.55f, 1.15f, glm::smoothstep(0.0f, 0.35f, grooves));
		return builder.add(position, d / PADDLE_RADII, glm::vec4(color, 0.0f), SkinnedModel::FUR, TAIL);
	});
	return model;
}

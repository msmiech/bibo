#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Scene/Camera.hpp"
#include "TestSupport.hpp"

using namespace bibo::scene;
using namespace bibo::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using glm::mat4;
using glm::vec3;
using glm::vec4;

namespace
{
	const vec3 UP(0.0f, 1.0f, 0.0f);

	class Target : public SceneObject
	{
	public:
		Target(mat4 modelMatrix, vec3 direction, GLFWwindow& window) : SceneObject(modelMatrix, window), direction(direction) {}
		void update(float) override {}
		void draw(bibo::Shader*) override {}
		vec3 getDirection() override { return direction; }
	private:
		vec3 direction;
	};

	vec3 toView(Camera& camera, vec3 worldPosition)
	{
		return vec3(camera.getViewMatrix() * vec4(worldPosition, 1.0f));
	}

	float depth(const mat4& projection, float viewZ)
	{
		vec4 clip = projection * vec4(0.0f, 0.0f, viewZ, 1.0f);
		return clip.z / clip.w;
	}
}

TEST_CASE("The projection of the camera fits the window", "[gl][camera]")
{
	GlContext gl(1024, 576);
	Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());

	mat4 projection = camera.getProjectionMatrix();

	CHECK_THAT(projection[1][1] / projection[0][0], WithinRel(1024.0f / 576.0f, 1e-5f)); //aspect ratio
	CHECK_THAT(projection[1][1], WithinRel(1.0f / std::tan(glm::radians(30.0f)), 1e-5f)); //60 degrees vertical field of view
	CHECK_THAT(depth(projection, -0.1f), WithinAbs(-1.0f, 1e-4f)); //near plane
	CHECK_THAT(depth(projection, -200.0f), WithinAbs(1.0f, 1e-4f)); //far plane
}

TEST_CASE("The camera looks from its position along its direction", "[gl][camera]")
{
	GlContext gl;
	const vec3 position(0.0f, 1.0f, 2.0f);
	Camera camera(position, vec3(0.0f, 0.0f, -1.0f), UP, gl.window());

	CHECK(camera.getPosition() == position);
	CHECK_THAT(toView(camera, position), IsNear(vec3(0.0f), 1e-6f));
	CHECK_THAT(toView(camera, position + vec3(0.0f, 0.0f, -5.0f)), IsNear(vec3(0.0f, 0.0f, -5.0f), 1e-5f)); //straight ahead
	CHECK_THAT(toView(camera, position + vec3(0.0f, 2.0f, 0.0f)), IsNear(vec3(0.0f, 2.0f, 0.0f), 1e-5f)); //up stays up
}

TEST_CASE("The camera can be pointed at a target", "[gl][camera]")
{
	GlContext gl;
	Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());

	camera.setTarget(vec3(10.0f, 1.0f, 2.0f));

	CHECK_THAT(toView(camera, vec3(10.0f, 1.0f, 2.0f)), IsNear(vec3(0.0f, 0.0f, -10.0f), 1e-4f));
}

TEST_CASE("The camera follows a target from behind and above", "[gl][camera]")
{
	GlContext gl;
	Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());
	const vec3 target(4.0f, 0.0f, -6.0f);
	const vec3 targetDirection(0.0f, 0.0f, -1.0f);

	SECTION("Position and direction of the target")
	{
		camera.setTarget(target, targetDirection * 5.0f); //the length of the direction does not matter

		CHECK_THAT(camera.getPosition(), IsNear(target - 3.0f * targetDirection + vec3(0.0f, 2.5f, 0.0f), 1e-5f));
		vec3 targetInView = toView(camera, target);
		CHECK_THAT(targetInView.x, WithinAbs(0.0f, 1e-4f));
		CHECK_THAT(targetInView.y, WithinAbs(0.0f, 1e-4f));
		CHECK(targetInView.z < 0.0f);
	}

	SECTION("Scene object (the bear)")
	{
		Target bear(glm::translate(mat4(1.0f), target), targetDirection, gl.window());
		//The direction is computed from the previous position of the camera, so it settles after the second frame
		camera.setTarget(&bear);
		camera.setTarget(&bear);

		vec3 offset = camera.getPosition() - target;
		CHECK(offset.y > 0.0f); //above
		CHECK(offset.z > 0.0f); //behind (the target looks along -z)
		vec3 pointAheadOfTarget = toView(camera, target + 3.0f * targetDirection);
		CHECK_THAT(pointAheadOfTarget.x, WithinAbs(0.0f, 1e-4f));
		CHECK_THAT(pointAheadOfTarget.y, WithinAbs(0.0f, 1e-4f));
		CHECK(pointAheadOfTarget.z < 0.0f);
	}
}

TEST_CASE("The camera is locked to the bear and does not move on its own", "[gl][camera]")
{
	GlContext gl;
	const vec3 position(0.0f, 1.0f, 2.0f);
	Camera camera(position, vec3(0.0f, 0.0f, -1.0f), UP, gl.window());

	CHECK(camera.isLocked());
	camera.update(0.5f); //no keys pressed
	CHECK(camera.isLocked());
	CHECK(camera.getPosition() == position);
}

TEST_CASE("Scene objects can be moved without changing their rotation", "[gl]")
{
	GlContext gl;
	const mat4 rotated = glm::rotate(mat4(1.0f), glm::radians(30.0f), UP);
	Target object(rotated, vec3(0.0f), gl.window());

	object.setPosition(vec3(1.0f, 2.0f, 3.0f));

	CHECK(object.getPosition() == vec3(1.0f, 2.0f, 3.0f));
	CHECK(glm::mat3(object.modelMatrix) == glm::mat3(rotated));
}

TEST_CASE("The view frustum of the camera matches its view and projection matrix", "[gl][camera][frustum]")
{
	GlContext gl(1024, 576);
	Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());
	SECTION("Initial direction")
	{
	}
	SECTION("Pointed at a target")
	{
		camera.setTarget(vec3(30.0f, -5.0f, 12.0f));
	}
	SECTION("Following a target")
	{
		camera.setTarget(vec3(-20.0f, 3.0f, 40.0f), vec3(1.0f, 0.0f, 1.0f));
	}

	const ViewFrustum frustum = camera.getViewFrustum();
	const mat4 viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
	int inside = 0, outside = 0;
	for (float x = -150.0f; x <= 150.0f; x += 7.0f)
	{
		for (float y = -60.0f; y <= 60.0f; y += 7.0f)
		{
			for (float z = -150.0f; z <= 150.0f; z += 7.0f)
			{
				const vec3 point = camera.getPosition() + vec3(x, y, z);
				Clipped expected = clip(viewProjection, point);
				if (expected == Clipped::BORDER)
				{
					continue;
				}
				bool isInside = frustum.pointInFrustum(point) == ViewFrustum::INSIDE;
				if (isInside != (expected == Clipped::INSIDE))
				{
					FAIL(glm::to_string(point) << (isInside ? " is inside of the frustum, but not on the screen" : " is outside of the frustum, but on the screen"));
				}
				++(isInside ? inside : outside);
			}
		}
	}
	CHECK(inside > 100);
	CHECK(outside > 100);
}

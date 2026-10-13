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
	CHECK_THAT(depth(projection, -camera.getNearPlane()), WithinAbs(-1.0f, 1e-4f)); //near plane
	CHECK_THAT(depth(projection, -camera.getFarPlane()), WithinAbs(1.0f, 1e-4f)); //far plane
	CHECK(camera.getNearPlane() < Camera::COLLISION_RADIUS); //the environment never comes closer than the near plane
	CHECK(camera.getFarPlane() > 1000.0f); //the island and the sea up to the horizon
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
		Camera::FollowTarget bear;
		bear.position = target;
		bear.direction = targetDirection;
		camera.snapBehind(bear);
		camera.follow(bear, 1.0f / 60.0f);

		vec3 offset = camera.getPosition() - target;
		CHECK(offset.y > 0.0f); //above
		CHECK(offset.z > 0.0f); //behind (the target looks along -z)
		CHECK_THAT(glm::length(camera.getPosition() - camera.getPivot()), WithinAbs(Camera::DEFAULT_DISTANCE, 1e-3f));
		vec3 pivotInView = toView(camera, camera.getPivot()); //the camera orbits around the point above the target
		CHECK_THAT(pivotInView.x, WithinAbs(0.0f, 1e-4f));
		CHECK_THAT(pivotInView.y, WithinAbs(0.0f, 1e-4f));
		CHECK(pivotInView.z < 0.0f);
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

namespace
{
	Camera::FollowTarget bearAt(vec3 position, vec3 direction, vec3 velocity = vec3(0.0f))
	{
		Camera::FollowTarget target;
		target.position = position;
		target.direction = direction;
		target.velocity = velocity;
		return target;
	}

	void followFor(Camera& camera, const Camera::FollowTarget& target, float seconds)
	{
		for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
		{
			camera.follow(target, 1.0f / 60.0f);
		}
	}
}

TEST_CASE("The camera orbits around the bear", "[gl][camera]")
{
	GlContext gl;
	Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());
	const Camera::FollowTarget bear = bearAt(vec3(3.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, 1.0f));
	camera.snapBehind(bear);
	const vec3 right = camera.getRight();
	CHECK_THAT(camera.getForward(), IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-5f)); //behind the bear
	CHECK_THAT(right, IsNear(vec3(-1.0f, 0.0f, 0.0f), 1e-5f));

	SECTION("Turning right")
	{
		camera.rotate(glm::radians(90.0f), 0.0f);
		CHECK_THAT(camera.getForward(), IsNear(right, 1e-5f));
		CHECK_THAT(camera.getPosition().x, WithinAbs(camera.getPivot().x + Camera::DEFAULT_DISTANCE * std::cos(camera.getPitch()), 1e-3f));
	}
	SECTION("Looking down and up stays within limits, the distance stays the same")
	{
		camera.rotate(0.0f, 10.0f);
		CHECK_THAT(camera.getPitch(), WithinAbs(glm::radians(70.0f), 1e-5f));
		CHECK(camera.getPosition().y > camera.getPivot().y + 6.0f);
		camera.rotate(0.0f, -10.0f);
		CHECK_THAT(camera.getPitch(), WithinAbs(glm::radians(-20.0f), 1e-5f));
		CHECK(camera.getPosition().y < camera.getPivot().y);
		CHECK_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(Camera::DEFAULT_DISTANCE, 1e-3f));
	}
	SECTION("The bear stays in the middle of the image")
	{
		camera.rotate(1.3f, -0.2f);
		vec3 pivot = toView(camera, camera.getPivot());
		CHECK_THAT(pivot.x, WithinAbs(0.0f, 1e-4f));
		CHECK_THAT(pivot.y, WithinAbs(0.0f, 1e-4f));
	}
	SECTION("Zooming")
	{
		camera.zoom(100.0f);
		followFor(camera, bear, 2.0f);
		CHECK_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(Camera::MIN_DISTANCE, 0.01f));
	}
}

TEST_CASE("The camera follows the bear and goes back behind it when it runs", "[gl][camera]")
{
	GlContext gl;
	Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());
	const vec3 direction(0.0f, 0.0f, 1.0f);
	camera.snapBehind(bearAt(vec3(0.0f), direction));
	camera.rotate(1.0f, 0.0f);
	const float turned = camera.getYaw();

	SECTION("Standing: the camera stays where the player turned it")
	{
		followFor(camera, bearAt(vec3(0.0f), direction), 5.0f);
		CHECK_THAT(camera.getYaw(), WithinAbs(turned, 1e-5f));
	}
	SECTION("Running: after a moment the camera turns back behind the bear")
	{
		vec3 position(0.0f);
		for (int i = 0; i < 60; ++i)
		{
			position += direction * (10.0f / 60.0f);
			camera.follow(bearAt(position, direction, direction * 10.0f), 1.0f / 60.0f);
		}
		CHECK_THAT(camera.getYaw(), WithinAbs(turned, 1e-5f)); //not right away
		for (int i = 0; i < 5 * 60; ++i)
		{
			position += direction * (10.0f / 60.0f);
			camera.follow(bearAt(position, direction, direction * 10.0f), 1.0f / 60.0f);
		}
		CHECK_THAT(camera.getYaw(), WithinAbs(0.0f, 0.05f));
		CHECK(glm::distance(vec3(camera.getPivot().x, 0.0f, camera.getPivot().z), position) < 0.5f); //keeps up (a bit behind at full speed)
	}
	SECTION("Recentered")
	{
		camera.recenter();
		followFor(camera, bearAt(vec3(0.0f), direction), 0.5f);
		CHECK_THAT(camera.getYaw(), WithinAbs(0.0f, 0.02f));
	}
}

TEST_CASE("The camera does not go into the environment", "[gl][camera]")
{
	GlContext gl;
	Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), UP, gl.window());
	float free = 1.0f;
	float probedRadius = 0.0f;
	camera.setCollisionProbe([&](vec3, vec3, float radius) {
		probedRadius = radius;
		return free;
	});
	const Camera::FollowTarget bear = bearAt(vec3(0.0f), vec3(0.0f, 0.0f, 1.0f));
	camera.snapBehind(bear);
	REQUIRE_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(Camera::DEFAULT_DISTANCE, 1e-3f));
	CHECK(probedRadius > 0.13f); //larger than the near plane (the camera does not see into a wall)

	//Something gets between the bear and the camera: it moves in front of it at once
	free = 0.4f;
	camera.follow(bear, 1.0f / 60.0f);
	CHECK_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(0.4f * Camera::DEFAULT_DISTANCE, 1e-3f));
	camera.rotate(0.2f, 0.0f);
	CHECK_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(0.4f * Camera::DEFAULT_DISTANCE, 1e-3f));

	//And moves back smoothly when it is gone
	free = 1.0f;
	camera.follow(bear, 1.0f / 60.0f);
	float distance = glm::distance(camera.getPosition(), camera.getPivot());
	CHECK(distance > 0.4f * Camera::DEFAULT_DISTANCE);
	CHECK(distance < Camera::DEFAULT_DISTANCE - 1.0f);
	followFor(camera, bear, 2.0f);
	CHECK_THAT(glm::distance(camera.getPosition(), camera.getPivot()), WithinAbs(Camera::DEFAULT_DISTANCE, 1e-2f));
}

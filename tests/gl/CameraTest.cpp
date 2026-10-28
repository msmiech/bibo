#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "Scene/Camera.hpp"
#include "TestSupport.hpp"

using bibo::scene::Camera;
using bibo::scene::SceneObject;
using bibo::scene::ViewFrustum;
using bibo::test::Clipped;
using bibo::test::GlContext;
using bibo::test::IsNear;

using bibo::test::Clip;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using glm::mat4;
using glm::vec3;
using glm::vec4;

namespace {
const vec3 kUp(0.0f, 1.0f, 0.0f);

class Target : public SceneObject {
 public:
  Target(mat4 model_matrix, vec3 direction, GLFWwindow& window)
      : SceneObject(model_matrix, window), direction_(direction) {}
  void Update(float) override {}
  void Draw(bibo::Shader*) override {}
  vec3 GetDirection() override { return direction_; }

 private:
  vec3 direction_;
};

vec3 ToView(Camera& camera, vec3 world_position) {
  return vec3(camera.GetViewMatrix() * vec4(world_position, 1.0f));
}

float Depth(const mat4& projection, float view_z) {
  vec4 clip = projection * vec4(0.0f, 0.0f, view_z, 1.0f);
  return clip.z / clip.w;
}
}  // namespace

TEST_CASE("The projection of the camera fits the window", "[gl][camera]") {
  GlContext gl(1024, 576);
  Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());

  mat4 projection = camera.GetProjectionMatrix();

  CHECK_THAT(projection[1][1] / projection[0][0],
             WithinRel(1024.0f / 576.0f, 1e-5f));  // aspect ratio
  CHECK_THAT(projection[1][1],
             WithinRel(1.0f / std::tan(glm::radians(30.0f)),
                       1e-5f));  // 60 degrees vertical field of view
  CHECK_THAT(Depth(projection, -camera.GetNearPlane()),
             WithinAbs(-1.0f, 1e-4f));  // near plane
  CHECK_THAT(Depth(projection, -camera.GetFarPlane()),
             WithinAbs(1.0f, 1e-4f));  // far plane
  // the environment never comes closer than the near plane
  CHECK(camera.GetNearPlane() < Camera::kCollisionRadius);
  // the island and the sea up to the horizon
  CHECK(camera.GetFarPlane() > 1000.0f);
}

TEST_CASE("The camera looks from its position along its direction",
          "[gl][camera]") {
  GlContext gl;
  const vec3 position(0.0f, 1.0f, 2.0f);
  Camera camera(position, vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());

  CHECK(camera.GetPosition() == position);
  CHECK_THAT(ToView(camera, position), IsNear(vec3(0.0f), 1e-6f));
  CHECK_THAT(ToView(camera, position + vec3(0.0f, 0.0f, -5.0f)),
             IsNear(vec3(0.0f, 0.0f, -5.0f), 1e-5f));  // straight ahead
  CHECK_THAT(ToView(camera, position + vec3(0.0f, 2.0f, 0.0f)),
             IsNear(vec3(0.0f, 2.0f, 0.0f), 1e-5f));  // up stays up
}

TEST_CASE("The camera can be pointed at a target", "[gl][camera]") {
  GlContext gl;
  Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), kUp,
                gl.Window());

  camera.SetTarget(vec3(10.0f, 1.0f, 2.0f));

  CHECK_THAT(ToView(camera, vec3(10.0f, 1.0f, 2.0f)),
             IsNear(vec3(0.0f, 0.0f, -10.0f), 1e-4f));
}

TEST_CASE("The camera follows a target from behind and above", "[gl][camera]") {
  GlContext gl;
  Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), kUp,
                gl.Window());
  const vec3 target(4.0f, 0.0f, -6.0f);
  const vec3 target_direction(0.0f, 0.0f, -1.0f);

  SECTION("Position and direction of the target") {
    camera.SetTarget(target,
                     target_direction *
                         5.0f);  // the length of the direction does not matter

    CHECK_THAT(camera.GetPosition(),
               IsNear(target - 3.0f * target_direction + vec3(0.0f, 2.5f, 0.0f),
                      1e-5f));
    vec3 target_in_view = ToView(camera, target);
    CHECK_THAT(target_in_view.x, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(target_in_view.y, WithinAbs(0.0f, 1e-4f));
    CHECK(target_in_view.z < 0.0f);
  }

  SECTION("Scene object (the bear)") {
    Camera::FollowTarget bear;
    bear.position = target;
    bear.direction = target_direction;
    camera.SnapBehind(bear);
    camera.Follow(bear, 1.0f / 60.0f);

    vec3 offset = camera.GetPosition() - target;
    CHECK(offset.y > 0.0f);  // above
    CHECK(offset.z > 0.0f);  // behind (the target looks along -z)
    CHECK_THAT(glm::length(camera.GetPosition() - camera.GetPivot()),
               WithinAbs(Camera::kDefaultDistance, 1e-3f));
    // the camera orbits around the point above the target
    vec3 pivot_in_view = ToView(camera, camera.GetPivot());
    CHECK_THAT(pivot_in_view.x, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(pivot_in_view.y, WithinAbs(0.0f, 1e-4f));
    CHECK(pivot_in_view.z < 0.0f);
  }
}

TEST_CASE("The camera is locked to the bear and does not move on its own",
          "[gl][camera]") {
  GlContext gl;
  const vec3 position(0.0f, 1.0f, 2.0f);
  Camera camera(position, vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());

  CHECK(camera.IsLocked());
  camera.Update(0.5f);  // no keys pressed
  CHECK(camera.IsLocked());
  CHECK(camera.GetPosition() == position);
}

TEST_CASE("Scene objects can be moved without changing their rotation",
          "[gl]") {
  GlContext gl;
  const mat4 rotated = glm::rotate(mat4(1.0f), glm::radians(30.0f), kUp);
  Target object(rotated, vec3(0.0f), gl.Window());

  object.SetPosition(vec3(1.0f, 2.0f, 3.0f));

  CHECK(object.GetPosition() == vec3(1.0f, 2.0f, 3.0f));
  CHECK(glm::mat3(object.model_matrix) == glm::mat3(rotated));
}

TEST_CASE(
    "The view frustum of the camera matches its view and projection matrix",
    "[gl][camera][frustum]") {
  GlContext gl(1024, 576);
  Camera camera(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), kUp,
                gl.Window());
  SECTION("Initial direction") {}
  SECTION("Pointed at a target") {
    camera.SetTarget(vec3(30.0f, -5.0f, 12.0f));
  }
  SECTION("Following a target") {
    camera.SetTarget(vec3(-20.0f, 3.0f, 40.0f), vec3(1.0f, 0.0f, 1.0f));
  }

  const ViewFrustum frustum = camera.GetViewFrustum();
  const mat4 view_projection =
      camera.GetProjectionMatrix() * camera.GetViewMatrix();
  int inside = 0, outside = 0;
  for (float x = -150.0f; x <= 150.0f; x += 7.0f) {
    for (float y = -60.0f; y <= 60.0f; y += 7.0f) {
      for (float z = -150.0f; z <= 150.0f; z += 7.0f) {
        const vec3 point = camera.GetPosition() + vec3(x, y, z);
        Clipped expected = Clip(view_projection, point);
        if (expected == Clipped::kBorder) {
          continue;
        }
        bool is_inside = frustum.PointInFrustum(point) == ViewFrustum::kInside;
        if (is_inside != (expected == Clipped::kInside)) {
          FAIL(glm::to_string(point)
               << (is_inside
                       ? " is inside of the frustum, but not on the screen"
                       : " is outside of the frustum, but on the screen"));
        }
        ++(is_inside ? inside : outside);
      }
    }
  }
  CHECK(inside > 100);
  CHECK(outside > 100);
}

namespace {
Camera::FollowTarget BearAt(vec3 position, vec3 direction,
                            vec3 velocity = vec3(0.0f)) {
  Camera::FollowTarget target;
  target.position = position;
  target.direction = direction;
  target.velocity = velocity;
  return target;
}

void FollowFor(Camera& camera, const Camera::FollowTarget& target,
               float seconds) {
  for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
    camera.Follow(target, 1.0f / 60.0f);
  }
}
}  // namespace

TEST_CASE("The camera orbits around the bear", "[gl][camera]") {
  GlContext gl;
  Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());
  const Camera::FollowTarget bear =
      BearAt(vec3(3.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, 1.0f));
  camera.SnapBehind(bear);
  const vec3 right = camera.GetRight();
  CHECK_THAT(camera.GetForward(),
             IsNear(vec3(0.0f, 0.0f, 1.0f), 1e-5f));  // behind the bear
  CHECK_THAT(right, IsNear(vec3(-1.0f, 0.0f, 0.0f), 1e-5f));

  SECTION("Turning right") {
    camera.Rotate(glm::radians(90.0f), 0.0f);
    CHECK_THAT(camera.GetForward(), IsNear(right, 1e-5f));
    CHECK_THAT(camera.GetPosition().x,
               WithinAbs(camera.GetPivot().x + Camera::kDefaultDistance *
                                                   std::cos(camera.GetPitch()),
                         1e-3f));
  }
  SECTION(
      "Looking down and up stays within limits, the distance stays the same") {
    camera.Rotate(0.0f, 10.0f);
    CHECK_THAT(camera.GetPitch(), WithinAbs(glm::radians(70.0f), 1e-5f));
    CHECK(camera.GetPosition().y > camera.GetPivot().y + 6.0f);
    camera.Rotate(0.0f, -10.0f);
    CHECK_THAT(camera.GetPitch(), WithinAbs(glm::radians(-20.0f), 1e-5f));
    CHECK(camera.GetPosition().y < camera.GetPivot().y);
    CHECK_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
               WithinAbs(Camera::kDefaultDistance, 1e-3f));
  }
  SECTION("The bear stays in the middle of the image") {
    camera.Rotate(1.3f, -0.2f);
    vec3 pivot = ToView(camera, camera.GetPivot());
    CHECK_THAT(pivot.x, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(pivot.y, WithinAbs(0.0f, 1e-4f));
  }
  SECTION("Zooming") {
    camera.Zoom(100.0f);
    FollowFor(camera, bear, 2.0f);
    CHECK_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
               WithinAbs(Camera::kMinDistance, 0.01f));
  }
}

TEST_CASE("The camera follows the bear and goes back behind it when it runs",
          "[gl][camera]") {
  GlContext gl;
  Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());
  const vec3 direction(0.0f, 0.0f, 1.0f);
  camera.SnapBehind(BearAt(vec3(0.0f), direction));
  camera.Rotate(1.0f, 0.0f);
  const float turned = camera.GetYaw();

  SECTION("Standing: the camera stays where the player turned it") {
    FollowFor(camera, BearAt(vec3(0.0f), direction), 5.0f);
    CHECK_THAT(camera.GetYaw(), WithinAbs(turned, 1e-5f));
  }
  SECTION("Running: after a moment the camera turns back behind the bear") {
    vec3 position(0.0f);
    for (int i = 0; i < 60; ++i) {
      position += direction * (10.0f / 60.0f);
      camera.Follow(BearAt(position, direction, direction * 10.0f),
                    1.0f / 60.0f);
    }
    CHECK_THAT(camera.GetYaw(), WithinAbs(turned, 1e-5f));  // not right away
    for (int i = 0; i < 5 * 60; ++i) {
      position += direction * (10.0f / 60.0f);
      camera.Follow(BearAt(position, direction, direction * 10.0f),
                    1.0f / 60.0f);
    }
    CHECK_THAT(camera.GetYaw(), WithinAbs(0.0f, 0.05f));
    CHECK(glm::distance(vec3(camera.GetPivot().x, 0.0f, camera.GetPivot().z),
                        // keeps up (a bit behind at full speed)
                        position) < 0.5f);
  }
  SECTION("Recentered") {
    camera.Recenter();
    FollowFor(camera, BearAt(vec3(0.0f), direction), 0.5f);
    CHECK_THAT(camera.GetYaw(), WithinAbs(0.0f, 0.02f));
  }
}

TEST_CASE("The camera does not go into the environment", "[gl][camera]") {
  GlContext gl;
  Camera camera(vec3(0.0f), vec3(0.0f, 0.0f, -1.0f), kUp, gl.Window());
  float free = 1.0f;
  float probed_radius = 0.0f;
  camera.SetCollisionProbe([&](vec3, vec3, float radius) {
    probed_radius = radius;
    return free;
  });
  const Camera::FollowTarget bear = BearAt(vec3(0.0f), vec3(0.0f, 0.0f, 1.0f));
  camera.SnapBehind(bear);
  REQUIRE_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
               WithinAbs(Camera::kDefaultDistance, 1e-3f));
  // larger than the near plane (the camera does not see into a wall)
  CHECK(probed_radius > 0.13f);

  // Something gets between the bear and the camera: it moves in front of it at
  // once
  free = 0.4f;
  camera.Follow(bear, 1.0f / 60.0f);
  CHECK_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
             WithinAbs(0.4f * Camera::kDefaultDistance, 1e-3f));
  camera.Rotate(0.2f, 0.0f);
  CHECK_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
             WithinAbs(0.4f * Camera::kDefaultDistance, 1e-3f));

  // And moves back smoothly when it is gone
  free = 1.0f;
  camera.Follow(bear, 1.0f / 60.0f);
  float distance = glm::distance(camera.GetPosition(), camera.GetPivot());
  CHECK(distance > 0.4f * Camera::kDefaultDistance);
  CHECK(distance < Camera::kDefaultDistance - 1.0f);
  FollowFor(camera, bear, 2.0f);
  CHECK_THAT(glm::distance(camera.GetPosition(), camera.GetPivot()),
             WithinAbs(Camera::kDefaultDistance, 1e-2f));
}

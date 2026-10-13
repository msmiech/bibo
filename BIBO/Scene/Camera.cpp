#include "Camera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include "../Animation/Skeleton.hpp"
#include "../Util/Input.hpp"

using namespace bibo;
using namespace scene;
using animation::smoothingFactor;

namespace
{
	//The locked camera orbits around this point above the center of the target, looking down a little by default
	const float PIVOT_HEIGHT = 0.75f;
	const float DEFAULT_PITCH = glm::radians(17.0f);
	const float MIN_PITCH = glm::radians(-20.0f), MAX_PITCH = glm::radians(70.0f); //from slightly below to high above
	const float FREE_MAX_PITCH = glm::radians(89.0f);
	//Following: closely in the horizontal, the height lags behind (a jump does not shake the view), but not too far
	const float FOLLOW_HALF_LIFE = 0.03f, HEIGHT_HALF_LIFE = 0.08f, AIR_HEIGHT_HALF_LIFE = 0.25f, MAX_HEIGHT_LAG = 1.5f;
	//When the player did not turn the camera for a while, it goes back behind the running target (not when it runs towards the camera)
	const float AUTO_RECENTER_DELAY = 1.5f, AUTO_RECENTER_HALF_LIFE = 0.9f, AUTO_RECENTER_MAX_ANGLE = glm::radians(110.0f);
	const float RECENTER_HALF_LIFE = 0.06f;
	//Obstacles pull the camera in at once, it moves back out smoothly
	const float RETURN_HALF_LIFE = 0.2f;
	//The field of view widens a bit at full speed
	const float BASE_APERTURE = 60.0f, SPEED_APERTURE = 6.0f;

	float wrapAngle(float angle)
	{
		const float pi = glm::pi<float>();
		angle = std::fmod(angle + pi, 2.0f * pi);
		return angle < 0.0f ? angle + pi : angle - pi;
	}

	float yawOf(vec3 direction)
	{
		return std::atan2(direction.x, direction.z);
	}
}

Camera::Camera(vec3 position, vec3 direction, vec3 up, GLFWwindow& window) : SceneObject(window),
position(position),
up(up)
{
	glfwGetWindowSize(&window, &windowWidth, &windowHeight);
	setDirection(direction);
}

Camera::~Camera()
{
}

void Camera::applyAngles()
{
	direction = normalize(vec3(cos(pitch) * sin(yaw), -sin(pitch), cos(pitch) * cos(yaw)));
}

void Camera::setDirection(vec3 newDirection)
{
	direction = normalize(newDirection);
	yaw = std::atan2(direction.x, direction.z);
	pitch = -std::asin(std::clamp(direction.y, -1.0f, 1.0f));
}

void Camera::update(float time_delta)
{
	//Toggled once per key press, not in every frame while the key is held
	bool lockKeyPressed = input::isKeyDown(window, GLFW_KEY_L);
	if (lockKeyPressed && !lockKeyWasPressed) {
		locked = !locked;
		if (!locked)
		{
			pitch = std::clamp(pitch, -FREE_MAX_PITCH, FREE_MAX_PITCH);
		}
	}
	lockKeyWasPressed = lockKeyPressed;
	if (!locked)
	{
		auto velocity = speed * time_delta;
		vec3 right = normalize(cross(direction, up));
		if (input::isKeyDown(window, GLFW_KEY_W))
		{
			position += direction * velocity;
		}
		if (input::isKeyDown(window, GLFW_KEY_S))
		{
			position -= direction * velocity;
		}
		if (input::isKeyDown(window, GLFW_KEY_A))
		{
			position -= right * velocity;
		}
		if (input::isKeyDown(window, GLFW_KEY_D))
		{
			position += right * velocity;
		}
	}
}

void Camera::rotate(float right, float down)
{
	if (right == 0.0f && down == 0.0f)
	{
		return;
	}
	yaw = wrapAngle(yaw - right);
	pitch = locked ? std::clamp(pitch + down, MIN_PITCH, MAX_PITCH) : std::clamp(pitch + down, -FREE_MAX_PITCH, FREE_MAX_PITCH);
	timeSinceRotated = 0.0f;
	recentering = false;
	applyAngles();
	if (locked && hasPivot)
	{
		placeAroundPivot(0.0f);
	}
}

void Camera::zoom(float steps)
{
	targetDistance = std::clamp(targetDistance - 0.8f * steps, MIN_DISTANCE, MAX_DISTANCE);
}

void Camera::recenter()
{
	recentering = true;
}

vec3 Camera::pivotOf(const FollowTarget& target) const
{
	return target.position + vec3(0.0f, PIVOT_HEIGHT, 0.0f);
}

void Camera::snapBehind(const FollowTarget& target)
{
	pivot = pivotOf(target);
	hasPivot = true;
	yaw = yawOf(target.direction);
	pitch = DEFAULT_PITCH;
	distance = targetDistance;
	timeSinceRotated = 0.0f;
	recentering = false;
	aperture = BASE_APERTURE;
	applyAngles();
	placeAroundPivot(0.0f);
}

void Camera::follow(const FollowTarget& target, float time_delta)
{
	if (!hasPivot)
	{
		snapBehind(target);
		return;
	}
	if (time_delta <= 0.0f)
	{
		return;
	}

	vec3 goal = pivotOf(target);
	float horizontal = smoothingFactor(FOLLOW_HALF_LIFE, time_delta);
	pivot.x += (goal.x - pivot.x) * horizontal;
	pivot.z += (goal.z - pivot.z) * horizontal;
	pivot.y += (goal.y - pivot.y) * smoothingFactor(target.airborne ? AIR_HEIGHT_HALF_LIFE : HEIGHT_HALF_LIFE, time_delta);
	pivot.y = std::clamp(pivot.y, goal.y - MAX_HEIGHT_LAG, goal.y + MAX_HEIGHT_LAG);

	float speed = length(vec3(target.velocity.x, 0.0f, target.velocity.z));
	float behind = yawOf(target.direction);
	timeSinceRotated += time_delta;
	if (recentering)
	{
		float turn = wrapAngle(behind - yaw);
		yaw = wrapAngle(yaw + turn * smoothingFactor(RECENTER_HALF_LIFE, time_delta));
		pitch += (DEFAULT_PITCH - pitch) * smoothingFactor(RECENTER_HALF_LIFE, time_delta);
		recentering = std::abs(turn) > 0.01f || std::abs(DEFAULT_PITCH - pitch) > 0.01f;
	}
	else if (timeSinceRotated > AUTO_RECENTER_DELAY && speed > 2.0f)
	{
		float turn = wrapAngle(behind - yaw);
		if (std::abs(turn) < AUTO_RECENTER_MAX_ANGLE)
		{
			float amount = smoothingFactor(AUTO_RECENTER_HALF_LIFE, time_delta) * std::clamp(speed / 10.0f, 0.0f, 1.0f);
			yaw = wrapAngle(yaw + turn * amount);
			pitch += (DEFAULT_PITCH - pitch) * amount;
		}
	}
	applyAngles();

	distance += (targetDistance - distance) * smoothingFactor(0.08f, time_delta);
	float targetAperture = BASE_APERTURE + SPEED_APERTURE * std::clamp((speed - 4.0f) / 6.0f, 0.0f, 1.0f);
	aperture += (targetAperture - aperture) * smoothingFactor(0.3f, time_delta);

	placeAroundPivot(time_delta);
}

void Camera::placeAroundPivot(float time_delta)
{
	vec3 back = -direction;
	float allowed = distance;
	if (collisionProbe)
	{
		allowed = distance * std::clamp(collisionProbe(pivot, pivot + back * distance, COLLISION_RADIUS), 0.0f, 1.0f);
	}
	if (allowed < currentDistance || time_delta <= 0.0f)
	{
		currentDistance = allowed; //never through a wall
	}
	else
	{
		currentDistance += (allowed - currentDistance) * smoothingFactor(RETURN_HALF_LIFE, time_delta);
	}
	position = pivot + back * currentDistance;
}

vec3 Camera::getForward() const
{
	return vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

vec3 Camera::getRight() const
{
	return vec3(-std::cos(yaw), 0.0f, std::sin(yaw));
}

void Camera::setTarget(vec3 target)
{
	setDirection(target - position);
}

void Camera::setView(vec3 eye, vec3 targetPosition)
{
	position = eye;
	setDirection(targetPosition - eye);
}

void Camera::setTarget(vec3 targetPosition, vec3 targetDirection)
{
	vec3 targetDistance = normalize(targetDirection) * (-3.0f) - vec3(0.0f, -2.5f, 0.0f);
	mat4 translationMatrix = translate(mat4(1.0f), targetDistance);
	position = vec3(translationMatrix * vec4(targetPosition, 1.0f));
	setDirection(targetPosition - position);
}

mat4 Camera::getViewMatrix()
{
	return lookAt(position, position + direction, up);
}

mat4 Camera::getProjectionMatrix()
{
	return perspective(radians(aperture), //OEffnungswinkel der Kamera
		getAspectRatio(),
		zNear,
		zFar);
}

ViewFrustum Camera::getViewFrustum()
{
	ViewFrustum frustum;
	frustum.setCamInternals(aperture, getAspectRatio(), zNear, zFar);
	frustum.setCamDef(position, position + direction, up);
	return frustum;
}

float Camera::getAspectRatio() const
{
	return static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
}

void Camera::setViewportSize(int width, int height)
{
	if (width > 0 && height > 0)
	{
		windowWidth = width;
		windowHeight = height;
	}
}

bool Camera::isLocked()
{
	return locked;
}

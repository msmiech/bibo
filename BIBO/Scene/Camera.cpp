#include "Camera.hpp"

using namespace bibo;
using namespace scene;

double previousX;
double previousY;

Camera::Camera(vec3 position, vec3 direction, vec3 up, Shader* shader, GLFWwindow& window) : SceneObject(window),
shader(shader),
position(position),
direction(direction),
up(up)
{
	glfwGetWindowSize(&window, &windowWidth, &windowHeight);

}

Camera::~Camera()
{
}


void Camera::onMouseMoved(double mouseX, double mouseY, float time_delta)
{
	auto diffX = previousX < 0 ? 0 : mouseX - previousX;
	auto diffY = previousY < 0 ? 0 : previousY - mouseY; //y-axis is inverted
	previousX = mouseX;
	previousY = mouseY;

	horizontalAngle += diffX * time_delta;
	verticalAngle += diffY * time_delta;


	if (verticalAngle > 89.0f)
		verticalAngle = 89.0f;
	if (verticalAngle < -89.0f)
		verticalAngle = -89.0f;

	vec3 direction(
		cos(verticalAngle) * sin(horizontalAngle),
		sin(verticalAngle),
		cos(verticalAngle) * cos(horizontalAngle)
		);

	this->direction = normalize(direction);
	this->right = normalize(cross(direction, up));
	//this->up = normalize(cross(direction, right));
}

void Camera::update(float time_delta)
{
	if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
		locked = !locked;
	}
	if (!locked)
	{
		auto velocity = speed * time_delta;
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		{
			position += direction * velocity;
		}
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		{
			position -= direction * velocity;
		}
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		{
			position -= right * velocity;
		}
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		{
			position += right * velocity;
		}
		double mouseX = 0;
		double mouseY = 0;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
		{
			onMouseMoved(mouseX, mouseY, time_delta);
		}
		else {
			previousX = -1;
			previousY = -1;
		}
	}
}

void Camera::setTarget(vec3 target)
{
	direction = normalize(target - position);
}

void Camera::setTarget(vec3 targetPosition, vec3 targetDirection)
{
	vec3 targetDistance = normalize(targetDirection) * (-3.0f) - vec3(0.0f, -2.5f, 0.0f);
	mat4 translationMatrix = translate(mat4(1.0f), targetDistance);
	position = vec3(translationMatrix * vec4(targetPosition, 1.0f));
	direction = normalize(targetPosition - position);
}

void Camera::setTarget(SceneObject* targetObject)
{
	vec3 targetPosition = targetObject->getPosition();
	vec3 targetDirection = targetObject->getDirection();
	this->direction = normalize(targetPosition + 3.f * targetDirection - position);

	mat4 targetModelMatrix = targetObject->modelMatrix;
	mat4 translationMatrix = translate(targetModelMatrix, vec3(-1.0f, 6.0f, 4.0f)); //This translation is adapted to collada/rootnode patch

	position = vec3(translationMatrix * vec4(1.0f));
}

mat4 Camera::getViewMatrix()
{
	return lookAt(position, position + direction, up);
}

mat4 Camera::getProjectionMatrix()
{
	return perspective(radians(aperture), //OEffnungswinkel der Kamera
		static_cast<float>(windowWidth) / static_cast<float>(windowHeight), //screen ratio
		zNear,
		zFar);
}

bool Camera::isLocked()
{
	return locked;
}

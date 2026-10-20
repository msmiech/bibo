#include "Input.hpp"
#include <algorithm>
#include <bitset>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace bibo;
using namespace input;

namespace
{
	//Sticks: dead zones of the movement and the camera stick
	const float MOVE_DEAD_ZONE = 0.15f, LOOK_DEAD_ZONE = 0.12f, STICK_MAX = 0.95f;
	//Camera speed at sensitivity 5 (the middle of 1 to 10): radians per pixel of the mouse, radians per second of the stick
	const float MOUSE_RADIANS_PER_PIXEL = 0.0025f;
	const float STICK_YAW_RATE = 3.2f, STICK_PITCH_RATE = 2.0f;
	//The left stick selects the next entry of a menu when it is pushed this far
	const float MENU_STICK_THRESHOLD = 0.6f;

	//Pressed by the demo script
	std::bitset<GLFW_KEY_LAST + 1> scriptedKeys;

	bool isValidKey(int key)
	{
		return key >= 0 && key <= GLFW_KEY_LAST;
	}
}

const char* const Input::MAPPINGS_FILE = "gamecontrollerdb.txt";

bool input::isKeyDown(GLFWwindow* window, int key)
{
	return (isValidKey(key) && scriptedKeys[key]) || glfwGetKey(window, key) == GLFW_PRESS;
}

void input::setScriptedKey(int key, bool down)
{
	if (isValidKey(key))
	{
		scriptedKeys[key] = down;
	}
}

glm::vec2 input::applyDeadZone(glm::vec2 stick, float inner, float outer)
{
	float length = glm::length(stick);
	if (length <= inner || length <= 0.0f)
	{
		return glm::vec2(0.0f);
	}
	float scaled = std::min((length - inner) / (outer - inner), 1.0f);
	return stick * (scaled / length);
}

glm::vec3 input::cameraRelative(glm::vec2 move, glm::vec3 forward, glm::vec3 right)
{
	glm::vec3 result = right * move.x + forward * move.y;
	result.y = 0.0f;
	float length = glm::length(result);
	float wanted = std::min(glm::length(move), 1.0f);
	return length > 1e-6f ? result * (wanted / length) : glm::vec3(0.0f);
}

Input::Input(GLFWwindow* window) : window(window)
{
	//GLFW knows most gamepads (including DualSense and Xbox Series), newer mappings can be added with this file
	std::ifstream file(MAPPINGS_FILE);
	if (file)
	{
		std::stringstream mappings;
		mappings << file.rdbuf();
		if (glfwUpdateGamepadMappings(mappings.str().c_str()) == GLFW_TRUE)
		{
			std::cout << "Gamepad mappings loaded from " << MAPPINGS_FILE << std::endl;
		}
	}
	if (glfwRawMouseMotionSupported())
	{
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); //only used while the cursor is disabled
	}
}

void Input::update()
{
	//The first connected gamepad is used, it may be connected or disconnected at any time
	if (gamepad < 0 || !glfwJoystickIsGamepad(gamepad))
	{
		gamepad = -1;
		for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick)
		{
			if (glfwJoystickIsGamepad(joystick))
			{
				gamepad = joystick;
				break;
			}
		}
		state = previousState = GLFWgamepadstate{};
	}
	previousState = state;
	if (gamepad < 0 || !glfwGetGamepadState(gamepad, &state))
	{
		state = GLFWgamepadstate{};
	}

	menuKeys.clear();
	if (wasPressed(GLFW_GAMEPAD_BUTTON_DPAD_UP)) menuKeys.push_back(GLFW_KEY_UP);
	if (wasPressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN)) menuKeys.push_back(GLFW_KEY_DOWN);
	if (wasPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)) menuKeys.push_back(GLFW_KEY_LEFT);
	if (wasPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)) menuKeys.push_back(GLFW_KEY_RIGHT);
	//The left stick works like the D-pad: one step each time it is pushed
	float x = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], y = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
	glm::ivec2 direction(x > MENU_STICK_THRESHOLD ? 1 : (x < -MENU_STICK_THRESHOLD ? -1 : 0), y > MENU_STICK_THRESHOLD ? 1 : (y < -MENU_STICK_THRESHOLD ? -1 : 0));
	if (direction.y != 0 && direction.y != stickDirection.y) menuKeys.push_back(direction.y < 0 ? GLFW_KEY_UP : GLFW_KEY_DOWN);
	if (direction.x != 0 && direction.x != stickDirection.x) menuKeys.push_back(direction.x < 0 ? GLFW_KEY_LEFT : GLFW_KEY_RIGHT);
	stickDirection = direction;
	if (wasPressed(GLFW_GAMEPAD_BUTTON_A)) menuKeys.push_back(GLFW_KEY_ENTER);
	if (wasPressed(GLFW_GAMEPAD_BUTTON_B)) menuKeys.push_back(GLFW_KEY_ESCAPE);
}

bool Input::wasPressed(int button) const
{
	return state.buttons[button] == GLFW_PRESS && previousState.buttons[button] != GLFW_PRESS;
}

std::string Input::getGamepadName() const
{
	const char* name = gamepad >= 0 ? glfwGetGamepadName(gamepad) : nullptr;
	return name ? name : "";
}

PlayerInput Input::playerInput(float timeDelta, const Settings& settings, bool freeCamera)
{
	PlayerInput result;
	float sensitivity = std::clamp(settings.cameraSensitivity, 1, 10) / 5.0f;
	float invert = settings.invertCameraY ? -1.0f : 1.0f;

	//Keyboard: the arrow keys always move the bear, WASD only if it does not move the free camera
	glm::vec2 keys(0.0f);
	keys.x = (isKeyDown(window, GLFW_KEY_RIGHT) ? 1.0f : 0.0f) - (isKeyDown(window, GLFW_KEY_LEFT) ? 1.0f : 0.0f);
	keys.y = (isKeyDown(window, GLFW_KEY_UP) ? 1.0f : 0.0f) - (isKeyDown(window, GLFW_KEY_DOWN) ? 1.0f : 0.0f);
	if (!freeCamera)
	{
		keys.x += (isKeyDown(window, GLFW_KEY_D) ? 1.0f : 0.0f) - (isKeyDown(window, GLFW_KEY_A) ? 1.0f : 0.0f);
		keys.y += (isKeyDown(window, GLFW_KEY_W) ? 1.0f : 0.0f) - (isKeyDown(window, GLFW_KEY_S) ? 1.0f : 0.0f);
	}
	keys = glm::clamp(keys, -1.0f, 1.0f);
	if (glm::length(keys) > 1.0f)
	{
		keys = glm::normalize(keys); //diagonal
	}
	result.jump = isKeyDown(window, GLFW_KEY_SPACE);

	//Mouse
	if (mouseCaptured)
	{
		result.look = mouseDelta * (MOUSE_RADIANS_PER_PIXEL * sensitivity);
		result.look.y *= invert;
	}
	mouseDelta = glm::vec2(0.0f);
	result.zoom = scroll;
	scroll = 0.0f;
	bool middleButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
	result.recenter = middleButton && !middleButtonWasPressed && mouseCaptured;
	middleButtonWasPressed = middleButton;

	//Gamepad (stick up is negative)
	glm::vec2 stick(0.0f);
	if (gamepad >= 0)
	{
		stick = applyDeadZone(glm::vec2(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], -state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]), MOVE_DEAD_ZONE, STICK_MAX);
		glm::vec2 look = applyDeadZone(glm::vec2(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]), LOOK_DEAD_ZONE, STICK_MAX);
		//Quadratic response: small movements of the stick turn the camera slowly and precisely
		look *= glm::length(look);
		result.look += glm::vec2(look.x * STICK_YAW_RATE, look.y * STICK_PITCH_RATE * invert) * (sensitivity * timeDelta);
		result.jump = result.jump || state.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS;
		result.recenter = result.recenter || wasPressed(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
	}
	result.move = glm::length(stick) > glm::length(keys) ? stick : keys;
	if (jumpBlocked)
	{
		jumpBlocked = result.jump;
		result.jump = false;
	}
	return result;
}

void Input::setMouseCaptured(bool captured)
{
	if (captured == mouseCaptured)
	{
		return;
	}
	mouseCaptured = captured;
	glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	//The cursor jumps when it is captured or released, that is not a movement
	hasCursor = false;
	mouseDelta = glm::vec2(0.0f);
}

void Input::onCursorPos(double x, double y)
{
	glm::dvec2 position(x, y);
	if (mouseCaptured && hasCursor)
	{
		mouseDelta += glm::vec2(position - cursor);
	}
	cursor = position;
	hasCursor = true;
}

void Input::onScroll(double yOffset)
{
	if (mouseCaptured)
	{
		scroll += static_cast<float>(yOffset);
	}
}

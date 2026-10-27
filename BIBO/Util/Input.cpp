#include "Input.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace bibo::input {

namespace {
// Sticks: dead zones of the movement and the camera stick
const float kMoveDeadZone = 0.15f, kLookDeadZone = 0.12f, kStickMax = 0.95f;
// Camera speed at sensitivity 5 (the middle of 1 to 10): radians per pixel of
// the mouse, radians per second of the stick
const float kMouseRadiansPerPixel = 0.0025f;
const float kStickYawRate = 3.2f, kStickPitchRate = 2.0f;
// The left stick selects the next entry of a menu when it is pushed this far
const float kMenuStickThreshold = 0.6f;

// Pressed by the demo script
std::bitset<GLFW_KEY_LAST + 1> scripted_keys;

bool IsValidKey(int key) { return key >= 0 && key <= GLFW_KEY_LAST; }
}  // namespace

const char* const Input::kMappingsFile = "gamecontrollerdb.txt";

bool IsKeyDown(GLFWwindow* window, int key) {
  return (IsValidKey(key) && scripted_keys[key]) ||
         glfwGetKey(window, key) == GLFW_PRESS;
}

void SetScriptedKey(int key, bool down) {
  if (IsValidKey(key)) {
    scripted_keys[key] = down;
  }
}

glm::vec2 ApplyDeadZone(glm::vec2 stick, float inner, float outer) {
  float length = glm::length(stick);
  if (length <= inner || length <= 0.0f) {
    return glm::vec2(0.0f);
  }
  float scaled = std::min((length - inner) / (outer - inner), 1.0f);
  return stick * (scaled / length);
}

glm::vec3 CameraRelative(glm::vec2 move, glm::vec3 forward, glm::vec3 right) {
  glm::vec3 result = right * move.x + forward * move.y;
  result.y = 0.0f;
  float length = glm::length(result);
  float wanted = std::min(glm::length(move), 1.0f);
  return length > 1e-6f ? result * (wanted / length) : glm::vec3(0.0f);
}

Input::Input(GLFWwindow* window) : window_(window) {
  // GLFW knows most gamepads (including DualSense and Xbox Series), newer
  // mappings can be added with this file
  std::ifstream file(kMappingsFile);
  if (file) {
    std::stringstream mappings;
    mappings << file.rdbuf();
    if (glfwUpdateGamepadMappings(mappings.str().c_str()) == GLFW_TRUE) {
      std::cout << "Gamepad mappings loaded from " << kMappingsFile
                << std::endl;
    }
  }
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION,
                     GLFW_TRUE);  // only used while the cursor is disabled
  }
}

void Input::Update() {
  // The first connected gamepad is used, it may be connected or disconnected at
  // any time
  if (gamepad_ < 0 || !glfwJoystickIsGamepad(gamepad_)) {
    gamepad_ = -1;
    for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST;
         ++joystick) {
      if (glfwJoystickIsGamepad(joystick)) {
        gamepad_ = joystick;
        break;
      }
    }
    state_ = previous_state_ = GLFWgamepadstate{};
  }
  previous_state_ = state_;
  if (gamepad_ < 0 || !glfwGetGamepadState(gamepad_, &state_)) {
    state_ = GLFWgamepadstate{};
  }

  menu_keys_.clear();
  if (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_UP))
    menu_keys_.push_back(GLFW_KEY_UP);
  if (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN))
    menu_keys_.push_back(GLFW_KEY_DOWN);
  if (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT))
    menu_keys_.push_back(GLFW_KEY_LEFT);
  if (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT))
    menu_keys_.push_back(GLFW_KEY_RIGHT);
  // The left stick works like the D-pad: one step each time it is pushed
  float x = state_.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
        y = state_.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
  glm::ivec2 direction(
      x > kMenuStickThreshold ? 1 : (x < -kMenuStickThreshold ? -1 : 0),
      y > kMenuStickThreshold ? 1 : (y < -kMenuStickThreshold ? -1 : 0));
  stick_pushed_ =
      glm::ivec2(direction.x != stick_direction_.x ? direction.x : 0,
                 direction.y != stick_direction_.y ? direction.y : 0);
  if (stick_pushed_.y != 0)
    menu_keys_.push_back(stick_pushed_.y < 0 ? GLFW_KEY_UP : GLFW_KEY_DOWN);
  if (stick_pushed_.x != 0)
    menu_keys_.push_back(stick_pushed_.x < 0 ? GLFW_KEY_LEFT : GLFW_KEY_RIGHT);
  stick_direction_ = direction;
  if (WasPressed(GLFW_GAMEPAD_BUTTON_A)) menu_keys_.push_back(GLFW_KEY_ENTER);
  if (WasPressed(GLFW_GAMEPAD_BUTTON_B)) menu_keys_.push_back(GLFW_KEY_ESCAPE);
}

bool Input::WasPressed(int button) const {
  return state_.buttons[button] == GLFW_PRESS &&
         previous_state_.buttons[button] != GLFW_PRESS;
}

bool Input::KeyPressed(int key) {
  bool down = IsKeyDown(window_, key);
  bool pressed = down && !keys_were_down_[key];
  keys_were_down_[key] = down;
  return pressed;
}

std::string Input::GetGamepadName() const {
  const char* name = gamepad_ >= 0 ? glfwGetGamepadName(gamepad_) : nullptr;
  return name ? name : "";
}

PlayerInput Input::ReadPlayerInput(float time_delta, const Settings& settings,
                                   bool free_camera) {
  PlayerInput result;
  float sensitivity = std::clamp(settings.camera_sensitivity, 1, 10) / 5.0f;
  float invert = settings.invert_camera_y ? -1.0f : 1.0f;

  // Keyboard: the arrow keys always move the bear, WASD only if it does not
  // move the free camera
  glm::vec2 keys(0.0f);
  keys.x = (IsKeyDown(window_, GLFW_KEY_RIGHT) ? 1.0f : 0.0f) -
           (IsKeyDown(window_, GLFW_KEY_LEFT) ? 1.0f : 0.0f);
  keys.y = (IsKeyDown(window_, GLFW_KEY_UP) ? 1.0f : 0.0f) -
           (IsKeyDown(window_, GLFW_KEY_DOWN) ? 1.0f : 0.0f);
  if (!free_camera) {
    keys.x += (IsKeyDown(window_, GLFW_KEY_D) ? 1.0f : 0.0f) -
              (IsKeyDown(window_, GLFW_KEY_A) ? 1.0f : 0.0f);
    keys.y += (IsKeyDown(window_, GLFW_KEY_W) ? 1.0f : 0.0f) -
              (IsKeyDown(window_, GLFW_KEY_S) ? 1.0f : 0.0f);
  }
  keys = glm::clamp(keys, -1.0f, 1.0f);
  if (glm::length(keys) > 1.0f) {
    keys = glm::normalize(keys);  // diagonal
  }
  result.jump = IsKeyDown(window_, GLFW_KEY_SPACE);

  // Mouse
  if (mouse_captured_) {
    result.look = mouse_delta_ * (kMouseRadiansPerPixel * sensitivity);
    result.look.y *= invert;
  }
  mouse_delta_ = glm::vec2(0.0f);
  result.zoom = scroll_;
  scroll_ = 0.0f;
  bool middle_button =
      glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
  result.recenter =
      middle_button && !middle_button_was_pressed_ && mouse_captured_;
  middle_button_was_pressed_ = middle_button;

  // Gamepad (stick up is negative)
  glm::vec2 stick(0.0f);
  if (gamepad_ >= 0) {
    stick = ApplyDeadZone(glm::vec2(state_.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
                                    -state_.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]),
                          kMoveDeadZone, kStickMax);
    glm::vec2 look =
        ApplyDeadZone(glm::vec2(state_.axes[GLFW_GAMEPAD_AXIS_RIGHT_X],
                                state_.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]),
                      kLookDeadZone, kStickMax);
    // Quadratic response: small movements of the stick turn the camera slowly
    // and precisely
    look *= glm::length(look);
    result.look +=
        glm::vec2(look.x * kStickYawRate, look.y * kStickPitchRate * invert) *
        (sensitivity * time_delta);
    result.jump =
        result.jump || state_.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS;
    result.recenter =
        result.recenter || WasPressed(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
  }
  result.move = glm::length(stick) > glm::length(keys) ? stick : keys;

  // Conversations: every key once per press (all of them are checked, so that
  // none is pressed by an old state)
  bool talk = KeyPressed(GLFW_KEY_E);
  bool enter = KeyPressed(GLFW_KEY_ENTER);
  enter = KeyPressed(GLFW_KEY_KP_ENTER) || enter;
  bool space = KeyPressed(GLFW_KEY_SPACE);
  int previous =
      (KeyPressed(GLFW_KEY_LEFT) ? 1 : 0) + (KeyPressed(GLFW_KEY_UP) ? 1 : 0) +
      (KeyPressed(GLFW_KEY_A) ? 1 : 0) + (KeyPressed(GLFW_KEY_W) ? 1 : 0);
  int next = (KeyPressed(GLFW_KEY_RIGHT) ? 1 : 0) +
             (KeyPressed(GLFW_KEY_DOWN) ? 1 : 0) +
             (KeyPressed(GLFW_KEY_D) ? 1 : 0) +
             (KeyPressed(GLFW_KEY_S) ? 1 : 0);
  result.cancel = KeyPressed(GLFW_KEY_BACKSPACE);
  if (gamepad_ >= 0) {
    talk = talk || WasPressed(GLFW_GAMEPAD_BUTTON_X);
    enter = enter || WasPressed(GLFW_GAMEPAD_BUTTON_A);
    previous += (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT) ? 1 : 0) +
                (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0) +
                (stick_pushed_.x < 0 || stick_pushed_.y < 0 ? 1 : 0);
    next += (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 : 0) +
            (WasPressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : 0) +
            (stick_pushed_.x > 0 || stick_pushed_.y > 0 ? 1 : 0);
    result.cancel = result.cancel || WasPressed(GLFW_GAMEPAD_BUTTON_B);
  }
  result.interact = talk;
  result.confirm = talk || enter || space;
  result.navigate =
      next > 0 && previous == 0 ? 1 : (previous > 0 && next == 0 ? -1 : 0);
  if (jump_blocked_) {
    jump_blocked_ = result.jump;
    result.jump = false;
  }
  return result;
}

void Input::SetMouseCaptured(bool captured) {
  if (captured == mouse_captured_) {
    return;
  }
  mouse_captured_ = captured;
  glfwSetInputMode(window_, GLFW_CURSOR,
                   captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  // The cursor jumps when it is captured or released, that is not a movement
  has_cursor_ = false;
  mouse_delta_ = glm::vec2(0.0f);
}

void Input::OnCursorPos(double x, double y) {
  glm::dvec2 position(x, y);
  if (mouse_captured_ && has_cursor_) {
    mouse_delta_ += glm::vec2(position - cursor_);
  }
  cursor_ = position;
  has_cursor_ = true;
}

void Input::OnScroll(double y_offset) {
  if (mouse_captured_) {
    scroll_ += static_cast<float>(y_offset);
  }
}

}  // namespace bibo::input

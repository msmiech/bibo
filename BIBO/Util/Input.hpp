#pragma once
#include <GLFW/glfw3.h>

#include <bitset>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Settings.hpp"

namespace bibo {
namespace input {
/**
 * What the player wants to do in this frame, from the keyboard, the mouse and
 * the gamepad
 */
struct PlayerInput {
  glm::vec2 move = glm::vec2(
      0.0f);  // relative to the camera: x right, y forward, length 0 to 1
  glm::vec2 look = glm::vec2(
      0.0f);  // camera rotation in this frame (radians): x to the right, y down
  float zoom =
      0.0f;  // camera closer (positive) or further away, in mouse wheel steps
  bool jump = false;      // held
  bool recenter = false;  // camera back behind the bear (pressed in this frame)
  // Pressed in this frame:
  bool interact = false;  // talk to an animal (E, X / Square)
  bool confirm = false;   // go on in a conversation, choose the answer (E,
                          // Enter, Space, A / Cross, X / Square)
  int navigate = 0;  // select the previous (-1) or next (1) answer (arrow keys,
                     // WASD, D-pad, left stick)
  bool cancel = false;  // end a conversation (Backspace, B / Circle)
};

/**
 * Radial dead zone of an analog stick: positions within inner count as the rest
 * position (sticks never rest exactly at 0), positions beyond outer as fully
 * pushed, the range in between is stretched to 0..1 (keeps the direction)
 */
glm::vec2 applyDeadZone(glm::vec2 stick, float inner, float outer);
/**
 * Direction in world space for a movement relative to the camera
 * @param move x: right, y: forward
 * @param forward, right Horizontal directions of the camera
 */
glm::vec3 cameraRelative(glm::vec2 move, glm::vec3 forward, glm::vec3 right);

/**
 * Replaces glfwGetKey for the keys that are held (walking, jumping, the
 * camera): a key is down when it is pressed on the keyboard or by the demo
 * script (see DemoScript)
 */
bool isKeyDown(GLFWwindow* window, int key);
/**
 * Presses or releases a key for isKeyDown, e.g. by the demo script
 */
void setScriptedKey(int key, bool down);

/**
 * Keyboard, mouse and gamepads (e.g. Sony DualSense, Xbox Series) via GLFW.
 * Gamepads use the GLFW mappings (SDL_GameControllerDB), so the buttons are
 * named like on an Xbox controller: A is Cross, B is Circle, Start is Options.
 * - Keyboard: WASD or arrow keys move the bear, SPACE jumps, E talks
 * - Mouse: moves the camera while it is captured (during the game), the wheel
 * zooms, the middle button recenters the camera
 * - Gamepad: left stick moves the bear, right stick moves the camera, A jumps,
 * X talks, the right stick button recenters the camera
 */
class Input {
 public:
  /**
   * Additional gamepad mappings (SDL_GameControllerDB format) are read from
   * this file in the working directory if it exists
   */
  static const char* const MAPPINGS_FILE;

  explicit Input(GLFWwindow* window);

  /**
   * Reads the gamepads (call once per frame, before the other functions)
   */
  void update();
  /**
   * @param freeCamera WASD moves the unlocked camera, not the bear
   */
  PlayerInput playerInput(float timeDelta, const Settings& settings,
                          bool freeCamera);
  /**
   * No jump until the jump buttons are released (e.g. A on the gamepad and
   * SPACE also press the buttons of the menus, the button that resumes the game
   * would make the bear jump right away)
   */
  void blockJumpUntilReleased() { jumpBlocked = true; }

  /**
   * The mouse turns the camera (cursor hidden and unlimited) or is a normal
   * cursor (menus)
   */
  void setMouseCaptured(bool captured);
  bool isMouseCaptured() const { return mouseCaptured; }
  // Forwarded from the GLFW callbacks
  void onCursorPos(double x, double y);
  void onScroll(double yOffset);

  /**
   * @return true if a gamepad is connected (the first one is used)
   */
  bool hasGamepad() const { return gamepad >= 0; }
  std::string getGamepadName() const;
  /**
   * @param button GLFW_GAMEPAD_BUTTON_...
   * @return true if the button of the gamepad was pressed since the last update
   */
  bool wasPressed(int button) const;
  /**
   * Keys for navigating the menus with the gamepad, pressed since the last
   * update: arrow keys (D-pad, left stick), ENTER (A) and ESCAPE (B)
   */
  const std::vector<int>& getMenuKeys() const { return menuKeys; }

 private:
  /**
   * @return true if the key is down now and was not in the last call
   */
  bool keyPressed(int key);

  GLFWwindow* window;
  int gamepad = -1;  // GLFW joystick id
  GLFWgamepadstate state{}, previousState{};
  std::vector<int> menuKeys;
  glm::ivec2 stickDirection =
      glm::ivec2(0);  // left stick pushed in a direction (for the menus)
  glm::ivec2 stickPushed =
      glm::ivec2(0);  // pushed in a direction since the last update
  std::bitset<GLFW_KEY_LAST + 1>
      keysWereDown;  // of the keys that only count when they are pressed

  bool mouseCaptured = false;
  bool hasCursor = false;
  glm::dvec2 cursor = glm::dvec2(0.0);
  glm::vec2 mouseDelta = glm::vec2(0.0f);  // pixels since the last playerInput
  float scroll = 0.0f;
  bool middleButtonWasPressed = false;
  bool jumpBlocked = false;
};
}  // namespace input
}  // namespace bibo

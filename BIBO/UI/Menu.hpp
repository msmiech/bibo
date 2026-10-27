#pragma once
#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Util/Settings.hpp"

namespace bibo {
namespace ui {
enum class MenuAction {
  Start,            // Main menu: start the game
  Resume,           // Pause menu: continue the game
  Quit,             // Main and pause menu: quit the game
  Restart,          // Pause menu: start again at the start
  MainMenu,         // Pause menu: back to the main menu
  GraphicsChanged,  // Settings menu: a graphics setting was changed (they are
                    // applied immediately)
  ApplyDisplay,   // Settings menu: a new window mode or resolution was applied
  SettingsClosed  // Settings menu: closed with Back or ESC
};

/**
 * What the HUD of the quests shows (see Menu::setHud)
 */
struct Hud {
  bool visible = false;
  // What the player can do here, e.g. "Talk to the beaver" with the key "E"
  // (empty: nothing)
  std::string prompt, promptKey;
  // A conversation: who talks (empty: no conversation), the text so far, the
  // answers to choose from
  std::string speaker, text;
  bool canContinue = false;  // the page is complete, continueKey goes on
  std::string continueKey;
  std::vector<std::string> choices;
  int selectedChoice = 0;
  // The countdown before a race: "3", "2", "1", "GO!" (empty: none)
  std::string countdown;
  // The race: its time, the checkpoints passed and the time limit
  bool racing = false;
  std::string raceTime, checkpoints, timeLimit;
  // Fishing for Mishka: the time, the salmon eaten of all ("2/6") and the
  // points
  bool fishing = false;
  std::string fishingTime, salmon, score;
  // The goal of the quest that is on (e.g. "Explore the stone circle on the
  // hill") and the way there ("120 m" with an arrow)
  std::string objective, objectiveDetail;
  // The result of the race or of fishing for a while after it ended (empty:
  // none)
  std::string banner, bannerDetail;
  bool bannerSuccess = false;

  bool operator==(const Hud&) const = default;
};

/**
 * Main menu, pause menu and settings menu of the game, rendered with RmlUi on
 * top of the scene, and the HUD of the quests (the conversations, the countdown
 * and the time of the race) under the menus. The menus are RML documents with
 * an RCSS style sheet (similar to HTML and CSS) in the UI folder. The settings
 * menu changes the Settings directly and reports the changes with a MenuAction.
 */
class Menu {
 public:
  /**
   * @param onAction Called from update() when a button was pressed or a setting
   * was changed
   */
  Menu(GLFWwindow* window, Settings& settings,
       std::function<void(MenuAction)> onAction);
  ~Menu();
  Menu(const Menu&) = delete;
  Menu& operator=(const Menu&) = delete;

  void showMainMenu();
  void showPauseMenu();
  void hide();
  /**
   * Updates the HUD (it is only drawn when it changed)
   */
  void setHud(const Hud& hud);
  /**
   * @return false if the HUD could not be loaded (e.g. missing UI folder)
   */
  bool hasHud() const;
  /**
   * Returns from the settings menu to the menu it was opened from (like its
   * Back button)
   * @return false if the settings menu was not open
   */
  bool back();
  bool isVisible() const;

  // Input, forwarded from the GLFW callbacks
  void onKey(int key, int action, int mods);
  void onChar(unsigned int codepoint);
  void onCursorPos(double x, double y);
  void onCursorEnter(bool entered);
  void onMouseButton(int button, int action, int mods);
  void onScroll(double yOffset);
  /**
   * Must be called when the framebuffer size (in pixels) changes. The menus are
   * scaled to its height.
   */
  void onFramebufferResized(int width, int height);

  void update();
  /**
   * Draws the HUD and the visible menu over the content of the default
   * framebuffer
   */
  void render();

 private:
  struct Impl;  // Keeps the RmlUi headers out of the game code
  std::unique_ptr<Impl> impl;
};
}  // namespace ui
}  // namespace bibo

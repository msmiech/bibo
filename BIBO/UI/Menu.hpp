#ifndef BIBO_UI_MENU_HPP_
#define BIBO_UI_MENU_HPP_
#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Util/Settings.hpp"

namespace bibo {
namespace ui {
enum class MenuAction {
  kStart,     // Main menu: start the game
  kResume,    // Pause menu: continue the game
  kQuit,      // Main and pause menu: quit the game
  kRestart,   // Pause menu: start again at the start
  kMainMenu,  // Pause menu: back to the main menu
  // Settings menu: a graphics setting was changed (they are applied
  // immediately)
  kGraphicsChanged,
  kApplyDisplay,   // Settings menu: a new window mode or resolution was applied
  kSettingsClosed  // Settings menu: closed with Back or ESC
};

/**
 * What the HUD of the quests shows (see Menu::SetHud)
 */
struct Hud {
  bool visible = false;
  // What the player can do here, e.g. "Talk to the beaver" with the key "E"
  // (empty: nothing)
  std::string prompt, prompt_key;
  // A conversation: who talks (empty: no conversation), the text so far, the
  // answers to choose from
  std::string speaker, text;
  bool can_continue = false;  // the page is complete, continue_key goes on
  std::string continue_key;
  std::vector<std::string> choices;
  int selected_choice = 0;
  // The countdown before a race: "3", "2", "1", "GO!" (empty: none)
  std::string countdown;
  // The race: its time, the checkpoints passed and the time limit
  bool racing = false;
  std::string race_time, checkpoints, time_limit;
  // Fishing for Mishka: the time, the salmon eaten of all ("2/6") and the
  // points
  bool fishing = false;
  std::string fishing_time, salmon, score;
  // The goal of the quest that is on (e.g. "Explore the stone circle on the
  // hill") and the way there ("120 m" with an arrow)
  std::string objective, objective_detail;
  // The result of the race or of fishing for a while after it ended (empty:
  // none)
  std::string banner, banner_detail;
  bool banner_success = false;

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
   * @param on_action Called from Update() when a button was pressed or a
   * setting was changed
   */
  Menu(GLFWwindow* window, Settings& settings,
       std::function<void(MenuAction)> on_action);
  ~Menu();
  Menu(const Menu&) = delete;
  Menu& operator=(const Menu&) = delete;

  void ShowMainMenu();
  void ShowPauseMenu();
  void Hide();
  /**
   * Updates the HUD (it is only drawn when it changed)
   */
  void SetHud(const Hud& hud);
  /**
   * @return false if the HUD could not be loaded (e.g. missing UI folder)
   */
  bool HasHud() const;
  /**
   * Returns from the settings menu to the menu it was opened from (like its
   * Back button)
   * @return false if the settings menu was not open
   */
  bool Back();
  bool IsVisible() const;

  // Input, forwarded from the GLFW callbacks
  void OnKey(int key, int action, int mods);
  void OnChar(unsigned int codepoint);
  void OnCursorPos(double x, double y);
  void OnCursorEnter(bool entered);
  void OnMouseButton(int button, int action, int mods);
  void OnScroll(double y_offset);
  /**
   * Must be called when the framebuffer size (in pixels) changes. The menus are
   * scaled to its height.
   */
  void OnFramebufferResized(int width, int height);

  void Update();
  /**
   * Draws the HUD and the visible menu over the content of the default
   * framebuffer
   */
  void Render();

 private:
  struct Impl;  // Keeps the RmlUi headers out of the game code
  std::unique_ptr<Impl> impl_;
};
}  // namespace ui
}  // namespace bibo

#endif  // BIBO_UI_MENU_HPP_

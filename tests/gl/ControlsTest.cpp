#include <GLFW/glfw3.h>

#include <catch2/catch_test_macros.hpp>

#include "TestSupport.hpp"
#include "Util/Input.hpp"
#include "Util/Settings.hpp"

using bibo::Settings;
using bibo::input::Input;
using bibo::input::PlayerInput;
using bibo::input::SetScriptedKey;
using bibo::test::GlContext;

TEST_CASE(
    "The button that closed a menu is not meant for the game: it does not "
    "answer a conversation and the bear does not jump",
    "[gl][input]") {
  GlContext gl;
  Input controls(&gl.Window());
  Settings settings;
  auto read = [&]() {
    controls.Update();
    return controls.ReadPlayerInput(1.0f / 60.0f, settings, false);
  };
  auto hold = [](bool down) {
    for (int key : {GLFW_KEY_ENTER, GLFW_KEY_SPACE, GLFW_KEY_E,
                    GLFW_KEY_BACKSPACE, GLFW_KEY_DOWN}) {
      SetScriptedKey(key, down);
    }
  };
  read();

  // The keys are down when the menu closes (e.g. ENTER on Resume while a
  // conversation is open)
  hold(true);
  controls.IgnoreMenuPress();
  PlayerInput resumed = read();
  CHECK_FALSE(resumed.confirm);
  CHECK_FALSE(resumed.interact);
  CHECK_FALSE(resumed.cancel);
  CHECK(resumed.navigate == 0);
  CHECK_FALSE(resumed.jump);
  // Still held: not pressed again
  PlayerInput held = read();
  CHECK_FALSE(held.confirm);
  CHECK_FALSE(held.cancel);
  CHECK(held.navigate == 0);
  CHECK_FALSE(held.jump);

  // Released and pressed again: they count
  hold(false);
  read();
  hold(true);
  PlayerInput pressed = read();
  hold(false);  // the scripted keys are shared by all tests
  CHECK(pressed.confirm);
  CHECK(pressed.interact);
  CHECK(pressed.cancel);
  CHECK(pressed.navigate == 1);
  CHECK(pressed.jump);
}

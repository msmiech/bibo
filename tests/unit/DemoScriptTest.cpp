#include <GLFW/glfw3.h>

#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "TestSupport.hpp"
#include "Util/DemoScript.hpp"

namespace test = bibo::test;
using bibo::DemoScript;

using Event = DemoScript::Event;

namespace {
DemoScript Parse(const std::string& text, int fps = 10) {
  DemoScript script;
  std::istringstream stream(text);
  std::string error;
  bool parsed = script.Parse(stream, fps, error);
  INFO(error);
  REQUIRE(parsed);
  return script;
}

std::string ParseError(const std::string& text) {
  DemoScript script;
  std::istringstream stream(text);
  std::string error;
  CHECK_FALSE(script.Parse(stream, 10, error));
  return error;
}

bool HasEvent(const DemoScript& script, int frame, Event::Type type,
              int key = GLFW_KEY_UNKNOWN) {
  for (const Event& event : script.EventsAt(frame)) {
    if (event.type == type && (type == Event::kQuit || event.key == key)) {
      return true;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("Key names of the demo script", "[demo]") {
  CHECK(DemoScript::KeyFromName("Up") == GLFW_KEY_UP);
  CHECK(DemoScript::KeyFromName("ENTER") == GLFW_KEY_ENTER);
  CHECK(DemoScript::KeyFromName("escape") == GLFW_KEY_ESCAPE);
  CHECK(DemoScript::KeyFromName("r") == GLFW_KEY_R);
  CHECK(DemoScript::KeyFromName("7") == GLFW_KEY_7);
  CHECK(DemoScript::KeyFromName("F1") == GLFW_KEY_F1);
  CHECK(DemoScript::KeyFromName("f12") == GLFW_KEY_F12);

  CHECK(DemoScript::KeyFromName("F13") == GLFW_KEY_UNKNOWN);
  CHECK(DemoScript::KeyFromName("F0") == GLFW_KEY_UNKNOWN);
  CHECK(DemoScript::KeyFromName("Fx") == GLFW_KEY_UNKNOWN);
  CHECK(DemoScript::KeyFromName("ctrl") == GLFW_KEY_UNKNOWN);
  CHECK(DemoScript::KeyFromName("") == GLFW_KEY_UNKNOWN);
}

TEST_CASE(
    "A pressed key is released in the next frame, the times are rounded to "
    "frames",
    "[demo]") {
  DemoScript script = Parse("press Enter\nwait 0.26\npress Escape # back\n");

  CHECK(HasEvent(script, 0, Event::kKeyDown, GLFW_KEY_ENTER));
  CHECK(HasEvent(script, 1, Event::kKeyUp, GLFW_KEY_ENTER));
  CHECK(HasEvent(script, 3, Event::kKeyDown, GLFW_KEY_ESCAPE));
  CHECK(HasEvent(script, 4, Event::kKeyUp, GLFW_KEY_ESCAPE));
  CHECK(script.EventsAt(2).empty());
}

TEST_CASE(
    "Held keys overlap the following commands and the game quits after they "
    "are released",
    "[demo]") {
  DemoScript script = Parse(
      "# walk and turn\n"
      "hold Up 2\n"
      "wait 0.5\n"
      "hold Left 0.5\n"
      "wait 0.5\n",
      10);

  CHECK(HasEvent(script, 0, Event::kKeyDown, GLFW_KEY_UP));
  CHECK(HasEvent(script, 5, Event::kKeyDown, GLFW_KEY_LEFT));
  CHECK(HasEvent(script, 10, Event::kKeyUp, GLFW_KEY_LEFT));
  CHECK(HasEvent(script, 20, Event::kKeyUp, GLFW_KEY_UP));
  CHECK(HasEvent(script, 20, Event::kQuit));
  CHECK(script.LastFrame() == 20);

  // The quit comes after the release in the same frame
  std::vector<Event> last_events = script.EventsAt(20);
  REQUIRE(last_events.size() == 2);
  CHECK(last_events.back().type == Event::kQuit);
}

TEST_CASE("quit ends the demo script", "[demo]") {
  DemoScript script = Parse("wait 1\nquit\npress Unknown\nwait 5\n");

  CHECK(HasEvent(script, 10, Event::kQuit));
  CHECK(script.LastFrame() == 10);
}

TEST_CASE("Errors in the demo script name the line", "[demo]") {
  CHECK(ParseError("wait 1\njump\n") == "line 2: unknown command 'jump'");
  CHECK(ParseError("press Shift") == "line 1: unknown key 'Shift'");
  CHECK(ParseError("press") == "line 1: unknown key ''");
  CHECK(ParseError("wait") == "line 1: wait needs the time in seconds");
  CHECK(ParseError("wait -1") == "line 1: wait needs the time in seconds");
  CHECK(ParseError("hold Up") ==
        "line 1: hold needs the key and the time in seconds");
  CHECK(ParseError("press Up 2") == "line 1: unexpected '2'");
}

TEST_CASE("The demo script of the README GIF is valid", "[demo][assets]") {
  DemoScript script;
  std::string error;
  bool loaded =
      script.Load(test::AssetPath("tools/readme_demo.txt").string(), 12, error);
  INFO(error);
  CHECK(loaded);
  CHECK(script.LastFrame() > 0);
}

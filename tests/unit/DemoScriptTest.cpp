#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <GLFW/glfw3.h>

#include "Util/DemoScript.hpp"
#include "TestSupport.hpp"

using namespace bibo;
using Event = DemoScript::Event;

namespace
{
	DemoScript parse(const std::string& text, int fps = 10)
	{
		DemoScript script;
		std::istringstream stream(text);
		std::string error;
		bool parsed = script.parse(stream, fps, error);
		INFO(error);
		REQUIRE(parsed);
		return script;
	}

	std::string parseError(const std::string& text)
	{
		DemoScript script;
		std::istringstream stream(text);
		std::string error;
		CHECK_FALSE(script.parse(stream, 10, error));
		return error;
	}

	bool hasEvent(const DemoScript& script, int frame, Event::Type type, int key = GLFW_KEY_UNKNOWN)
	{
		for (const Event& event : script.eventsAt(frame))
		{
			if (event.type == type && (type == Event::Quit || event.key == key))
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("Key names of the demo script", "[demo]")
{
	CHECK(DemoScript::keyFromName("Up") == GLFW_KEY_UP);
	CHECK(DemoScript::keyFromName("ENTER") == GLFW_KEY_ENTER);
	CHECK(DemoScript::keyFromName("escape") == GLFW_KEY_ESCAPE);
	CHECK(DemoScript::keyFromName("r") == GLFW_KEY_R);
	CHECK(DemoScript::keyFromName("7") == GLFW_KEY_7);
	CHECK(DemoScript::keyFromName("F1") == GLFW_KEY_F1);
	CHECK(DemoScript::keyFromName("f12") == GLFW_KEY_F12);

	CHECK(DemoScript::keyFromName("F13") == GLFW_KEY_UNKNOWN);
	CHECK(DemoScript::keyFromName("F0") == GLFW_KEY_UNKNOWN);
	CHECK(DemoScript::keyFromName("Fx") == GLFW_KEY_UNKNOWN);
	CHECK(DemoScript::keyFromName("ctrl") == GLFW_KEY_UNKNOWN);
	CHECK(DemoScript::keyFromName("") == GLFW_KEY_UNKNOWN);
}

TEST_CASE("A pressed key is released in the next frame, the times are rounded to frames", "[demo]")
{
	DemoScript script = parse("press Enter\nwait 0.26\npress Escape # back\n");

	CHECK(hasEvent(script, 0, Event::KeyDown, GLFW_KEY_ENTER));
	CHECK(hasEvent(script, 1, Event::KeyUp, GLFW_KEY_ENTER));
	CHECK(hasEvent(script, 3, Event::KeyDown, GLFW_KEY_ESCAPE));
	CHECK(hasEvent(script, 4, Event::KeyUp, GLFW_KEY_ESCAPE));
	CHECK(script.eventsAt(2).empty());
}

TEST_CASE("Held keys overlap the following commands and the game quits after they are released", "[demo]")
{
	DemoScript script = parse(
		"# walk and turn\n"
		"hold Up 2\n"
		"wait 0.5\n"
		"hold Left 0.5\n"
		"wait 0.5\n", 10);

	CHECK(hasEvent(script, 0, Event::KeyDown, GLFW_KEY_UP));
	CHECK(hasEvent(script, 5, Event::KeyDown, GLFW_KEY_LEFT));
	CHECK(hasEvent(script, 10, Event::KeyUp, GLFW_KEY_LEFT));
	CHECK(hasEvent(script, 20, Event::KeyUp, GLFW_KEY_UP));
	CHECK(hasEvent(script, 20, Event::Quit));
	CHECK(script.lastFrame() == 20);

	//The quit comes after the release in the same frame
	std::vector<Event> lastEvents = script.eventsAt(20);
	REQUIRE(lastEvents.size() == 2);
	CHECK(lastEvents.back().type == Event::Quit);
}

TEST_CASE("quit ends the demo script", "[demo]")
{
	DemoScript script = parse("wait 1\nquit\npress Unknown\nwait 5\n");

	CHECK(hasEvent(script, 10, Event::Quit));
	CHECK(script.lastFrame() == 10);
}

TEST_CASE("Errors in the demo script name the line", "[demo]")
{
	CHECK(parseError("wait 1\njump\n") == "line 2: unknown command 'jump'");
	CHECK(parseError("press Shift") == "line 1: unknown key 'Shift'");
	CHECK(parseError("press") == "line 1: unknown key ''");
	CHECK(parseError("wait") == "line 1: wait needs the time in seconds");
	CHECK(parseError("wait -1") == "line 1: wait needs the time in seconds");
	CHECK(parseError("hold Up") == "line 1: hold needs the key and the time in seconds");
	CHECK(parseError("press Up 2") == "line 1: unexpected '2'");
}

TEST_CASE("The demo script of the README GIF is valid", "[demo][assets]")
{
	DemoScript script;
	std::string error;
	bool loaded = script.load(test::assetPath("tools/readme_demo.txt").string(), 12, error);
	INFO(error);
	CHECK(loaded);
	CHECK(script.lastFrame() > 0);
}

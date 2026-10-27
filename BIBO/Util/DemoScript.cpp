#include "DemoScript.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace bibo {

using std::ifstream;
using std::istream;
using std::istringstream;
using std::map;
using std::max;
using std::string;
using std::to_string;
using std::vector;

namespace {
string ToLower(string text) {
  transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return text;
}

// Reads a duration in seconds and rounds it to frames
bool ReadFrames(istringstream& line, int fps, int& frames) {
  double seconds;
  if ((line >> seconds).fail() || !(seconds >= 0) || seconds > 3600) {
    return false;
  }
  frames = static_cast<int>(lround(seconds * fps));
  return true;
}
}  // namespace

int DemoScript::KeyFromName(const string& name) {
  static const map<string, int> kNamedKeys = {
      {"up", GLFW_KEY_UP},
      {"down", GLFW_KEY_DOWN},
      {"left", GLFW_KEY_LEFT},
      {"right", GLFW_KEY_RIGHT},
      {"space", GLFW_KEY_SPACE},
      {"enter", GLFW_KEY_ENTER},
      {"escape", GLFW_KEY_ESCAPE},
      {"tab", GLFW_KEY_TAB},
      {"backspace", GLFW_KEY_BACKSPACE}};
  string key = ToLower(name);
  auto named_key = kNamedKeys.find(key);
  if (named_key != kNamedKeys.end()) {
    return named_key->second;
  }
  if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
    return GLFW_KEY_A + (key[0] - 'a');
  }
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
    return GLFW_KEY_0 + (key[0] - '0');
  }
  if ((key.size() == 2 || key.size() == 3) && key[0] == 'f' &&
      isdigit(static_cast<unsigned char>(key[1])) &&
      (key.size() == 2 || isdigit(static_cast<unsigned char>(key[2])))) {
    int number = stoi(key.substr(1));
    if (number >= 1 && number <= 12) {
      return GLFW_KEY_F1 + (number - 1);
    }
  }
  return GLFW_KEY_UNKNOWN;
}

bool DemoScript::Parse(istream& script, int fps, string& error) {
  events_.clear();
  vector<Event> parsed;
  int frame = 0;  // of the next command
  string text;
  for (int line_number = 1; getline(script, text); ++line_number) {
    istringstream line(text.substr(0, text.find('#')));
    string command;
    if (!(line >> command)) {
      continue;  // empty line or comment
    }
    command = ToLower(command);
    auto fail = [&](const string& message) {
      error = "line " + to_string(line_number) + ": " + message;
      return false;
    };

    if (command == "quit") {
      break;
    } else if (command == "wait") {
      int frames;
      if (!ReadFrames(line, fps, frames)) {
        return fail("wait needs the time in seconds");
      }
      frame += frames;
    } else if (command == "press" || command == "hold") {
      string key_name;
      line >> key_name;
      int key = KeyFromName(key_name);
      if (key == GLFW_KEY_UNKNOWN) {
        return fail("unknown key '" + key_name + "'");
      }
      int frames = 1;
      if (command == "hold" && !ReadFrames(line, fps, frames)) {
        return fail("hold needs the key and the time in seconds");
      }
      // Released in a later frame, so that the game sees the key down at least
      // once
      parsed.push_back({frame, Event::kKeyDown, key});
      parsed.push_back({frame + max(frames, 1), Event::kKeyUp, key});
    } else {
      return fail("unknown command '" + command + "'");
    }

    string rest;
    if (line >> rest) {
      return fail("unexpected '" + rest + "'");
    }
  }

  // The game ends after the last key was released
  for (const Event& event : parsed) {
    frame = max(frame, event.frame);
  }
  parsed.push_back({frame, Event::kQuit, GLFW_KEY_UNKNOWN});
  stable_sort(parsed.begin(), parsed.end(),
              [](const Event& a, const Event& b) { return a.frame < b.frame; });
  events_ = parsed;
  return true;
}

bool DemoScript::Load(const string& path, int fps, string& error) {
  ifstream file(path);
  if (!file) {
    error = "could not open " + path;
    return false;
  }
  return Parse(file, fps, error);
}

vector<DemoScript::Event> DemoScript::EventsAt(int frame) const {
  auto by_frame = [](const Event& a, const Event& b) {
    return a.frame < b.frame;
  };
  auto range =
      equal_range(events_.begin(), events_.end(),
                  Event{frame, Event::kQuit, GLFW_KEY_UNKNOWN}, by_frame);
  return vector<Event>(range.first, range.second);
}

int DemoScript::LastFrame() const {
  return events_.empty() ? 0 : events_.back().frame;
}

}  // namespace bibo

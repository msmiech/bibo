#include "DemoScript.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

using namespace std;
using namespace bibo;

namespace {
string toLower(string text) {
  transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return text;
}

// Reads a duration in seconds and rounds it to frames
bool readFrames(istringstream& line, int fps, int& frames) {
  double seconds;
  if ((line >> seconds).fail() || !(seconds >= 0) || seconds > 3600) {
    return false;
  }
  frames = static_cast<int>(lround(seconds * fps));
  return true;
}
}  // namespace

int DemoScript::keyFromName(const string& name) {
  static const map<string, int> namedKeys = {{"up", GLFW_KEY_UP},
                                             {"down", GLFW_KEY_DOWN},
                                             {"left", GLFW_KEY_LEFT},
                                             {"right", GLFW_KEY_RIGHT},
                                             {"space", GLFW_KEY_SPACE},
                                             {"enter", GLFW_KEY_ENTER},
                                             {"escape", GLFW_KEY_ESCAPE},
                                             {"tab", GLFW_KEY_TAB},
                                             {"backspace", GLFW_KEY_BACKSPACE}};
  string key = toLower(name);
  auto namedKey = namedKeys.find(key);
  if (namedKey != namedKeys.end()) {
    return namedKey->second;
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

bool DemoScript::parse(istream& script, int fps, string& error) {
  events.clear();
  vector<Event> parsed;
  int frame = 0;  // of the next command
  string text;
  for (int lineNumber = 1; getline(script, text); ++lineNumber) {
    istringstream line(text.substr(0, text.find('#')));
    string command;
    if (!(line >> command)) {
      continue;  // empty line or comment
    }
    command = toLower(command);
    auto fail = [&](const string& message) {
      error = "line " + to_string(lineNumber) + ": " + message;
      return false;
    };

    if (command == "quit") {
      break;
    } else if (command == "wait") {
      int frames;
      if (!readFrames(line, fps, frames)) {
        return fail("wait needs the time in seconds");
      }
      frame += frames;
    } else if (command == "press" || command == "hold") {
      string keyName;
      line >> keyName;
      int key = keyFromName(keyName);
      if (key == GLFW_KEY_UNKNOWN) {
        return fail("unknown key '" + keyName + "'");
      }
      int frames = 1;
      if (command == "hold" && !readFrames(line, fps, frames)) {
        return fail("hold needs the key and the time in seconds");
      }
      // Released in a later frame, so that the game sees the key down at least
      // once
      parsed.push_back({frame, Event::KeyDown, key});
      parsed.push_back({frame + max(frames, 1), Event::KeyUp, key});
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
  parsed.push_back({frame, Event::Quit, GLFW_KEY_UNKNOWN});
  stable_sort(parsed.begin(), parsed.end(),
              [](const Event& a, const Event& b) { return a.frame < b.frame; });
  events = parsed;
  return true;
}

bool DemoScript::load(const string& path, int fps, string& error) {
  ifstream file(path);
  if (!file) {
    error = "could not open " + path;
    return false;
  }
  return parse(file, fps, error);
}

vector<DemoScript::Event> DemoScript::eventsAt(int frame) const {
  auto byFrame = [](const Event& a, const Event& b) {
    return a.frame < b.frame;
  };
  auto range =
      equal_range(events.begin(), events.end(),
                  Event{frame, Event::Quit, GLFW_KEY_UNKNOWN}, byFrame);
  return vector<Event>(range.first, range.second);
}

int DemoScript::lastFrame() const {
  return events.empty() ? 0 : events.back().frame;
}

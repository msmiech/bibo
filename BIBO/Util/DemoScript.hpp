#pragma once
#include <istream>
#include <string>
#include <vector>

namespace bibo {
/**
 * Input of the game played back frame by frame, e.g. to record the gameplay GIF
 * of the README (see tools/readme_demo.txt). A script has one command per line,
 * # starts a comment: wait <seconds>         the following commands happen
 * later press <key>            presses the key and releases it in the next
 * frame hold <key> <seconds>   holds the key down (the following commands
 * happen meanwhile, e.g. turning while walking) quit                   ends the
 * game (also at the end of the script) Keys: Up, Down, Left, Right, Space,
 * Enter, Escape, Tab, Backspace, F1 to F12, A to Z and 0 to 9 (case
 * insensitive). The times are rounded to frames, the game runs with a fixed
 * time step while it plays the script.
 */
class DemoScript {
 public:
  struct Event {
    enum Type { KeyDown, KeyUp, Quit };
    int frame;
    Type type;
    int key;  // GLFW key code (not used by Quit)
  };

  /**
   * @param fps Frames per second the times are converted to
   * @param error Line number and description of the first error
   * @return false if the script has an error (no events then)
   */
  bool parse(std::istream& script, int fps, std::string& error);
  bool load(const std::string& path, int fps, std::string& error);

  /**
   * @return The events of the frame (in the order of the script)
   */
  std::vector<Event> eventsAt(int frame) const;

  /**
   * @return The frame of the last event (the Quit at the end)
   */
  int lastFrame() const;

  /**
   * @return The GLFW key code or GLFW_KEY_UNKNOWN
   */
  static int keyFromName(const std::string& name);

 private:
  std::vector<Event> events;  // sorted by frame
};
}  // namespace bibo

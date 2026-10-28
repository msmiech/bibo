#ifndef BIBO_UTIL_SETTINGS_HPP_
#define BIBO_UTIL_SETTINGS_HPP_
#include <string>

namespace bibo {
enum WindowMode {
  kWindowed = 0,
  // Exclusive full screen, switches the monitor to the video mode of the
  // resolution
  kFullscreen = 1,
  // Window without border covering the monitor, keeps the resolution of the
  // desktop
  kBorderless = 2
};

/**
 * Display and graphics options of the game. Changed in the settings menu (some
 * also with the F-keys) and saved to a text file with one "name = value" line
 * per option.
 */
struct Settings {
  // Display
  int window_mode = kWindowed;
  int width = 800;  // Window size (windowed) or video mode (full screen)
  int height = 600;
  bool vsync = true;

  // Graphics
  bool hdr = true;
  bool bloom = true;  // requires HDR
  bool shadows = true;
  bool normal_mapping = true;
  bool transparency = true;
  bool bilinear_filtering = true;  // false = nearest neighbor
  int mipmapping = 2;              // 0 = off, 1 = nearest neighbor, 2 = linear
  bool wireframe = false;
  // 0 = low, 1 = medium, 2 = high: how far the details and the vegetation of
  // the world reach
  int world_detail = 2;

  // Controls
  int camera_sensitivity = 5;    // 1 (slow) to 10 (fast), mouse and gamepad
  bool invert_camera_y = false;  // pushing the mouse or stick up looks down

  // HUD
  bool show_frame_time = true;
  bool show_help = false;

  // Sound
  bool music = true;
  bool sound_effects = true;

  /**
   * Reads the settings written by Save(). Missing entries keep their current
   * value, invalid ones are ignored.
   * @return false if the file could not be opened (e.g. on the first start)
   */
  bool Load(const std::string& path);
  bool Save(const std::string& path) const;
};
}  // namespace bibo

#endif  // BIBO_UTIL_SETTINGS_HPP_

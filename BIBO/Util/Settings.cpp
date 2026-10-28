#include "Settings.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace bibo {

using std::endl;
using std::ifstream;
using std::map;
using std::ofstream;
using std::string;
using std::stringstream;

namespace {
const char* const kWindowModeNames[] = {"windowed", "fullscreen", "borderless"};

string Trim(const string& text) {
  auto begin = text.find_first_not_of(" \t\r");
  if (begin == string::npos) {
    return "";
  }
  auto end = text.find_last_not_of(" \t\r");
  return text.substr(begin, end - begin + 1);
}

void Read(const map<string, string>& values, const string& name, bool& value) {
  auto entry = values.find(name);
  if (entry == values.end()) {
    return;
  }
  if (entry->second == "true" || entry->second == "1") {
    value = true;
  } else if (entry->second == "false" || entry->second == "0") {
    value = false;
  }
}

void Read(const map<string, string>& values, const string& name, int& value,
          int min, int max) {
  auto entry = values.find(name);
  int parsed = 0;
  if (entry != values.end() &&
      !(stringstream(entry->second) >> parsed).fail() && parsed >= min &&
      parsed <= max) {
    value = parsed;
  }
}

const char* ToString(bool value) { return value ? "true" : "false"; }
}  // namespace

bool Settings::Load(const string& path) {
  ifstream file(path);
  if (!file) {
    return false;
  }

  map<string, string> values;
  string line;
  while (getline(file, line)) {
    auto separator = line.find('=');
    if (line.empty() || line[0] == '#' || separator == string::npos) {
      continue;
    }
    values[Trim(line.substr(0, separator))] = Trim(line.substr(separator + 1));
  }

  auto mode = values.find("window_mode");
  for (int i = kWindowed; mode != values.end() && i <= kBorderless; ++i) {
    if (mode->second == kWindowModeNames[i]) {
      window_mode = i;
    }
  }
  Read(values, "width", width, 320, 16384);
  Read(values, "height", height, 240, 16384);
  Read(values, "vsync", vsync);

  Read(values, "hdr", hdr);
  Read(values, "bloom", bloom);
  Read(values, "shadows", shadows);
  Read(values, "normal_mapping", normal_mapping);
  Read(values, "transparency", transparency);
  Read(values, "bilinear_filtering", bilinear_filtering);
  Read(values, "mipmapping", mipmapping, 0, 2);
  Read(values, "wireframe", wireframe);
  Read(values, "world_detail", world_detail, 0, 2);

  Read(values, "camera_sensitivity", camera_sensitivity, 1, 10);
  Read(values, "invert_camera_y", invert_camera_y);

  Read(values, "show_frame_time", show_frame_time);
  Read(values, "show_help", show_help);
  Read(values, "music", music);
  Read(values, "sound_effects", sound_effects);
  return true;
}

bool Settings::Save(const string& path) const {
  ofstream file(path);
  if (!file) {
    return false;
  }

  file
      << "# BIBO settings, written by the game when the settings menu is closed"
      << endl
      << "# window_mode: windowed, fullscreen or borderless; mipmapping: 0 = "
         "off, 1 = nearest neighbor, 2 = linear; world_detail: 0 = low, 1 = "
         "medium, 2 = high"
      << endl
      << "window_mode = "
      << kWindowModeNames[window_mode >= kWindowed && window_mode <= kBorderless
                              ? window_mode
                              : kWindowed]
      << endl
      << "width = " << width << endl
      << "height = " << height << endl
      << "vsync = " << ToString(vsync) << endl
      << "hdr = " << ToString(hdr) << endl
      << "bloom = " << ToString(bloom) << endl
      << "shadows = " << ToString(shadows) << endl
      << "normal_mapping = " << ToString(normal_mapping) << endl
      << "transparency = " << ToString(transparency) << endl
      << "bilinear_filtering = " << ToString(bilinear_filtering) << endl
      << "mipmapping = " << mipmapping << endl
      << "wireframe = " << ToString(wireframe) << endl
      << "world_detail = " << world_detail << endl
      << "camera_sensitivity = " << camera_sensitivity << endl
      << "invert_camera_y = " << ToString(invert_camera_y) << endl
      << "show_frame_time = " << ToString(show_frame_time) << endl
      << "show_help = " << ToString(show_help) << endl
      << "music = " << ToString(music) << endl
      << "sound_effects = " << ToString(sound_effects) << endl;
  return static_cast<bool>(file);
}

}  // namespace bibo

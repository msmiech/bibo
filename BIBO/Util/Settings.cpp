#include "Settings.hpp"
#include <fstream>
#include <map>
#include <sstream>

using namespace std;
using namespace bibo;

namespace
{
	const char* const windowModeNames[] = { "windowed", "fullscreen", "borderless" };

	string trim(const string& text)
	{
		auto begin = text.find_first_not_of(" \t\r");
		if (begin == string::npos)
		{
			return "";
		}
		auto end = text.find_last_not_of(" \t\r");
		return text.substr(begin, end - begin + 1);
	}

	void read(const map<string, string>& values, const string& name, bool& value)
	{
		auto entry = values.find(name);
		if (entry == values.end())
		{
			return;
		}
		if (entry->second == "true" || entry->second == "1")
		{
			value = true;
		}
		else if (entry->second == "false" || entry->second == "0")
		{
			value = false;
		}
	}

	void read(const map<string, string>& values, const string& name, int& value, int min, int max)
	{
		auto entry = values.find(name);
		int parsed = 0;
		if (entry != values.end() && !(stringstream(entry->second) >> parsed).fail() && parsed >= min && parsed <= max)
		{
			value = parsed;
		}
	}

	const char* toString(bool value)
	{
		return value ? "true" : "false";
	}
}

bool Settings::load(const string& path)
{
	ifstream file(path);
	if (!file)
	{
		return false;
	}

	map<string, string> values;
	string line;
	while (getline(file, line))
	{
		auto separator = line.find('=');
		if (line.empty() || line[0] == '#' || separator == string::npos)
		{
			continue;
		}
		values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
	}

	auto mode = values.find("window_mode");
	for (int i = WINDOWED; mode != values.end() && i <= BORDERLESS; ++i)
	{
		if (mode->second == windowModeNames[i])
		{
			windowMode = i;
		}
	}
	read(values, "width", width, 320, 16384);
	read(values, "height", height, 240, 16384);
	read(values, "vsync", vsync);

	read(values, "hdr", hdr);
	read(values, "bloom", bloom);
	read(values, "shadows", shadows);
	read(values, "normal_mapping", normalMapping);
	read(values, "transparency", transparency);
	read(values, "bilinear_filtering", bilinearFiltering);
	read(values, "mipmapping", mipmapping, 0, 2);
	read(values, "wireframe", wireframe);

	read(values, "camera_sensitivity", cameraSensitivity, 1, 10);
	read(values, "invert_camera_y", invertCameraY);

	read(values, "show_frame_time", showFrameTime);
	read(values, "show_help", showHelp);
	return true;
}

bool Settings::save(const string& path) const
{
	ofstream file(path);
	if (!file)
	{
		return false;
	}

	file << "# BIBO settings, written by the game when the settings menu is closed" << endl
		<< "# window_mode: windowed, fullscreen or borderless; mipmapping: 0 = off, 1 = nearest neighbor, 2 = linear" << endl
		<< "window_mode = " << windowModeNames[windowMode >= WINDOWED && windowMode <= BORDERLESS ? windowMode : WINDOWED] << endl
		<< "width = " << width << endl
		<< "height = " << height << endl
		<< "vsync = " << toString(vsync) << endl
		<< "hdr = " << toString(hdr) << endl
		<< "bloom = " << toString(bloom) << endl
		<< "shadows = " << toString(shadows) << endl
		<< "normal_mapping = " << toString(normalMapping) << endl
		<< "transparency = " << toString(transparency) << endl
		<< "bilinear_filtering = " << toString(bilinearFiltering) << endl
		<< "mipmapping = " << mipmapping << endl
		<< "wireframe = " << toString(wireframe) << endl
		<< "camera_sensitivity = " << cameraSensitivity << endl
		<< "invert_camera_y = " << toString(invertCameraY) << endl
		<< "show_frame_time = " << toString(showFrameTime) << endl
		<< "show_help = " << toString(showHelp) << endl;
	return static_cast<bool>(file);
}

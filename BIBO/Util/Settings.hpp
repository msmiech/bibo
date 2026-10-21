#pragma once
#include <string>

namespace bibo
{
	enum WindowMode
	{
		WINDOWED = 0,
		FULLSCREEN = 1, //Exclusive full screen, switches the monitor to the video mode of the resolution
		BORDERLESS = 2  //Window without border covering the monitor, keeps the resolution of the desktop
	};

	/**
	* Display and graphics options of the game. Changed in the settings menu (some also with the F-keys)
	* and saved to a text file with one "name = value" line per option.
	*/
	struct Settings
	{
		//Display
		int windowMode = WINDOWED;
		int width = 800; //Window size (windowed) or video mode (full screen)
		int height = 600;
		bool vsync = true;

		//Graphics
		bool hdr = true;
		bool bloom = true; //requires HDR
		bool shadows = true;
		bool normalMapping = true;
		bool transparency = true;
		bool bilinearFiltering = true; //false = nearest neighbor
		int mipmapping = 2; //0 = off, 1 = nearest neighbor, 2 = linear
		bool wireframe = false;
		int worldDetail = 2; //0 = low, 1 = medium, 2 = high: how far the details and the vegetation of the world reach

		//Controls
		int cameraSensitivity = 5; //1 (slow) to 10 (fast), mouse and gamepad
		bool invertCameraY = false; //pushing the mouse or stick up looks down

		//HUD
		bool showFrameTime = true;
		bool showHelp = false;

		//Sound
		bool music = true;
		bool soundEffects = true;

		/**
		* Reads the settings written by save(). Missing entries keep their current value, invalid ones are ignored.
		* @return false if the file could not be opened (e.g. on the first start)
		*/
		bool load(const std::string& path);
		bool save(const std::string& path) const;
	};
}

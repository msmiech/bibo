#pragma once
#include <vector>
#include <GLFW/glfw3.h>
#include "Settings.hpp"

namespace bibo
{
	namespace display
	{
		struct Resolution
		{
			int width;
			int height;

			bool operator==(const Resolution& other) const { return width == other.width && height == other.height; }
		};

		/**
		* @return The monitor of a full screen window, otherwise the monitor that contains the center of the window
		* (the primary monitor if no monitor contains it, nullptr if there is no monitor at all)
		*/
		GLFWmonitor* getMonitor(GLFWwindow* window);
		/**
		* @return The resolutions of the video modes of the monitor (without duplicates for different refresh rates), largest first
		*/
		std::vector<Resolution> getResolutions(GLFWmonitor* monitor);
		/**
		* Creates a window. The window hints (e.g. of the OpenGL context) must be set before.
		* @param windowMode WindowMode
		* @param width Window size (windowed) or video mode (full screen), ignored for borderless windows
		*/
		GLFWwindow* createWindow(int windowMode, int width, int height, const char* title);
		/**
		* Switches the window to another window mode and/or size, on the monitor it is currently on
		* @param windowMode WindowMode
		* @param width Window size (windowed) or video mode (full screen), ignored for borderless windows
		*/
		void applyWindowMode(GLFWwindow* window, int windowMode, int width, int height);
	}
}

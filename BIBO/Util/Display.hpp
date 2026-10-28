#ifndef BIBO_UTIL_DISPLAY_HPP_
#define BIBO_UTIL_DISPLAY_HPP_
#include <GLFW/glfw3.h>

#include <vector>

#include "Settings.hpp"

namespace bibo {
namespace display {
struct Resolution {
  int width;
  int height;

  bool operator==(const Resolution&) const = default;
};

/**
 * @return The monitor of a full screen window, otherwise the monitor that
 * contains the center of the window (the primary monitor if no monitor contains
 * it, nullptr if there is no monitor at all)
 */
GLFWmonitor* GetMonitor(GLFWwindow* window);
/**
 * @return The resolutions of the video modes of the monitor (without duplicates
 * for different refresh rates), largest first
 */
std::vector<Resolution> GetResolutions(GLFWmonitor* monitor);
/**
 * Creates a window. The window hints (e.g. of the OpenGL context) must be set
 * before.
 * @param window_mode WindowMode
 * @param width Window size (windowed) or video mode (full screen), ignored for
 * borderless windows
 */
GLFWwindow* CreateWindow(int window_mode, int width, int height,
                         const char* title);
/**
 * Switches the window to another window mode and/or size, on the monitor it is
 * currently on
 * @param window_mode WindowMode
 * @param width Window size (windowed) or video mode (full screen), ignored for
 * borderless windows
 */
void ApplyWindowMode(GLFWwindow* window, int window_mode, int width,
                     int height);
/**
 * Sets the icon of the window (title bar, task bar) from an image file, on the
 * platforms that let applications set it. On Windows the icon is a resource of
 * the executable (BIBO/bibo.rc) that GLFW uses for every window.
 */
void SetWindowIcon(GLFWwindow* window, const char* path);
}  // namespace display
}  // namespace bibo

#endif  // BIBO_UTIL_DISPLAY_HPP_

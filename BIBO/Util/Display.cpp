#include "Display.hpp"

#include <stb_image.h>

#include <algorithm>
#include <iostream>

using namespace std;
using namespace bibo;
using namespace display;

namespace {
bool isWayland() {
#if GLFW_VERSION_MAJOR > 3 || \
    (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
  return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
  return false;
#endif
}
}  // namespace

GLFWmonitor* display::getMonitor(GLFWwindow* window) {
  if (GLFWmonitor* fullscreenMonitor = glfwGetWindowMonitor(window)) {
    return fullscreenMonitor;
  }

  int x = 0, y = 0, width = 0, height = 0;
  glfwGetWindowPos(window, &x, &y);
  glfwGetWindowSize(window, &width, &height);
  int centerX = x + width / 2;
  int centerY = y + height / 2;

  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    int monitorX = 0, monitorY = 0;
    glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    if (mode && centerX >= monitorX && centerX < monitorX + mode->width &&
        centerY >= monitorY && centerY < monitorY + mode->height) {
      return monitors[i];
    }
  }
  return glfwGetPrimaryMonitor();
}

vector<Resolution> display::getResolutions(GLFWmonitor* monitor) {
  vector<Resolution> resolutions;
  int count = 0;
  const GLFWvidmode* modes =
      monitor ? glfwGetVideoModes(monitor, &count) : nullptr;
  for (int i = 0; i < count; ++i) {
    Resolution resolution = {modes[i].width, modes[i].height};
    // Smaller video modes are too small for the menus
    if (resolution.width >= 640 && resolution.height >= 480 &&
        find(resolutions.begin(), resolutions.end(), resolution) ==
            resolutions.end()) {
      resolutions.push_back(resolution);
    }
  }
  sort(resolutions.begin(), resolutions.end(),
       [](const Resolution& a, const Resolution& b) {
         return a.width != b.width ? a.width > b.width : a.height > b.height;
       });
  return resolutions;
}

GLFWwindow* display::createWindow(int windowMode, int width, int height,
                                  const char* title) {
  if (windowMode == FULLSCREEN) {
    // Without a monitor, the window is created windowed
    return glfwCreateWindow(width, height, title, glfwGetPrimaryMonitor(),
                            nullptr);
  }

  // Created hidden and shown after it was moved to its position (centered or
  // covering the monitor)
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  if (window) {
    applyWindowMode(window, windowMode, width, height);
    glfwShowWindow(window);
  }
  return window;
}

void display::applyWindowMode(GLFWwindow* window, int windowMode, int width,
                              int height) {
  GLFWmonitor* monitor = getMonitor(window);
  if (!monitor) {
    glfwSetWindowSize(window, width, height);
    return;
  }

  if (windowMode == FULLSCREEN) {
    // GLFW_DONT_CARE: the highest refresh rate of the resolution
    glfwSetWindowMonitor(window, monitor, 0, 0, width, height, GLFW_DONT_CARE);
    return;
  }

  // The decoration (title bar and border) is applied when the window leaves
  // full screen
  glfwSetWindowAttrib(window, GLFW_DECORATED,
                      windowMode == WINDOWED ? GLFW_TRUE : GLFW_FALSE);
  if (glfwGetWindowMonitor(window)) {
    // Leave full screen first, so that the monitor is back in the video mode of
    // the desktop
    glfwSetWindowMonitor(window, nullptr, 0, 0, width, height, GLFW_DONT_CARE);
  }

  int monitorX = 0, monitorY = 0;
  glfwGetMonitorPos(monitor, &monitorX, &monitorY);
  const GLFWvidmode* desktopMode = glfwGetVideoMode(monitor);

  if (windowMode == BORDERLESS) {
    if (isWayland()) {
      // Wayland neither lets windows position themselves nor change the video
      // mode, so full screen is borderless there
      glfwSetWindowMonitor(window, monitor, 0, 0, desktopMode->width,
                           desktopMode->height, desktopMode->refreshRate);
    } else {
      // Unlike a full screen window, it stays open behind other windows when
      // switching to them (e.g. Alt+Tab)
      glfwSetWindowMonitor(window, nullptr, monitorX, monitorY,
                           desktopMode->width, desktopMode->height,
                           GLFW_DONT_CARE);
    }
    return;
  }

  // Windowed: centered in the work area of the monitor (without the task bar),
  // including the title bar and border
  int areaX = 0, areaY = 0, areaWidth = 0, areaHeight = 0;
  glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaWidth, &areaHeight);
  glfwSetWindowSize(window, width, height);
  int left = 0, top = 0, right = 0, bottom = 0;
  glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
  glfwSetWindowPos(
      window, areaX + left + max(0, (areaWidth - left - right - width) / 2),
      areaY + top + max(0, (areaHeight - top - bottom - height) / 2));
}

void display::setWindowIcon(GLFWwindow* window, const char* path) {
#if defined(_WIN32) || defined(__APPLE__)
  // Windows: the icon resource of the executable, macOS: windows have no icons
  // (the icon of an application belongs to its bundle)
  (void)window;
  (void)path;
#else
  if (isWayland()) {
    // The compositor shows the icon of the desktop entry, applications cannot
    // set one
    return;
  }
  GLFWimage icon;
  int channels = 0;
  icon.pixels = stbi_load(path, &icon.width, &icon.height, &channels, 4);
  if (!icon.pixels) {
    std::cerr << "WARNING: Could not load the window icon " << path << " ("
              << stbi_failure_reason() << ")" << std::endl;
    return;
  }
  glfwSetWindowIcon(window, 1, &icon);
  stbi_image_free(icon.pixels);
#endif
}

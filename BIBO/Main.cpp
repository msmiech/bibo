#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Audio/MusicPlayer.hpp"
#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "UI/Menu.hpp"
#include "Util/Console.hpp"
#include "Util/DemoScript.hpp"
#include "Util/Display.hpp"
#include "Util/FrameRecorder.hpp"
#include "Util/Input.hpp"
#include "Util/Settings.hpp"

using namespace bibo;

#include <ft2build.h>
#include FT_FREETYPE_H

using namespace scene;

unique_ptr<physics::PhysicsWorld> physicsWorld;

void initialize();
void init(GLFWwindow* window);
void initPhysics();
void initTextRendering();
void updateTextProjection();
void initFreeType();
void initMenu();
void textRendering();
void initDebug();
void cleanup();
void draw();
void update(float time_delta);
void gameLoop();
void playDemoFrame(int frame);
void renderText(Shader* shader, const string& text, GLfloat x, GLfloat y,
                GLfloat scale, vec3 color);
void resizeScreenBuffers();
render::RenderSettings renderSettings();
void applyGraphicsSettings();
bool pauseGame();
void resumeGame();
void restartGame();
void playSounds(const vector<SalmonQuest::Event>& salmonEvents,
                const vector<StonesQuest::Event>& stonesEvents);
void updateHud();
void onMenuAction(ui::MenuAction action);
void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods);
void fKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                  int mode);
void framebufferSizeCallback(GLFWwindow* window, int width, int height);
void windowFocusCallback(GLFWwindow* window, int focused);
void joystickCallback(int joystick, int event);
void handleGamepad();

// vars
// in the working directory, like the assets
const char* const SETTINGS_FILE = "settings.cfg";
Settings settings;
unsigned int windowWidth = 800;  // Size of the framebuffer in pixels
unsigned int windowHeight = 600;
GLFWwindow* window;
float time_delta;  // Last frame time
// Longer frames (e.g. while the window is dragged) slow the game down instead
// of simulating a few huge steps
const float MAX_TIME_STEP = 0.1f;

// Demo (--demo) and recording (--record): the game runs with a fixed time step
// and the default settings, so that a script always plays the same way (e.g.
// tools/readme_demo.txt for the GIF of the README)
float fixedTimeStep = 0;  // 0: the time step is the duration of the last frame
bool useSavedSettings = true;
bool recording = false;  //--record
unique_ptr<DemoScript> demoScript;
unique_ptr<FrameRecorder> recorder;
bool recordingFailed = false;

enum GameState { MAIN_MENU, PLAYING, PAUSED };
GameState gameState = MAIN_MENU;
unique_ptr<ui::Menu> menu;
// Keyboard, mouse and gamepad of the player
unique_ptr<input::Input> controls;

unique_ptr<Shader> mainShader;
unique_ptr<Shader> textShader;

unique_ptr<Scene> gameScene;
// The music and the sounds (not while recording: the game runs faster or slower
// than real time then)
unique_ptr<audio::MusicPlayer> music;
// the beaver does not make the same sound twice in a row
unsigned int beaverSound = 0;
unsigned int bearSound = 0;     // neither does Mishka
unsigned int raccoonSound = 0;  // nor the raccoons
// Shadow maps, HDR image, bloom, auto exposure and tone mapping
unique_ptr<render::Renderer> renderer;

#ifdef _WIN32
// Ask laptops with switchable graphics to run the game on the dedicated
// Nvidia/AMD GPU
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
// Based on:
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering Holds
// all state information relevant to a character as loaded using FreeType
struct Character {
  GLuint textureID;  // ID handle of the glyph texture
  ivec2 size;        // Size of glyph
  ivec2 bearing;     // Offset from baseline to left/top of glyph
  GLuint advance;    // Horizontal offset to advance to next glyph
};
map<GLchar, Character> Characters;
GLuint textVAO;
GLuint textVBO;
string lastMessage = "";
float messageTimeLeft = 0;

int main(int argc, char** argv) {
  // Options anywhere on the command line: --demo <script> plays the input of
  // the script, --record <file> writes every frame to the file (raw video, see
  // FrameRecorder), --fps <frames per second> of both (default 15)
  vector<string> arguments;  // width height [fullscreen]
  string demoPath;
  string recordPath;
  int fps = 15;
  for (int i = 1; i < argc; ++i) {
    string argument = argv[i];
    bool hasValue = i + 1 < argc;
    if (argument == "--demo" && hasValue) {
      demoPath = argv[++i];
    } else if (argument == "--record" && hasValue) {
      recordPath = argv[++i];
    } else if (argument == "--fps" && hasValue) {
      if ((stringstream(argv[++i]) >> fps).fail() || fps <= 0 || fps > 1000) {
        util::fatalError("--fps needs the number of frames per second");
      }
    } else if (argument.starts_with("--")) {
      util::fatalError("Unknown option " + argument +
                       " (BIBO [--demo <script>] [--record <file>] [--fps "
                       "<frames per second>] [width height [fullscreen]])");
    } else {
      arguments.push_back(argument);
    }
  }

  recording = !recordPath.empty();
  if (!demoPath.empty() || recording) {
    fixedTimeStep = 1.0f / fps;
    useSavedSettings = false;
    // It would show the fixed time step instead of the time it takes to draw a
    // frame
    settings.showFrameTime = false;
  }
  if (!demoPath.empty()) {
    demoScript = make_unique<DemoScript>();
    string error;
    if (!demoScript->load(demoPath, fps, error)) {
      util::fatalError("Demo script " + demoPath + ": " + error);
    }
  }

  // Saved by the settings menu, the command line arguments take precedence
  if (useSavedSettings) {
    settings.load(SETTINGS_FILE);
  }

  if (arguments.size() >= 2) {
    cout << "You are running" << endl;

    if ((stringstream(arguments[0]) >> settings.width).fail() ||
        settings.width <= 0) {
      cerr << "ERROR: Could not parse first element, try again!" << endl;
      util::pause();
      exit(EXIT_FAILURE);
    }

    if ((stringstream(arguments[1]) >> settings.height).fail() ||
        settings.height <= 0) {
      cerr << "ERROR: Could not parse second element, try again!" << endl;
      util::pause();
      exit(EXIT_FAILURE);
    }

    if (arguments.size() >= 3) {
      bool fullscreen;
      if ((stringstream(arguments[2]) >> fullscreen).fail()) {
        cerr << "ERROR: Could not parse third command-line-arg, try again!"
             << endl;
        util::pause();
        exit(EXIT_FAILURE);
      }
      settings.windowMode = fullscreen ? FULLSCREEN : WINDOWED;
    }
  }

  cout << "Starting BIBO game" << endl;

  initialize();

  if (!recordPath.empty()) {
    recorder =
        make_unique<FrameRecorder>(recordPath, windowWidth, windowHeight);
    if (!recorder->isOpen()) {
      util::fatalError("Could not create " + recordPath);
    }
    cout << "Recording " << windowWidth << "x" << windowHeight << " pixels at "
         << fps << " fps to " << recordPath << endl;
  }

  gameLoop();

  if (recorder) {
    cout << "Recorded " << recorder->getFrameCount() << " frames" << endl;
    recorder.reset();
  }

  cleanup();

  glfwDestroyWindow(window);

  glfwTerminate();

  return recordingFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void initialize() {
  if (!glfwInit()) {
    cerr << "ERROR: Could not init glfw." << endl;
    util::pause();
    exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifndef NDEBUG
  // Debug output (see initDebug), the extra checks of a debug context can slow
  // down the driver
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // Windowed, full screen or borderless (see settings)
  window = display::createWindow(settings.windowMode, settings.width,
                                 settings.height, "BIBO Project");

  if (!window)  // window == nullptr
  {
    cerr << "ERROR: Could not create a window with an OpenGL 4.3 core context"
         << endl;
    glfwTerminate();
    util::pause();
    exit(EXIT_FAILURE);
  }
  display::setWindowIcon(window, "Icon/bibo.png");
  // The framebuffer can be smaller than requested (e.g. limited to the screen)
  // or larger (scaled on high DPI screens)
  int framebufferWidth, framebufferHeight;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  windowWidth = std::max(framebufferWidth, 1);
  windowHeight = std::max(framebufferHeight, 1);

  // Input goes to the game or, while it is visible, to the menu
  glfwSetKeyCallback(window, keyCallback);
  glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
    if (menu && menu->isVisible()) menu->onChar(codepoint);
  });
  glfwSetMouseButtonCallback(window,
                             [](GLFWwindow*, int button, int action, int mods) {
                               if (menu && menu->isVisible())
                                 menu->onMouseButton(button, action, mods);
                             });
  glfwSetScrollCallback(window, [](GLFWwindow*, double, double yOffset) {
    if (menu && menu->isVisible()) menu->onScroll(yOffset);
    // zooms the camera
    else if (controls)
      controls->onScroll(yOffset);
  });
  // The menu always knows where the mouse is, so that it highlights the right
  // button as soon as it opens. During the game the mouse is captured and turns
  // the camera.
  glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
    if (menu) menu->onCursorPos(x, y);
    if (controls) controls->onCursorPos(x, y);
  });
  glfwSetJoystickCallback(joystickCallback);
  glfwSetCursorEnterCallback(window, [](GLFWwindow*, int entered) {
    if (menu) menu->onCursorEnter(entered == GLFW_TRUE);
  });
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetWindowFocusCallback(window, windowFocusCallback);
  glfwMakeContextCurrent(window);

  // GLEW initialization
  glewExperimental = true;  // Important for loading in old OSes
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    cerr << "OpenGL Error: " << error << endl;
  }
  GLenum glewStatus = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
  // On Wayland there is no GLX display, but the OpenGL functions were loaded
  // nevertheless
  if (glewStatus == GLEW_ERROR_NO_GLX_DISPLAY) {
    glewStatus = GLEW_OK;
  }
#endif
  if (glewStatus != GLEW_OK) {
    cerr << "ERROR: Could not load glew: " << glewGetErrorString(glewStatus)
         << endl;
    glfwTerminate();
    util::pause();
    exit(EXIT_FAILURE);
  }
  if (glewIsSupported("GL_VERSION_4_3")) {
    cout << "GLEW: OpenGL Version 4.3 is supported!" << endl;
  } else {
    cerr << "GLEW: OpenGL Version is below 4.3! :(" << endl;
  }

  initDebug();

  initPhysics();

  init(window);
}

#ifndef NDEBUG
namespace {
const char* debugSourceName(GLenum source) {
  switch (source) {
    case GL_DEBUG_SOURCE_API:
      return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
      return "Window System";
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
      return "Shader Compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY:
      return "Third Party";
    case GL_DEBUG_SOURCE_APPLICATION:
      return "Application";
    default:
      return "Other";
  }
}

const char* debugTypeName(GLenum type) {
  switch (type) {
    case GL_DEBUG_TYPE_ERROR:
      return "Error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
      return "Deprecated Behavior";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
      return "Undefined Behavior";
    case GL_DEBUG_TYPE_PORTABILITY:
      return "Portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
      return "Performance";
    default:
      return "Other";
  }
}

const char* debugSeverityName(GLenum severity) {
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
      return "High";
    case GL_DEBUG_SEVERITY_MEDIUM:
      return "Medium";
    case GL_DEBUG_SEVERITY_LOW:
      return "Low";
    default:
      return "Unknown";
  }
}

void GLAPIENTRY debugCallback(GLenum source, GLenum type, GLuint id,
                              GLenum severity, GLsizei, const GLchar* message,
                              const void*) {
  // Notifications come e.g. where the driver keeps a buffer, for every chunk of
  // the streamed world
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
    return;
  }
  cout << "OpenGL DEBUG: " << message
       << " [Source = " << debugSourceName(source)
       << ", Type = " << debugTypeName(type)
       << ", Severity = " << debugSeverityName(severity) << ", ID = " << id
       << "]" << endl;
}
}  // namespace
#endif

void initDebug() {
#ifndef NDEBUG
  // Core in OpenGL 4.3, which the game needs. Synchronous: the callback runs
  // right where the error happens.
  if (glDebugMessageCallback != nullptr) {
    glDebugMessageCallback(debugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  }
#endif
}

void initShaders() {
  cout << "Initializing shaders..." << endl;
  // Objects of the track (the world, the bear and the post processing have
  // their own shaders)
  cout << "Main shader..." << endl;
  mainShader =
      make_unique<Shader>("Shader/main_shader.vert", "Shader/main_shader.frag");
  cout << "Text shader..." << endl;
  textShader =
      make_unique<Shader>("Shader/text_shader.vert", "Shader/text_shader.frag");

  mainShader->enable();
  glUniform1i(mainShader->getUniformLocation("colorTextureSampler"),
              render::TextureUnit::COLOR);
  glUniform1i(mainShader->getUniformLocation("normalMap"),
              render::TextureUnit::NORMAL_MAP);

  cout << "Renderer (shadow maps, HDR, bloom)..." << endl;
  renderer = make_unique<render::Renderer>(windowWidth, windowHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void init(GLFWwindow* window) {
  glEnable(GL_DEPTH_TEST);
  controls = make_unique<input::Input>(window);

  initShaders();
  initTextRendering();

  cout << "Loading assets from files via assimp and setting up scene..."
       << endl;
  if (!recording) {
    // synthesizes the music while the scene loads
    music = make_unique<audio::MusicPlayer>();
  }
  gameScene = make_unique<Scene>(window, mainShader.get(), physicsWorld.get());
  gameScene->setViewportSize(windowWidth, windowHeight);
  // The scene stands still behind the main menu. Simulating the first second
  // (without input) lets the bear land on the ground and the camera follow it,
  // as at the start of the game.
  for (int i = 0; i < 60; ++i) {
    gameScene->update(1.0f / 60.0f);
  }

  applyGraphicsSettings();
  initMenu();
}

void initMenu() {
  cout << "Initializing menus (RmlUi)..." << endl;
  menu = make_unique<ui::Menu>(window, settings, onMenuAction);
  menu->showMainMenu();
  if (!menu->isVisible()) {
    // Without menus (e.g. missing UI folder) the game starts right away, ESC
    // quits as in earlier versions
    gameState = PLAYING;
  }
}

// Applies the settings that are not read directly when drawing (like
// settings.hdr)
void applyGraphicsSettings() {
  glfwSwapInterval(settings.vsync ? 1 : 0);
  glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
  texSettings::bilinear = settings.bilinearFiltering;
  texSettings::mipMappingQuality = settings.mipmapping;
  gameScene->getWorld().setDetail(settings.worldDetail);
  if (music) {
    music->setEnabled(settings.music);
    music->setEffectsEnabled(settings.soundEffects);
  }
}

void onMenuAction(ui::MenuAction action) {
  switch (action) {
    case ui::MenuAction::Start:
    case ui::MenuAction::Resume:
      resumeGame();
      break;
    case ui::MenuAction::Quit:
      glfwSetWindowShouldClose(window, true);
      break;
    case ui::MenuAction::Restart:
      restartGame();
      break;
    case ui::MenuAction::MainMenu:
      // Start in the main menu begins a new run
      gameScene->restart();
      gameState = MAIN_MENU;
      menu->showMainMenu();
      break;
    case ui::MenuAction::GraphicsChanged:
      applyGraphicsSettings();
      break;
    case ui::MenuAction::ApplyDisplay:
      // The new framebuffer size arrives in framebufferSizeCallback
      display::applyWindowMode(window, settings.windowMode, settings.width,
                               settings.height);
      // the swap interval (VSync) may be reset by a new video mode
      applyGraphicsSettings();
      [[fallthrough]];
    case ui::MenuAction::SettingsClosed:
      if (useSavedSettings && !settings.save(SETTINGS_FILE)) {
        cerr << "WARNING: Could not save the settings to " << SETTINGS_FILE
             << endl;
      }
      break;
  }
}

/**
 * @return false if there is no pause menu (e.g. missing UI folder), the game
 * keeps running then
 */
bool pauseGame() {
  menu->showPauseMenu();
  if (!menu->isVisible()) {
    return false;
  }
  gameState = PAUSED;
  return true;
}

void resumeGame() {
  gameState = PLAYING;
  menu->hide();
  // The button that pressed Start or Resume in the menu may be the jump button
  controls->blockJumpUntilReleased();
}

void restartGame() {
  gameScene->restart();
  resumeGame();
}

// Based on:
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initTextRendering() {
  cout << "Initializing FreeType..." << endl;
  // Set OpenGL options
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  updateTextProjection();

  initFreeType();

  glGenVertexArrays(1, &textVAO);
  glGenBuffers(1, &textVBO);
  glBindVertexArray(textVAO);
  glBindBuffer(GL_ARRAY_BUFFER, textVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, 0, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

// Text is positioned in pixels, (0, 0) is the bottom left corner of the window
void updateTextProjection() {
  mat4 textProjectionMatrix = ortho(0.0f, static_cast<GLfloat>(windowWidth),
                                    0.0f, static_cast<GLfloat>(windowHeight));
  textShader->enable();
  glUniformMatrix4fv(textShader->getUniformLocation("projection"), 1, GL_FALSE,
                     value_ptr(textProjectionMatrix));
}

// The render targets of the renderer have the size of the window, so they are
// reallocated when it changes
void resizeScreenBuffers() {
  renderer->resize(windowWidth, windowHeight);
  updateTextProjection();
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;  // minimized
  }
  windowWidth = width;
  windowHeight = height;
  if (gameScene) {
    resizeScreenBuffers();
    gameScene->setViewportSize(width, height);
  }
  if (menu) {
    menu->onFramebufferResized(width, height);
  }
  if (settings.windowMode == WINDOWED && !glfwGetWindowMonitor(window)) {
    // Keeps the size of a window resized by the user (e.g. by dragging its
    // border) for the settings
    glfwGetWindowSize(window, &settings.width, &settings.height);
  }
}

void windowFocusCallback(GLFWwindow* window, int focused) {
  // e.g. when switching to another window with Alt+Tab (without menus the game
  // keeps running, a demo does not need the keyboard)
  if (!focused && gameState == PLAYING && !demoScript) {
    pauseGame();
  }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods) {
  if (!menu) {
    return;  // still loading
  }
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    switch (gameState) {
      case PLAYING:
        if (!pauseGame()) {
          // without menus ESC quits as in earlier versions
          glfwSetWindowShouldClose(window, true);
        }
        break;
      case PAUSED:
        if (!menu->back()) {
          resumeGame();
        }
        break;
      case MAIN_MENU:
        menu->back();
        break;
    }
    return;
  }
  if (gameState == PLAYING && key == GLFW_KEY_R && action == GLFW_PRESS) {
    restartGame();  // once per press
    return;
  }

  if (gameState == PLAYING) {
    fKeyCallback(window, key, scancode, action, mods);
  } else {
    menu->onKey(key, action, mods);
  }
}

void gameLoop() {
  auto time = glfwGetTime();
  int frame = 0;
  while (!glfwWindowShouldClose(window)) {
    auto time_new = glfwGetTime();
    if (fixedTimeStep > 0) {
      // Every frame advances the game by the same time: a recording as fast as
      // the frames can be drawn, a demo in real time
      if (!recorder) {
        this_thread::sleep_for(
            chrono::duration<double>(time + fixedTimeStep - time_new));
        time_new = glfwGetTime();
      }
      time_delta = fixedTimeStep;
    } else {
      time_delta = static_cast<float>(time_new - time);
    }
    time = time_new;

    glfwPollEvents();

    // Nothing to draw while the window is minimized
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
      glfwWaitEvents();
      continue;
    }

    if (demoScript) {
      playDemoFrame(frame);
    }
    ++frame;

    controls->update();
    handleGamepad();
    // Handles the buttons pressed in the menu (e.g. Start changes the game
    // state)
    menu->update();
    // The mouse turns the camera during the game and is a normal cursor in the
    // menus
    controls->setMouseCaptured(gameState == PLAYING &&
                               glfwGetWindowAttrib(window, GLFW_FOCUSED));

    messageTimeLeft = std::max(0.0f, messageTimeLeft - time_delta);

    // Do game logic here, the game stands still while a menu is open
    if (gameState == PLAYING) {
      mainShader->enable();
      update(std::min(time_delta, MAX_TIME_STEP));
      vector<SalmonQuest::Event> salmonEvents = gameScene->takeSalmonEvents();
      for (SalmonQuest::Event event : salmonEvents) {
        if (event == SalmonQuest::Event::EATEN) {
          lastMessage = "A salmon for Mishka's quest! +" +
                        to_string(SalmonQuest::SALMON_POINTS) + " points (" +
                        to_string(gameScene->getSalmonEaten()) + "/" +
                        to_string(gameScene->getPoolCount()) + ")";
          messageTimeLeft = 3;
        }
      }
      vector<StonesQuest::Event> stonesEvents = gameScene->takeStonesEvents();
      for (StonesQuest::Event event : stonesEvents) {
        if (event == StonesQuest::Event::ARRIVED) {
          lastMessage =
              "You found the stone circle! Inspect the flat stone in the "
              "middle.";
          messageTimeLeft = 4;
        } else if (event == StonesQuest::Event::INSPECTED) {
          lastMessage = string("Go back and tell ") + StonesQuest::SPEAKER +
                        " what is happening at the stones.";
          messageTimeLeft = 4;
        }
      }
      playSounds(salmonEvents, stonesEvents);
    }
    updateHud();
    // Do drawing of game here
    draw();

    // The HUD of the quests and the menus
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // also in wireframe mode
    menu->render();
    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    if (recorder && !recordingFailed &&
        !recorder->recordFrame(windowWidth, windowHeight)) {
      cerr << "ERROR: Could not record the frame (the disk is full or the "
              "window size changed)"
           << endl;
      recordingFailed = true;
      glfwSetWindowShouldClose(window, true);
    }

    glfwSwapBuffers(window);
  }
}

// The keys of the demo script are pressed as on the keyboard, for the menus and
// the game
void playDemoFrame(int frame) {
  for (const DemoScript::Event& event : demoScript->eventsAt(frame)) {
    switch (event.type) {
      case DemoScript::Event::KeyDown:
      case DemoScript::Event::KeyUp: {
        bool down = event.type == DemoScript::Event::KeyDown;
        input::setScriptedKey(event.key, down);
        keyCallback(window, event.key, glfwGetKeyScancode(event.key),
                    down ? GLFW_PRESS : GLFW_RELEASE, 0);
        break;
      }
      case DemoScript::Event::Quit:
        glfwSetWindowShouldClose(window, true);
        break;
    }
  }
}

void update(float time_delta) {
  // WASD moves the free camera (L) instead of the bear
  gameScene->update(time_delta,
                    controls->playerInput(time_delta, settings,
                                          !gameScene->isCameraLocked()));
}

// The sounds of what happened in the scene
void playSounds(const vector<SalmonQuest::Event>& salmonEvents,
                const vector<StonesQuest::Event>& stonesEvents) {
  vector<RaceQuest::Event> events = gameScene->takeEvents();
  int splashes = gameScene->takeSplashes();
  int chatters = gameScene->takeRaccoonChatters();
  if (!music) {
    return;
  }
  for (RaceQuest::Event event : events) {
    switch (event) {
      case RaceQuest::Event::BEAVER_SOUND:
        // The beaver chatters while its words appear (it does not speak), never
        // the same sound twice in a row
        beaverSound += 1 + (beaverSound * 7u + 3u) % 3u;
        music->play(audio::SoundEffect::BEAVER, static_cast<int>(beaverSound),
                    0.85f);
        break;
      case RaceQuest::Event::COUNTDOWN_TICK:
        music->play(audio::SoundEffect::COUNTDOWN);
        break;
      case RaceQuest::Event::START:
        music->play(audio::SoundEffect::GO);
        break;
      case RaceQuest::Event::CHECKPOINT:
        music->play(audio::SoundEffect::CHECKPOINT, 0, 0.7f);
        break;
      case RaceQuest::Event::COMPLETED:
        music->play(audio::SoundEffect::SUCCESS);
        break;
      case RaceQuest::Event::FAILED:
        music->play(audio::SoundEffect::FAILURE);
        break;
      default:
        break;
    }
  }
  for (SalmonQuest::Event event : salmonEvents) {
    switch (event) {
      case SalmonQuest::Event::BEAR_SOUND:
        // Mishka grumbles while its words appear, never the same sound twice in
        // a row
        bearSound += 1 + (bearSound * 5u + 1u) % 3u;
        music->play(audio::SoundEffect::BEAR,
                    static_cast<int>(bearSound % audio::soundEffectVariants(
                                                     audio::SoundEffect::BEAR)),
                    0.9f);
        break;
      case SalmonQuest::Event::EATEN:
        music->play(audio::SoundEffect::CHECKPOINT, 0, 0.7f);
        break;
      case SalmonQuest::Event::COMPLETED:
        music->play(audio::SoundEffect::SUCCESS);
        break;
      default:
        break;
    }
  }
  for (StonesQuest::Event event : stonesEvents) {
    switch (event) {
      case StonesQuest::Event::ARRIVED:
        music->play(audio::SoundEffect::CHECKPOINT, 0, 0.7f);
        break;
      case StonesQuest::Event::COMPLETED:
        music->play(audio::SoundEffect::SUCCESS);
        break;
      default:
        break;
    }
  }
  // Chonk chitters while its words appear, the raccoons that cheer when they
  // hop
  for (int i = 0; i < std::min(chatters, 2); ++i) {
    raccoonSound += 1 + (raccoonSound * 3u + 2u) % 3u;
    music->play(
        audio::SoundEffect::RACCOON,
        static_cast<int>(raccoonSound % audio::soundEffectVariants(
                                            audio::SoundEffect::RACCOON)),
        0.7f);
  }
  if (splashes > 0) {
    music->play(audio::SoundEffect::SPLASH, 0, 0.8f);
  }
}

/**
 * @return The conversation that is shown: with the beaver, Mishka, Chonk or at
 * the stones (a real conversation before a line in passing)
 */
const Dialog& shownDialog() {
  const Dialog* dialogs[] = {&gameScene->getRaceQuest().getDialog(),
                             &gameScene->getSalmonQuest().getDialog(),
                             &gameScene->getStonesQuest().getDialog()};
  for (const Dialog* dialog : dialogs) {
    if (dialog->isOpen() && !dialog->isPassing()) {
      return *dialog;
    }
  }
  for (const Dialog* dialog : dialogs) {
    if (dialog->isOpen()) {
      return *dialog;
    }
  }
  return *dialogs[0];
}

/**
 * @return What the button does near an animal (or the flat stone), empty if
 * there is nothing to talk to
 */
string talkPrompt() {
  switch (gameScene->getTalkPartner()) {
    case Scene::TalkPartner::BEAVER:
      return string("Talk to ") + RaceQuest::SPEAKER;
    case Scene::TalkPartner::MISHKA:
      return string("Talk to ") + SalmonQuest::SPEAKER;
    case Scene::TalkPartner::CHONK:
      return string("Talk to ") + StonesQuest::SPEAKER;
    case Scene::TalkPartner::STONES:
      return "Inspect the flat stone";
    case Scene::TalkPartner::NONE:
      break;
  }
  return "";
}

/**
 * @return Where the goal of the stones quest is: how far and an arrow in its
 * direction, seen from the camera
 */
string objectiveWay() {
  const StonesQuest& stones = gameScene->getStonesQuest();
  vec3 goal = stones.getStage() == StonesQuest::Stage::RETURNING
                  ? gameScene->getChonk().getPosition()
                  : gameScene->getStonesCenter();
  vec3 offset = goal - gameScene->getBear().getPosition();
  offset.y = 0.0f;
  float distance = length(offset);
  if (distance < 3.0f) {
    return "";
  }
  vec3 forward = gameScene->getCamera().getForward();
  forward.y = 0.0f;
  forward =
      length(forward) > 1e-4f ? normalize(forward) : vec3(0.0f, 0.0f, -1.0f);
  vec3 right(-forward.z, 0.0f, forward.x);
  // Clockwise from ahead: up, up right, right, ...
  float angle = std::atan2(dot(offset, right), dot(offset, forward));
  const char* const ARROWS[] = {"\u2191", "\u2197", "\u2192", "\u2198",
                                "\u2193", "\u2199", "\u2190", "\u2196"};
  int sector =
      static_cast<int>(std::floor(angle / (glm::pi<float>() / 4.0f) + 0.5f));
  sector = ((sector % 8) + 8) % 8;
  return string(ARROWS[sector]) + "  " +
         to_string(static_cast<int>(std::round(distance))) + " m";
}

/**
 * The quests on the HUD: what the player can do near the beaver or Mishka, the
 * conversation, the countdown, the race, fishing and their results
 */
void updateHud() {
  ui::Hud hud;
  hud.visible = gameState != MAIN_MENU;
  const RaceQuest& quest = gameScene->getRaceQuest();
  const SalmonQuest& fishing = gameScene->getSalmonQuest();
  const Dialog& dialog = shownDialog();
  bool gamepad = controls->hasGamepad();
  const StonesQuest& stones = gameScene->getStonesQuest();
  hud.prompt = talkPrompt();
  if (!hud.prompt.empty()) {
    hud.promptKey = gamepad ? "X" : "E";
  }
  hud.objective = stones.getObjective();
  if (!hud.objective.empty()) {
    hud.objectiveDetail = objectiveWay();
  }
  if (dialog.isOpen()) {
    hud.speaker = dialog.getSpeaker();
    hud.text = dialog.getText();
    hud.canContinue = dialog.canContinue();
    hud.continueKey = gamepad ? "A" : "E";
    if (dialog.isChoosing()) {
      hud.choices = dialog.getChoices();
      hud.selectedChoice = dialog.getSelectedChoice();
    }
  }
  int countdown = quest.getCountdownNumber();
  if (countdown >= 0) {
    hud.countdown = countdown > 0 ? to_string(countdown) : "GO!";
  }
  if (quest.getStage() == RaceQuest::Stage::RACING) {
    hud.racing = true;
    hud.raceTime = RaceQuest::formatTime(quest.getRaceTime());
    hud.checkpoints = to_string(quest.getCheckpointsPassed()) + "/" +
                      to_string(quest.getCheckpointCount());
    hud.timeLimit =
        to_string(static_cast<int>(RaceQuest::TIME_LIMIT) / 60) + " min";
  }
  if (fishing.isActive()) {
    hud.fishing = true;
    hud.fishingTime = RaceQuest::formatTime(fishing.getTime());
    hud.salmon = to_string(fishing.getEatenCount()) + "/" +
                 to_string(fishing.getPoolCount());
    hud.score = to_string(fishing.getScore());
  }
  const float BANNER_TIME = 6.0f;
  bool fishingResult = fishing.getStage() == SalmonQuest::Stage::IDLE &&
                       fishing.getOutcome() != SalmonQuest::Outcome::NONE &&
                       fishing.getOutcomeAge() < BANNER_TIME &&
                       (quest.getOutcome() == RaceQuest::Outcome::NONE ||
                        fishing.getOutcomeAge() < quest.getOutcomeAge());
  if (fishingResult) {
    const string again =
        string("Talk to ") + SalmonQuest::SPEAKER + " to go fishing again.";
    if (fishing.getOutcome() == SalmonQuest::Outcome::COMPLETED) {
      hud.banner = "Fishing complete!";
      hud.bannerSuccess = true;
      hud.bannerDetail =
          to_string(fishing.getScore()) + " points in " +
          RaceQuest::formatTime(fishing.getTime()) +
          (fishing.isNewBest()
               ? (fishing.getCompletions() > 1 ? "  -  a new best time!" : "")
               : "  -  best: " + RaceQuest::formatTime(fishing.getBestTime()));
    } else {
      hud.banner = "Stopped fishing";
      hud.bannerDetail = to_string(fishing.getScore()) + " points. " + again;
    }
  } else if (stones.getStage() == StonesQuest::Stage::IDLE &&
             stones.getOutcome() != StonesQuest::Outcome::NONE &&
             stones.getOutcomeAge() < BANNER_TIME &&
             (quest.getOutcome() == RaceQuest::Outcome::NONE ||
              stones.getOutcomeAge() < quest.getOutcomeAge())) {
    if (stones.getOutcome() == StonesQuest::Outcome::COMPLETED) {
      hud.banner = "Quest complete!";
      hud.bannerSuccess = true;
      hud.bannerDetail = stones.getReport() == StonesQuest::Report::TRUTH
                             ? "Chonk found its long lost snack stash"
                             : "Chonk will go and see for itself";
    } else {
      hud.banner = "Stopped exploring";
      hud.bannerDetail = string("Talk to ") + StonesQuest::SPEAKER +
                         " to go to the stones again.";
    }
  } else if (quest.getStage() == RaceQuest::Stage::IDLE &&
             quest.getOutcomeAge() < BANNER_TIME) {
    const string again =
        string("Talk to ") + RaceQuest::SPEAKER + " to try again.";
    switch (quest.getOutcome()) {
      case RaceQuest::Outcome::COMPLETED:
        hud.banner = "Race complete!";
        hud.bannerSuccess = true;
        hud.bannerDetail =
            "Your time: " + RaceQuest::formatTime(quest.getRaceTime()) +
            (quest.isNewBest()
                 ? (quest.getCompletions() > 1 ? "  -  a new best time!" : "")
                 : "  -  best: " + RaceQuest::formatTime(quest.getBestTime()));
        break;
      case RaceQuest::Outcome::TOO_SLOW:
        hud.banner = "Race failed";
        hud.bannerDetail = "Out of time. " + again;
        break;
      case RaceQuest::Outcome::LEFT_TRACK:
        hud.banner = "Race failed";
        hud.bannerDetail = "You left the track. " + again;
        break;
      case RaceQuest::Outcome::FELL_INTO_WATER:
        hud.banner = "Race failed";
        hud.bannerDetail = "You fell into the water. " + again;
        break;
      case RaceQuest::Outcome::NONE:
        break;
    }
  }
  menu->setHud(hud);
}

/**
 * Start (Options on a DualSense) pauses and resumes the game, the D-pad (or
 * left stick), A (Cross) and B (Circle) work the menus like the arrow keys,
 * ENTER and ESC
 */
void handleGamepad() {
  if (!controls->hasGamepad()) {
    return;
  }
  if (gameState == PLAYING) {
    if (controls->wasPressed(GLFW_GAMEPAD_BUTTON_START)) {
      pauseGame();
    }
    return;
  }
  if (gameState == PAUSED && controls->wasPressed(GLFW_GAMEPAD_BUTTON_START)) {
    resumeGame();
    return;
  }
  for (int key : controls->getMenuKeys()) {
    keyCallback(window, key, 0, GLFW_PRESS, 0);
    keyCallback(window, key, 0, GLFW_RELEASE, 0);
  }
}

void joystickCallback(int joystick, int event) {
  if (event == GLFW_CONNECTED) {
    const char* name = glfwGetJoystickName(joystick);
    lastMessage = glfwJoystickIsGamepad(joystick)
                      ? string("Gamepad connected: ") + (name ? name : "")
                      : string("Controller without a gamepad mapping: ") +
                            (name ? name : "") + " (add it to " +
                            input::Input::MAPPINGS_FILE + ")";
    messageTimeLeft = 4;
  } else if (event == GLFW_DISCONNECTED) {
    lastMessage = "Gamepad disconnected";
    messageTimeLeft = 4;
    if (gameState == PLAYING && menu) {
      pauseGame();  // e.g. the battery is empty
    }
  }
}

render::RenderSettings renderSettings() {
  render::RenderSettings result;
  result.hdr = settings.hdr;
  result.bloom = settings.bloom;
  result.shadows = settings.shadows;
  result.normalMapping = settings.normalMapping;
  result.transparency = settings.transparency;
  result.wireframe = settings.wireframe;
  return result;
}

void draw() {
  // Shadow maps, the scene in HDR, bloom, exposure and tone mapping
  // (render::Renderer)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  renderer->render(*gameScene, renderSettings(), time_delta);
  // The HUD on top of the final image
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  textRendering();
  glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void cleanup() {
  cout << "Cleaning up memory..." << endl;
  // Releases the OpenGL resources of the menus, so it must happen before the
  // window (and its context) is destroyed
  menu.reset();
  music.reset();
  gameScene.reset();
  renderer.reset();
  // The glyphs and the quad of the text
  for (const auto& [c, glyph] : Characters) {
    glDeleteTextures(1, &glyph.textureID);
  }
  Characters.clear();
  glDeleteBuffers(1, &textVBO);
  glDeleteVertexArrays(1, &textVAO);
  textShader.reset();
  mainShader.reset();
  // The scene (and the character controller of the bear) must be released
  // before the physics world
  physicsWorld.reset();
}

void initPhysics() {
  cout << "Initializing Jolt Physics..." << endl;
  physicsWorld = make_unique<physics::PhysicsWorld>();
}

// Based on
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initFreeType() {
  FT_Library ft;
  if (FT_Init_FreeType(&ft)) {
    cout << "ERROR: Could not initialize FreeType" << endl;
    return;
  }
  // Without the font the game still runs, just without the HUD text
  FT_Face face;
  if (FT_New_Face(ft, "Font/DejaVuSans.ttf", 0, &face)) {
    cout << "ERROR: Could not load the font" << endl;
    FT_Done_FreeType(ft);
    return;
  }
  FT_Set_Pixel_Sizes(face, 0, 48);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // the glyphs have 1 byte per pixel

  // A texture for each ASCII character
  for (GLubyte c = 0; c < 128; c++) {
    if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
      cout << "ERROR: Could not load the glyph " << static_cast<int>(c) << endl;
      continue;
    }
    const FT_GlyphSlot glyph = face->glyph;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, glyph->bitmap.width,
                 glyph->bitmap.rows, 0, GL_RED, GL_UNSIGNED_BYTE,
                 glyph->bitmap.buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    Characters.emplace(
        c, Character{texture, ivec2(glyph->bitmap.width, glyph->bitmap.rows),
                     ivec2(glyph->bitmap_left, glyph->bitmap_top),
                     static_cast<GLuint>(glyph->advance.x)});
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  FT_Done_Face(face);
  FT_Done_FreeType(ft);
}

void renderText(Shader* shader, const string& text, GLfloat x, GLfloat y,
                GLfloat scale, vec3 color) {
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);  // the text is always in front of the scene
  shader->enable();
  glUniform3f(shader->getUniformLocation("textColor"), color.x, color.y,
              color.z);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(textVAO);
  for (char c : text) {
    auto glyph = Characters.find(c);
    if (glyph == Characters.end()) {
      continue;  // e.g. non-ASCII characters in the name of a gamepad
    }
    const Character& ch = glyph->second;
    GLfloat left = x + ch.bearing.x * scale;
    GLfloat bottom = y - (ch.size.y - ch.bearing.y) * scale;
    GLfloat right = left + ch.size.x * scale;
    GLfloat top = bottom + ch.size.y * scale;
    // Two triangles: x, y, u, v
    GLfloat vertices[6][4] = {
        {left, top, 0.0f, 0.0f},     {left, bottom, 0.0f, 1.0f},
        {right, bottom, 1.0f, 1.0f}, {left, top, 0.0f, 0.0f},
        {right, bottom, 1.0f, 1.0f}, {right, top, 1.0f, 0.0f}};
    glBindTexture(GL_TEXTURE_2D, ch.textureID);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // On to the next glyph (the advance is in 1/64 pixels)
    x += (ch.advance >> 6) * scale;
  }
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void textRendering() {
  if (gameState == MAIN_MENU) {
    return;  // the main menu covers the HUD
  }
  if (settings.showHelp) {
    const char* const HELP[] = {
        "WASD or arrow keys / left stick to run",
        "Mouse / right stick to look around, wheel to zoom",
        "SPACE / Cross or A to jump (hold to jump higher)",
        "E / Square or X to talk, R to restart",
        "Bober the beaver at the start has a race for you!",
        "Mishka at the largest pool wants you to go fishing",
        "Chonk the raccoon at the trailhead wonders about the standing stones"};
    // A line per tenth of the window, below the frame time
    for (int line = 0; line < 7; ++line) {
      renderText(textShader.get(), HELP[line], windowWidth / 10.f,
                 windowHeight - (2.f + line) * windowHeight / 10.f, 0.6f,
                 vec3(0.5, 0.8f, 0.2f));
    }
  }
  if (settings.showFrameTime) {
    renderText(textShader.get(),
               "FPS: " + to_string(1.f / time_delta).substr(0, 4),
               windowWidth / 10.f, windowHeight - 1.f * windowHeight / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    renderText(textShader.get(),
               "Frametime: " + to_string(time_delta).substr(0, 5),
               windowWidth / 3.f, windowHeight - 1.f * windowHeight / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    // Fewer objects are drawn with view frustum culling (F8)
    string drawnObjects = to_string(gameScene->getDrawnGeometryCount()) + "/" +
                          to_string(gameScene->getGeometryCount());
    renderText(textShader.get(), "Objects: " + drawnObjects,
               windowWidth * 0.62f, windowHeight - 1.f * windowHeight / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    // The streamed world: chunks drawn and loaded (per level of detail), drawn
    // plants and stones, jobs of the worker threads
    world::World::Statistics world = gameScene->getWorld().getStatistics();
    string lods;
    for (int count : world.streaming.chunksAtLod) {
      lods += (lods.empty() ? "" : "/") + to_string(count);
    }
    renderText(textShader.get(),
               "Chunks: " + to_string(world.drawnChunks) + " drawn (LOD " +
                   lods + ")  Plants: " + to_string(world.drawnInstances) +
                   "  Jobs: " + to_string(world.streaming.pendingJobs),
               windowWidth / 10.f, windowHeight - 1.45f * windowHeight / 10.f,
               0.4f, vec3(0.9, 0.5f, 0.5f));
  }

  // Not under the conversation box
  if (shownDialog().isOpen() && menu->hasHud()) {
    return;
  }
  if (messageTimeLeft > 0) {
    renderText(textShader.get(), lastMessage, windowWidth / 10.f,
               windowHeight / 10.f, 0.4f, vec3(0.7, 0.5f, 0.8f));
  }

  // Without the HUD document (e.g. missing UI folder) the quests are shown as
  // plain text
  if (!menu->hasHud()) {
    const RaceQuest& quest = gameScene->getRaceQuest();
    const SalmonQuest& fishing = gameScene->getSalmonQuest();
    const Dialog& dialog = shownDialog();
    vec3 color(0.95f, 0.85f, 0.4f);
    if (dialog.isOpen()) {
      renderText(textShader.get(),
                 dialog.getSpeaker() + ": " + dialog.getText(),
                 windowWidth / 10.f, 3.f * windowHeight / 10.f, 0.4f, color);
      string choices;
      for (std::size_t i = 0;
           i < dialog.getChoices().size() && dialog.isChoosing(); ++i) {
        bool selected = static_cast<int>(i) == dialog.getSelectedChoice();
        choices += (selected ? "[" : " ") + dialog.getChoices()[i] +
                   (selected ? "]  " : "   ");
      }
      renderText(textShader.get(), choices, windowWidth / 10.f,
                 2.5f * windowHeight / 10.f, 0.45f, color);
    } else if (!talkPrompt().empty()) {
      renderText(textShader.get(), "E: " + talkPrompt(), windowWidth / 10.f,
                 3.f * windowHeight / 10.f, 0.45f, color);
    }
    if (!gameScene->getStonesQuest().getObjective().empty()) {
      renderText(textShader.get(), gameScene->getStonesQuest().getObjective(),
                 windowWidth / 10.f, 2.f * windowHeight / 10.f, 0.5f, color);
    }
    if (fishing.isActive()) {
      string salmon = RaceQuest::formatTime(fishing.getTime()) +
                      "   Salmon: " + to_string(fishing.getEatenCount()) + "/" +
                      to_string(fishing.getPoolCount()) +
                      "   Score: " + to_string(fishing.getScore());
      renderText(textShader.get(), salmon, windowWidth / 10.f,
                 2.f * windowHeight / 10.f, 0.5f, color);
    }
    int countdown = quest.getCountdownNumber();
    if (countdown >= 0) {
      renderText(textShader.get(), countdown > 0 ? to_string(countdown) : "GO!",
                 windowWidth / 2.f, windowHeight / 2.f, 1.5f, color);
    }
    if (quest.getStage() == RaceQuest::Stage::RACING) {
      string race =
          RaceQuest::formatTime(quest.getRaceTime()) +
          "   Checkpoints: " + to_string(quest.getCheckpointsPassed()) + "/" +
          to_string(quest.getCheckpointCount());
      renderText(textShader.get(), race, windowWidth / 10.f,
                 2.f * windowHeight / 10.f, 0.5f, color);
    }
  }
}

void fKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                  int mode) {
  // The F-keys change the same settings as the settings menu
  if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
    settings.showHelp = !settings.showHelp;
  }

  if (key == GLFW_KEY_F2 && action == GLFW_PRESS) {
    settings.showFrameTime = !settings.showFrameTime;
    string frameTimeMsg = settings.showFrameTime ? "enabled" : "disabled";
    lastMessage = "Frame/time display " + frameTimeMsg;
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F3 && action == GLFW_PRESS) {
    settings.wireframe = !settings.wireframe;
    applyGraphicsSettings();
    string wireFrameModeMsg = settings.wireframe ? "enabled" : "disabled";
    lastMessage = "Wireframe mode " + wireFrameModeMsg;
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F4 && action == GLFW_PRESS) {
    settings.bilinearFiltering = !settings.bilinearFiltering;
    applyGraphicsSettings();
    string bilinearMsg =
        settings.bilinearFiltering ? "bilinear" : "nearest neighbor";
    lastMessage = "Texture sampling: " + bilinearMsg;
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F5 && action == GLFW_PRESS) {
    settings.mipmapping = (settings.mipmapping + 1) % 3;
    applyGraphicsSettings();

    string mipmapMsg;
    switch (settings.mipmapping) {
      case 0:
        mipmapMsg = "OFF";
        break;
      case 1:
        mipmapMsg = "Nearest Neighbor";
        break;
      case 2:
        mipmapMsg = "Bilinear";
        break;
    }
    lastMessage = "Texture sampling: " + mipmapMsg;
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F6 && action == GLFW_PRESS) {
    settings.normalMapping = !settings.normalMapping;
    applyGraphicsSettings();
    lastMessage =
        "Normal Mapping enabled: " + to_string(settings.normalMapping);
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F7 && action == GLFW_PRESS) {
    settings.shadows = !settings.shadows;
    applyGraphicsSettings();
    lastMessage =
        "Shadow Mapping (PCF) enabled: " + to_string(settings.shadows);
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F8 && action == GLFW_PRESS) {
    bool viewFrustumCullingEnabled = !gameScene->isViewFrustumCullingEnabled();
    gameScene->setViewFrustumCullingEnabled(viewFrustumCullingEnabled);
    lastMessage =
        "View Frustum Culling enabled: " + to_string(viewFrustumCullingEnabled);
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F9 && action == GLFW_PRESS) {
    settings.transparency = !settings.transparency;
    applyGraphicsSettings();
    lastMessage = "Transparency enabled: " + to_string(settings.transparency);
    messageTimeLeft = 3;
  }

  if (key == GLFW_KEY_F10 && action == GLFW_PRESS) {
    settings.hdr = !settings.hdr;
    lastMessage = "HDR enabled: " + to_string(settings.hdr);
    messageTimeLeft = 3;
  }

  if (settings.hdr && key == GLFW_KEY_F11 && action == GLFW_PRESS) {
    settings.bloom = !settings.bloom;
    lastMessage = "Bloom (requires HDR) enabled: " + to_string(settings.bloom);
    messageTimeLeft = 3;
  }
}

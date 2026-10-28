#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <ft2build.h>

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
#include FT_FREETYPE_H

namespace audio = bibo::audio;
namespace chrono = std::chrono;
namespace display = bibo::display;
namespace input = bibo::input;
namespace physics = bibo::physics;
namespace render = bibo::render;
namespace this_thread = std::this_thread;
namespace ui = bibo::ui;
namespace util = bibo::util;
namespace world = bibo::world;
using bibo::DemoScript;
using bibo::FrameRecorder;
using bibo::kFullscreen;
using bibo::kWindowed;
using bibo::Settings;
using bibo::Shader;
using bibo::scene::Dialog;
using bibo::scene::RaceQuest;
using bibo::scene::SalmonQuest;
using bibo::scene::Scene;
using bibo::scene::StonesQuest;
using glm::ivec2;
using glm::mat4;
using glm::ortho;
using glm::vec3;
using std::cerr;
using std::cout;
using std::endl;
using std::make_unique;
using std::map;
using std::string;
using std::stringstream;
using std::to_string;
using std::unique_ptr;
using std::vector;

unique_ptr<physics::PhysicsWorld> physics_world;

void Initialize();
void Init(GLFWwindow* window);
void InitPhysics();
void InitTextRendering();
void UpdateTextProjection();
void InitFreeType();
void InitMenu();
void TextRendering();
void InitDebug();
void Cleanup();
void Draw();
void Update(float time_delta);
void GameLoop();
void PlayDemoFrame(int frame);
void RenderText(Shader* shader, const string& text, GLfloat x, GLfloat y,
                GLfloat scale, vec3 color);
void ResizeScreenBuffers();
render::RenderSettings CurrentRenderSettings();
void ApplyGraphicsSettings();
bool PauseGame();
void ResumeGame();
void RestartGame();
void PlaySounds(const vector<SalmonQuest::Event>& salmon_events,
                const vector<StonesQuest::Event>& stones_events);
void UpdateHud();
void OnMenuAction(ui::MenuAction action);
void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods);
void FKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                  int mode);
void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
void WindowFocusCallback(GLFWwindow* window, int focused);
void JoystickCallback(int joystick, int event);
void HandleGamepad();

// vars
// in the working directory, like the assets
const char* const kSettingsFile = "settings.cfg";
Settings settings;
unsigned int window_width = 800;  // Size of the framebuffer in pixels
unsigned int window_height = 600;
GLFWwindow* window;
float time_delta;  // Last frame time
// Longer frames (e.g. while the window is dragged) slow the game down instead
// of simulating a few huge steps
const float kMaxTimeStep = 0.1f;

// Demo (--demo) and recording (--record): the game runs with a fixed time step
// and the default settings, so that a script always plays the same way (e.g.
// tools/readme_demo.txt for the GIF of the README)
float fixed_time_step =
    0;  // 0: the time step is the duration of the last frame
bool use_saved_settings = true;
bool recording = false;  //--record
unique_ptr<DemoScript> demo_script;
unique_ptr<FrameRecorder> recorder;
bool recording_failed = false;

enum GameState { kMainMenu, kPlaying, kPaused };
GameState game_state = kMainMenu;
unique_ptr<ui::Menu> menu;
// Keyboard, mouse and gamepad of the player
unique_ptr<input::Input> controls;

unique_ptr<Shader> main_shader;
unique_ptr<Shader> text_shader;

unique_ptr<Scene> game_scene;
// The music and the sounds (not while recording: the game runs faster or slower
// than real time then)
unique_ptr<audio::MusicPlayer> music;
// the beaver does not make the same sound twice in a row
unsigned int beaver_sound = 0;
unsigned int bear_sound = 0;     // neither does Mishka
unsigned int raccoon_sound = 0;  // nor the raccoons
// Shadow maps, HDR image, bloom, auto exposure and tone mapping
unique_ptr<render::Renderer> renderer;

#ifdef _WIN32
// Ask laptops with switchable graphics to run the game on the dedicated
// Nvidia/AMD GPU
extern "C" {
// The drivers look for these names
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;  // NOLINT
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;    // NOLINT
}
#endif
// Based on:
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering Holds
// all state information relevant to a character as loaded using FreeType
struct Character {
  GLuint texture_id;  // ID handle of the glyph texture
  ivec2 size;         // Size of glyph
  ivec2 bearing;      // Offset from baseline to left/top of glyph
  GLuint advance;     // Horizontal offset to advance to next glyph
};
map<GLchar, Character> characters;
GLuint text_vao;
GLuint text_vbo;
string last_message = "";
float message_time_left = 0;

int main(int argc, char** argv) {
  // Options anywhere on the command line: --demo <script> plays the input of
  // the script, --record <file> writes every frame to the file (raw video, see
  // FrameRecorder), --fps <frames per second> of both (default 15)
  vector<string> arguments;  // width height [fullscreen]
  string demo_path;
  string record_path;
  int fps = 15;
  for (int i = 1; i < argc; ++i) {
    string argument = argv[i];
    bool has_value = i + 1 < argc;
    if (argument == "--demo" && has_value) {
      demo_path = argv[++i];
    } else if (argument == "--record" && has_value) {
      record_path = argv[++i];
    } else if (argument == "--fps" && has_value) {
      if ((stringstream(argv[++i]) >> fps).fail() || fps <= 0 || fps > 1000) {
        util::FatalError("--fps needs the number of frames per second");
      }
    } else if (argument.starts_with("--")) {
      util::FatalError("Unknown option " + argument +
                       " (BIBO [--demo <script>] [--record <file>] [--fps "
                       "<frames per second>] [width height [fullscreen]])");
    } else {
      arguments.push_back(argument);
    }
  }

  recording = !record_path.empty();
  if (!demo_path.empty() || recording) {
    fixed_time_step = 1.0f / fps;
    use_saved_settings = false;
    // It would show the fixed time step instead of the time it takes to draw a
    // frame
    settings.show_frame_time = false;
  }
  if (!demo_path.empty()) {
    demo_script = make_unique<DemoScript>();
    string error;
    if (!demo_script->Load(demo_path, fps, error)) {
      util::FatalError("Demo script " + demo_path + ": " + error);
    }
  }

  // Saved by the settings menu, the command line arguments take precedence
  if (use_saved_settings) {
    settings.Load(kSettingsFile);
  }

  if (arguments.size() >= 2) {
    cout << "You are running" << endl;

    if ((stringstream(arguments[0]) >> settings.width).fail() ||
        settings.width <= 0) {
      cerr << "ERROR: Could not parse first element, try again!" << endl;
      util::Pause();
      exit(EXIT_FAILURE);
    }

    if ((stringstream(arguments[1]) >> settings.height).fail() ||
        settings.height <= 0) {
      cerr << "ERROR: Could not parse second element, try again!" << endl;
      util::Pause();
      exit(EXIT_FAILURE);
    }

    if (arguments.size() >= 3) {
      bool fullscreen;
      if ((stringstream(arguments[2]) >> fullscreen).fail()) {
        cerr << "ERROR: Could not parse third command-line-arg, try again!"
             << endl;
        util::Pause();
        exit(EXIT_FAILURE);
      }
      settings.window_mode = fullscreen ? kFullscreen : kWindowed;
    }
  }

  cout << "Starting BIBO game" << endl;

  Initialize();

  if (!record_path.empty()) {
    recorder =
        make_unique<FrameRecorder>(record_path, window_width, window_height);
    if (!recorder->IsOpen()) {
      util::FatalError("Could not create " + record_path);
    }
    cout << "Recording " << window_width << "x" << window_height
         << " pixels at " << fps << " fps to " << record_path << endl;
  }

  GameLoop();

  if (recorder) {
    cout << "Recorded " << recorder->GetFrameCount() << " frames" << endl;
    recorder.reset();
  }

  Cleanup();

  glfwDestroyWindow(window);

  glfwTerminate();

  return recording_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void Initialize() {
  if (!glfwInit()) {
    cerr << "ERROR: Could not init glfw." << endl;
    util::Pause();
    exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifndef NDEBUG
  // Debug output (see InitDebug), the extra checks of a debug context can slow
  // down the driver
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // Windowed, full screen or borderless (see settings)
  window = display::CreateWindow(settings.window_mode, settings.width,
                                 settings.height, "BIBO Project");

  if (!window)  // window == nullptr
  {
    cerr << "ERROR: Could not create a window with an OpenGL 4.3 core context"
         << endl;
    glfwTerminate();
    util::Pause();
    exit(EXIT_FAILURE);
  }
  display::SetWindowIcon(window, "Icon/bibo.png");
  // The framebuffer can be smaller than requested (e.g. limited to the screen)
  // or larger (scaled on high DPI screens)
  int framebuffer_width, framebuffer_height;
  glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
  window_width = std::max(framebuffer_width, 1);
  window_height = std::max(framebuffer_height, 1);

  // Input goes to the game or, while it is visible, to the menu
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
    if (menu && menu->IsVisible()) menu->OnChar(codepoint);
  });
  glfwSetMouseButtonCallback(window,
                             [](GLFWwindow*, int button, int action, int mods) {
                               if (menu && menu->IsVisible())
                                 menu->OnMouseButton(button, action, mods);
                             });
  glfwSetScrollCallback(window, [](GLFWwindow*, double, double y_offset) {
    if (menu && menu->IsVisible()) menu->OnScroll(y_offset);
    // zooms the camera
    else if (controls)
      controls->OnScroll(y_offset);
  });
  // The menu always knows where the mouse is, so that it highlights the right
  // button as soon as it opens. During the game the mouse is captured and turns
  // the camera.
  glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
    if (menu) menu->OnCursorPos(x, y);
    if (controls) controls->OnCursorPos(x, y);
  });
  glfwSetJoystickCallback(JoystickCallback);
  glfwSetCursorEnterCallback(window, [](GLFWwindow*, int entered) {
    if (menu) menu->OnCursorEnter(entered == GLFW_TRUE);
  });
  glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
  glfwSetWindowFocusCallback(window, WindowFocusCallback);
  glfwMakeContextCurrent(window);

  // GLEW initialization
  glewExperimental = true;  // Important for loading in old OSes
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    cerr << "OpenGL Error: " << error << endl;
  }
  GLenum glew_status = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
  // On Wayland there is no GLX display, but the OpenGL functions were loaded
  // nevertheless
  if (glew_status == GLEW_ERROR_NO_GLX_DISPLAY) {
    glew_status = GLEW_OK;
  }
#endif
  if (glew_status != GLEW_OK) {
    cerr << "ERROR: Could not load glew: " << glewGetErrorString(glew_status)
         << endl;
    glfwTerminate();
    util::Pause();
    exit(EXIT_FAILURE);
  }
  if (glewIsSupported("GL_VERSION_4_3")) {
    cout << "GLEW: OpenGL Version 4.3 is supported!" << endl;
  } else {
    cerr << "GLEW: OpenGL Version is below 4.3! :(" << endl;
  }

  InitDebug();

  InitPhysics();

  Init(window);
}

#ifndef NDEBUG
namespace {
const char* DebugSourceName(GLenum source) {
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

const char* DebugTypeName(GLenum type) {
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

const char* DebugSeverityName(GLenum severity) {
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

void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id,
                              GLenum severity, GLsizei, const GLchar* message,
                              const void*) {
  // Notifications come e.g. where the driver keeps a buffer, for every chunk of
  // the streamed world
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
    return;
  }
  cout << "OpenGL DEBUG: " << message
       << " [Source = " << DebugSourceName(source)
       << ", Type = " << DebugTypeName(type)
       << ", Severity = " << DebugSeverityName(severity) << ", ID = " << id
       << "]" << endl;
}
}  // namespace
#endif

void InitDebug() {
#ifndef NDEBUG
  // Core in OpenGL 4.3, which the game needs. Synchronous: the callback runs
  // right where the error happens.
  if (glDebugMessageCallback != nullptr) {
    glDebugMessageCallback(DebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  }
#endif
}

void InitShaders() {
  cout << "Initializing shaders..." << endl;
  // Objects of the track (the world, the bear and the post processing have
  // their own shaders)
  cout << "Main shader..." << endl;
  main_shader =
      make_unique<Shader>("Shader/main_shader.vert", "Shader/main_shader.frag");
  cout << "Text shader..." << endl;
  text_shader =
      make_unique<Shader>("Shader/text_shader.vert", "Shader/text_shader.frag");

  main_shader->Enable();
  glUniform1i(main_shader->GetUniformLocation("colorTextureSampler"),
              render::texture_unit::kColor);
  glUniform1i(main_shader->GetUniformLocation("normalMap"),
              render::texture_unit::kNormalMap);

  cout << "Renderer (shadow maps, HDR, bloom)..." << endl;
  renderer = make_unique<render::Renderer>(window_width, window_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void Init(GLFWwindow* window) {
  glEnable(GL_DEPTH_TEST);
  controls = make_unique<input::Input>(window);

  InitShaders();
  InitTextRendering();

  cout << "Loading assets from files via assimp and setting up scene..."
       << endl;
  if (!recording) {
    // synthesizes the music while the scene loads
    music = make_unique<audio::MusicPlayer>();
  }
  game_scene =
      make_unique<Scene>(window, main_shader.get(), physics_world.get());
  game_scene->SetViewportSize(window_width, window_height);
  // The scene stands still behind the main menu. Simulating the first second
  // (without input) lets the bear land on the ground and the camera follow it,
  // as at the start of the game.
  for (int i = 0; i < 60; ++i) {
    game_scene->Update(1.0f / 60.0f);
  }

  ApplyGraphicsSettings();
  InitMenu();
}

void InitMenu() {
  cout << "Initializing menus (RmlUi)..." << endl;
  menu = make_unique<ui::Menu>(window, settings, OnMenuAction);
  menu->ShowMainMenu();
  if (!menu->IsVisible()) {
    // Without menus (e.g. missing UI folder) the game starts right away, ESC
    // quits as in earlier versions
    game_state = kPlaying;
  }
}

// Applies the settings that are not read directly when drawing (like
// settings.hdr)
void ApplyGraphicsSettings() {
  glfwSwapInterval(settings.vsync ? 1 : 0);
  glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
  tex_settings::bilinear = settings.bilinear_filtering;
  tex_settings::mip_mapping_quality = settings.mipmapping;
  game_scene->GetWorld().SetDetail(settings.world_detail);
  if (music) {
    music->SetEnabled(settings.music);
    music->SetEffectsEnabled(settings.sound_effects);
  }
}

void OnMenuAction(ui::MenuAction action) {
  switch (action) {
    case ui::MenuAction::kStart:
    case ui::MenuAction::kResume:
      ResumeGame();
      break;
    case ui::MenuAction::kQuit:
      glfwSetWindowShouldClose(window, true);
      break;
    case ui::MenuAction::kRestart:
      RestartGame();
      break;
    case ui::MenuAction::kMainMenu:
      // Start in the main menu begins a new run
      game_scene->Restart();
      game_state = kMainMenu;
      menu->ShowMainMenu();
      break;
    case ui::MenuAction::kGraphicsChanged:
      ApplyGraphicsSettings();
      break;
    case ui::MenuAction::kApplyDisplay:
      // The new framebuffer size arrives in FramebufferSizeCallback
      display::ApplyWindowMode(window, settings.window_mode, settings.width,
                               settings.height);
      // the swap interval (VSync) may be reset by a new video mode
      ApplyGraphicsSettings();
      [[fallthrough]];
    case ui::MenuAction::kSettingsClosed:
      if (use_saved_settings && !settings.Save(kSettingsFile)) {
        cerr << "WARNING: Could not save the settings to " << kSettingsFile
             << endl;
      }
      break;
  }
}

/**
 * @return false if there is no pause menu (e.g. missing UI folder), the game
 * keeps running then
 */
bool PauseGame() {
  menu->ShowPauseMenu();
  if (!menu->IsVisible()) {
    return false;
  }
  game_state = kPaused;
  return true;
}

void ResumeGame() {
  game_state = kPlaying;
  menu->Hide();
  // The button that pressed Start or Resume in the menu may be the jump button
  controls->BlockJumpUntilReleased();
}

void RestartGame() {
  game_scene->Restart();
  ResumeGame();
}

// Based on:
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void InitTextRendering() {
  cout << "Initializing FreeType..." << endl;
  // Set OpenGL options
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  UpdateTextProjection();

  InitFreeType();

  glGenVertexArrays(1, &text_vao);
  glGenBuffers(1, &text_vbo);
  glBindVertexArray(text_vao);
  glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
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
void UpdateTextProjection() {
  mat4 text_projection_matrix =
      ortho(0.0f, static_cast<GLfloat>(window_width), 0.0f,
            static_cast<GLfloat>(window_height));
  text_shader->Enable();
  glUniformMatrix4fv(text_shader->GetUniformLocation("projection"), 1, GL_FALSE,
                     value_ptr(text_projection_matrix));
}

// The render targets of the renderer have the size of the window, so they are
// reallocated when it changes
void ResizeScreenBuffers() {
  renderer->Resize(window_width, window_height);
  UpdateTextProjection();
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;  // minimized
  }
  window_width = width;
  window_height = height;
  if (game_scene) {
    ResizeScreenBuffers();
    game_scene->SetViewportSize(width, height);
  }
  if (menu) {
    menu->OnFramebufferResized(width, height);
  }
  if (settings.window_mode == kWindowed && !glfwGetWindowMonitor(window)) {
    // Keeps the size of a window resized by the user (e.g. by dragging its
    // border) for the settings
    glfwGetWindowSize(window, &settings.width, &settings.height);
  }
}

void WindowFocusCallback(GLFWwindow* window, int focused) {
  // e.g. when switching to another window with Alt+Tab (without menus the game
  // keeps running, a demo does not need the keyboard)
  if (!focused && game_state == kPlaying && !demo_script) {
    PauseGame();
  }
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods) {
  if (!menu) {
    return;  // still loading
  }
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    switch (game_state) {
      case kPlaying:
        if (!PauseGame()) {
          // without menus ESC quits as in earlier versions
          glfwSetWindowShouldClose(window, true);
        }
        break;
      case kPaused:
        if (!menu->Back()) {
          ResumeGame();
        }
        break;
      case kMainMenu:
        menu->Back();
        break;
    }
    return;
  }
  if (game_state == kPlaying && key == GLFW_KEY_R && action == GLFW_PRESS) {
    RestartGame();  // once per press
    return;
  }

  if (game_state == kPlaying) {
    FKeyCallback(window, key, scancode, action, mods);
  } else {
    menu->OnKey(key, action, mods);
  }
}

void GameLoop() {
  auto time = glfwGetTime();
  int frame = 0;
  while (!glfwWindowShouldClose(window)) {
    auto time_new = glfwGetTime();
    if (fixed_time_step > 0) {
      // Every frame advances the game by the same time: a recording as fast as
      // the frames can be drawn, a demo in real time
      if (!recorder) {
        this_thread::sleep_for(
            chrono::duration<double>(time + fixed_time_step - time_new));
        time_new = glfwGetTime();
      }
      time_delta = fixed_time_step;
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

    if (demo_script) {
      PlayDemoFrame(frame);
    }
    ++frame;

    controls->Update();
    HandleGamepad();
    // Handles the buttons pressed in the menu (e.g. Start changes the game
    // state)
    menu->Update();
    // The mouse turns the camera during the game and is a normal cursor in the
    // menus
    controls->SetMouseCaptured(game_state == kPlaying &&
                               glfwGetWindowAttrib(window, GLFW_FOCUSED));

    message_time_left = std::max(0.0f, message_time_left - time_delta);

    // Do game logic here, the game stands still while a menu is open
    if (game_state == kPlaying) {
      main_shader->Enable();
      Update(std::min(time_delta, kMaxTimeStep));
      vector<SalmonQuest::Event> salmon_events = game_scene->TakeSalmonEvents();
      for (SalmonQuest::Event event : salmon_events) {
        if (event == SalmonQuest::Event::kEaten) {
          last_message = "A salmon for Mishka's quest! +" +
                         to_string(SalmonQuest::kSalmonPoints) + " points (" +
                         to_string(game_scene->GetSalmonEaten()) + "/" +
                         to_string(game_scene->GetPoolCount()) + ")";
          message_time_left = 3;
        }
      }
      vector<StonesQuest::Event> stones_events = game_scene->TakeStonesEvents();
      for (StonesQuest::Event event : stones_events) {
        if (event == StonesQuest::Event::kArrived) {
          last_message =
              "You found the stone circle! Inspect the flat stone in the "
              "middle.";
          message_time_left = 4;
        } else if (event == StonesQuest::Event::kInspected) {
          last_message = string("Go back and tell ") + StonesQuest::kSpeaker +
                         " what is happening at the stones.";
          message_time_left = 4;
        }
      }
      PlaySounds(salmon_events, stones_events);
    }
    UpdateHud();
    // Do drawing of game here
    Draw();

    // The HUD of the quests and the menus
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // also in wireframe mode
    menu->Render();
    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    if (recorder && !recording_failed &&
        !recorder->RecordFrame(window_width, window_height)) {
      cerr << "ERROR: Could not record the frame (the disk is full or the "
              "window size changed)"
           << endl;
      recording_failed = true;
      glfwSetWindowShouldClose(window, true);
    }

    glfwSwapBuffers(window);
  }
}

// The keys of the demo script are pressed as on the keyboard, for the menus and
// the game
void PlayDemoFrame(int frame) {
  for (const DemoScript::Event& event : demo_script->EventsAt(frame)) {
    switch (event.type) {
      case DemoScript::Event::kKeyDown:
      case DemoScript::Event::kKeyUp: {
        bool down = event.type == DemoScript::Event::kKeyDown;
        input::SetScriptedKey(event.key, down);
        KeyCallback(window, event.key, glfwGetKeyScancode(event.key),
                    down ? GLFW_PRESS : GLFW_RELEASE, 0);
        break;
      }
      case DemoScript::Event::kQuit:
        glfwSetWindowShouldClose(window, true);
        break;
    }
  }
}

void Update(float time_delta) {
  // WASD moves the free camera (L) instead of the bear
  game_scene->Update(time_delta,
                     controls->ReadPlayerInput(time_delta, settings,
                                               !game_scene->IsCameraLocked()));
}

// The sounds of what happened in the scene
void PlaySounds(const vector<SalmonQuest::Event>& salmon_events,
                const vector<StonesQuest::Event>& stones_events) {
  vector<RaceQuest::Event> events = game_scene->TakeEvents();
  int splashes = game_scene->TakeSplashes();
  int chatters = game_scene->TakeRaccoonChatters();
  if (!music) {
    return;
  }
  for (RaceQuest::Event event : events) {
    switch (event) {
      case RaceQuest::Event::kBeaverSound:
        // The beaver chatters while its words appear (it does not speak), never
        // the same sound twice in a row
        beaver_sound += 1 + (beaver_sound * 7u + 3u) % 3u;
        music->Play(audio::SoundEffect::kBeaver, static_cast<int>(beaver_sound),
                    0.85f);
        break;
      case RaceQuest::Event::kCountdownTick:
        music->Play(audio::SoundEffect::kCountdown);
        break;
      case RaceQuest::Event::kStart:
        music->Play(audio::SoundEffect::kGo);
        break;
      case RaceQuest::Event::kCheckpoint:
        music->Play(audio::SoundEffect::kCheckpoint, 0, 0.7f);
        break;
      case RaceQuest::Event::kCompleted:
        music->Play(audio::SoundEffect::kSuccess);
        break;
      case RaceQuest::Event::kFailed:
        music->Play(audio::SoundEffect::kFailure);
        break;
      default:
        break;
    }
  }
  for (SalmonQuest::Event event : salmon_events) {
    switch (event) {
      case SalmonQuest::Event::kBearSound:
        // Mishka grumbles while its words appear, never the same sound twice in
        // a row
        bear_sound += 1 + (bear_sound * 5u + 1u) % 3u;
        music->Play(
            audio::SoundEffect::kBear,
            static_cast<int>(bear_sound % audio::SoundEffectVariants(
                                              audio::SoundEffect::kBear)),
            0.9f);
        break;
      case SalmonQuest::Event::kEaten:
        music->Play(audio::SoundEffect::kCheckpoint, 0, 0.7f);
        break;
      case SalmonQuest::Event::kCompleted:
        music->Play(audio::SoundEffect::kSuccess);
        break;
      default:
        break;
    }
  }
  for (StonesQuest::Event event : stones_events) {
    switch (event) {
      case StonesQuest::Event::kArrived:
        music->Play(audio::SoundEffect::kCheckpoint, 0, 0.7f);
        break;
      case StonesQuest::Event::kCompleted:
        music->Play(audio::SoundEffect::kSuccess);
        break;
      default:
        break;
    }
  }
  // Chonk chitters while its words appear, the raccoons that cheer when they
  // hop
  for (int i = 0; i < std::min(chatters, 2); ++i) {
    raccoon_sound += 1 + (raccoon_sound * 3u + 2u) % 3u;
    music->Play(
        audio::SoundEffect::kRaccoon,
        static_cast<int>(raccoon_sound % audio::SoundEffectVariants(
                                             audio::SoundEffect::kRaccoon)),
        0.7f);
  }
  if (splashes > 0) {
    music->Play(audio::SoundEffect::kSplash, 0, 0.8f);
  }
}

/**
 * @return The conversation that is shown: with the beaver, Mishka, Chonk or at
 * the stones (a real conversation before a line in passing)
 */
const Dialog& ShownDialog() {
  const Dialog* dialogs[] = {&game_scene->GetRaceQuest().GetDialog(),
                             &game_scene->GetSalmonQuest().GetDialog(),
                             &game_scene->GetStonesQuest().GetDialog()};
  for (const Dialog* dialog : dialogs) {
    if (dialog->IsOpen() && !dialog->IsPassing()) {
      return *dialog;
    }
  }
  for (const Dialog* dialog : dialogs) {
    if (dialog->IsOpen()) {
      return *dialog;
    }
  }
  return *dialogs[0];
}

/**
 * @return What the button does near an animal (or the flat stone), empty if
 * there is nothing to talk to
 */
string TalkPrompt() {
  switch (game_scene->GetTalkPartner()) {
    case Scene::TalkPartner::kBeaver:
      return string("Talk to ") + RaceQuest::kSpeaker;
    case Scene::TalkPartner::kMishka:
      return string("Talk to ") + SalmonQuest::kSpeaker;
    case Scene::TalkPartner::kChonk:
      return string("Talk to ") + StonesQuest::kSpeaker;
    case Scene::TalkPartner::kStones:
      return "Inspect the flat stone";
    case Scene::TalkPartner::kNone:
      break;
  }
  return "";
}

/**
 * @return Where the goal of the stones quest is: how far and an arrow in its
 * direction, seen from the camera
 */
string ObjectiveWay() {
  const StonesQuest& stones = game_scene->GetStonesQuest();
  vec3 goal = stones.GetStage() == StonesQuest::Stage::kReturning
                  ? game_scene->GetChonk().GetPosition()
                  : game_scene->GetStonesCenter();
  vec3 offset = goal - game_scene->GetBear().GetPosition();
  offset.y = 0.0f;
  float distance = length(offset);
  if (distance < 3.0f) {
    return "";
  }
  vec3 forward = game_scene->GetCamera().GetForward();
  forward.y = 0.0f;
  forward =
      length(forward) > 1e-4f ? normalize(forward) : vec3(0.0f, 0.0f, -1.0f);
  vec3 right(-forward.z, 0.0f, forward.x);
  // Clockwise from ahead: up, up right, right, ...
  float angle = std::atan2(dot(offset, right), dot(offset, forward));
  const char* const arrows[] = {"\u2191", "\u2197", "\u2192", "\u2198",
                                "\u2193", "\u2199", "\u2190", "\u2196"};
  int sector =
      static_cast<int>(std::floor(angle / (glm::pi<float>() / 4.0f) + 0.5f));
  sector = ((sector % 8) + 8) % 8;
  return string(arrows[sector]) + "  " +
         to_string(static_cast<int>(std::round(distance))) + " m";
}

/**
 * The quests on the HUD: what the player can do near the beaver or Mishka, the
 * conversation, the countdown, the race, fishing and their results
 */
void UpdateHud() {
  ui::Hud hud;
  hud.visible = game_state != kMainMenu;
  const RaceQuest& quest = game_scene->GetRaceQuest();
  const SalmonQuest& fishing = game_scene->GetSalmonQuest();
  const Dialog& dialog = ShownDialog();
  bool gamepad = controls->HasGamepad();
  const StonesQuest& stones = game_scene->GetStonesQuest();
  hud.prompt = TalkPrompt();
  if (!hud.prompt.empty()) {
    hud.prompt_key = gamepad ? "X" : "E";
  }
  hud.objective = stones.GetObjective();
  if (!hud.objective.empty()) {
    hud.objective_detail = ObjectiveWay();
  }
  if (dialog.IsOpen()) {
    hud.speaker = dialog.GetSpeaker();
    hud.text = dialog.GetText();
    hud.can_continue = dialog.CanContinue();
    hud.continue_key = gamepad ? "A" : "E";
    if (dialog.IsChoosing()) {
      hud.choices = dialog.GetChoices();
      hud.selected_choice = dialog.GetSelectedChoice();
    }
  }
  int countdown = quest.GetCountdownNumber();
  if (countdown >= 0) {
    hud.countdown = countdown > 0 ? to_string(countdown) : "GO!";
  }
  if (quest.GetStage() == RaceQuest::Stage::kRacing) {
    hud.racing = true;
    hud.race_time = RaceQuest::FormatTime(quest.GetRaceTime());
    hud.checkpoints = to_string(quest.GetCheckpointsPassed()) + "/" +
                      to_string(quest.GetCheckpointCount());
    hud.time_limit =
        to_string(static_cast<int>(RaceQuest::kTimeLimit) / 60) + " min";
  }
  if (fishing.IsActive()) {
    hud.fishing = true;
    hud.fishing_time = RaceQuest::FormatTime(fishing.GetTime());
    hud.salmon = to_string(fishing.GetEatenCount()) + "/" +
                 to_string(fishing.GetPoolCount());
    hud.score = to_string(fishing.GetScore());
  }
  const float banner_time = 6.0f;
  bool fishing_result = fishing.GetStage() == SalmonQuest::Stage::kIdle &&
                        fishing.GetOutcome() != SalmonQuest::Outcome::kNone &&
                        fishing.GetOutcomeAge() < banner_time &&
                        (quest.GetOutcome() == RaceQuest::Outcome::kNone ||
                         fishing.GetOutcomeAge() < quest.GetOutcomeAge());
  if (fishing_result) {
    const string again =
        string("Talk to ") + SalmonQuest::kSpeaker + " to go fishing again.";
    if (fishing.GetOutcome() == SalmonQuest::Outcome::kCompleted) {
      hud.banner = "Fishing complete!";
      hud.banner_success = true;
      hud.banner_detail =
          to_string(fishing.GetScore()) + " points in " +
          RaceQuest::FormatTime(fishing.GetTime()) +
          (fishing.IsNewBest()
               ? (fishing.GetCompletions() > 1 ? "  -  a new best time!" : "")
               : "  -  best: " + RaceQuest::FormatTime(fishing.GetBestTime()));
    } else {
      hud.banner = "Stopped fishing";
      hud.banner_detail = to_string(fishing.GetScore()) + " points. " + again;
    }
  } else if (stones.GetStage() == StonesQuest::Stage::kIdle &&
             stones.GetOutcome() != StonesQuest::Outcome::kNone &&
             stones.GetOutcomeAge() < banner_time &&
             (quest.GetOutcome() == RaceQuest::Outcome::kNone ||
              stones.GetOutcomeAge() < quest.GetOutcomeAge())) {
    if (stones.GetOutcome() == StonesQuest::Outcome::kCompleted) {
      hud.banner = "Quest complete!";
      hud.banner_success = true;
      hud.banner_detail = stones.GetReport() == StonesQuest::Report::kTruth
                              ? "Chonk found its long lost snack stash"
                              : "Chonk will go and see for itself";
    } else {
      hud.banner = "Stopped exploring";
      hud.banner_detail = string("Talk to ") + StonesQuest::kSpeaker +
                          " to go to the stones again.";
    }
  } else if (quest.GetStage() == RaceQuest::Stage::kIdle &&
             quest.GetOutcomeAge() < banner_time) {
    const string again =
        string("Talk to ") + RaceQuest::kSpeaker + " to try again.";
    switch (quest.GetOutcome()) {
      case RaceQuest::Outcome::kCompleted:
        hud.banner = "Race complete!";
        hud.banner_success = true;
        hud.banner_detail =
            "Your time: " + RaceQuest::FormatTime(quest.GetRaceTime()) +
            (quest.IsNewBest()
                 ? (quest.GetCompletions() > 1 ? "  -  a new best time!" : "")
                 : "  -  best: " + RaceQuest::FormatTime(quest.GetBestTime()));
        break;
      case RaceQuest::Outcome::kTooSlow:
        hud.banner = "Race failed";
        hud.banner_detail = "Out of time. " + again;
        break;
      case RaceQuest::Outcome::kLeftTrack:
        hud.banner = "Race failed";
        hud.banner_detail = "You left the track. " + again;
        break;
      case RaceQuest::Outcome::kFellIntoWater:
        hud.banner = "Race failed";
        hud.banner_detail = "You fell into the water. " + again;
        break;
      case RaceQuest::Outcome::kNone:
        break;
    }
  }
  menu->SetHud(hud);
}

/**
 * Start (Options on a DualSense) pauses and resumes the game, the D-pad (or
 * left stick), A (Cross) and B (Circle) work the menus like the arrow keys,
 * ENTER and ESC
 */
void HandleGamepad() {
  if (!controls->HasGamepad()) {
    return;
  }
  if (game_state == kPlaying) {
    if (controls->WasPressed(GLFW_GAMEPAD_BUTTON_START)) {
      PauseGame();
    }
    return;
  }
  if (game_state == kPaused &&
      controls->WasPressed(GLFW_GAMEPAD_BUTTON_START)) {
    ResumeGame();
    return;
  }
  for (int key : controls->GetMenuKeys()) {
    KeyCallback(window, key, 0, GLFW_PRESS, 0);
    KeyCallback(window, key, 0, GLFW_RELEASE, 0);
  }
}

void JoystickCallback(int joystick, int event) {
  if (event == GLFW_CONNECTED) {
    const char* name = glfwGetJoystickName(joystick);
    last_message = glfwJoystickIsGamepad(joystick)
                       ? string("Gamepad connected: ") + (name ? name : "")
                       : string("Controller without a gamepad mapping: ") +
                             (name ? name : "") + " (add it to " +
                             input::Input::kMappingsFile + ")";
    message_time_left = 4;
  } else if (event == GLFW_DISCONNECTED) {
    last_message = "Gamepad disconnected";
    message_time_left = 4;
    if (game_state == kPlaying && menu) {
      PauseGame();  // e.g. the battery is empty
    }
  }
}

render::RenderSettings CurrentRenderSettings() {
  render::RenderSettings result;
  result.hdr = settings.hdr;
  result.bloom = settings.bloom;
  result.shadows = settings.shadows;
  result.normal_mapping = settings.normal_mapping;
  result.transparency = settings.transparency;
  result.wireframe = settings.wireframe;
  return result;
}

void Draw() {
  // Shadow maps, the scene in HDR, bloom, exposure and tone mapping
  // (render::Renderer)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  renderer->Render(*game_scene, CurrentRenderSettings(), time_delta);
  // The HUD on top of the final image
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  TextRendering();
  glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Cleanup() {
  cout << "Cleaning up memory..." << endl;
  // Releases the OpenGL resources of the menus, so it must happen before the
  // window (and its context) is destroyed
  menu.reset();
  music.reset();
  game_scene.reset();
  renderer.reset();
  // The glyphs and the quad of the text
  for (const auto& [c, glyph] : characters) {
    glDeleteTextures(1, &glyph.texture_id);
  }
  characters.clear();
  glDeleteBuffers(1, &text_vbo);
  glDeleteVertexArrays(1, &text_vao);
  text_shader.reset();
  main_shader.reset();
  // The scene (and the character controller of the bear) must be released
  // before the physics world
  physics_world.reset();
}

void InitPhysics() {
  cout << "Initializing Jolt Physics..." << endl;
  physics_world = make_unique<physics::PhysicsWorld>();
}

// Based on
// http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void InitFreeType() {
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
    characters.emplace(
        c, Character{texture, ivec2(glyph->bitmap.width, glyph->bitmap.rows),
                     ivec2(glyph->bitmap_left, glyph->bitmap_top),
                     static_cast<GLuint>(glyph->advance.x)});
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  FT_Done_Face(face);
  FT_Done_FreeType(ft);
}

void RenderText(Shader* shader, const string& text, GLfloat x, GLfloat y,
                GLfloat scale, vec3 color) {
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);  // the text is always in front of the scene
  shader->Enable();
  glUniform3f(shader->GetUniformLocation("textColor"), color.x, color.y,
              color.z);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(text_vao);
  for (char c : text) {
    auto glyph = characters.find(c);
    if (glyph == characters.end()) {
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
    glBindTexture(GL_TEXTURE_2D, ch.texture_id);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
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

void TextRendering() {
  if (game_state == kMainMenu) {
    return;  // the main menu covers the HUD
  }
  if (settings.show_help) {
    const char* const help[] = {
        "WASD or arrow keys / left stick to run",
        "Mouse / right stick to look around, wheel to zoom",
        "SPACE / Cross or A to jump (hold to jump higher)",
        "E / Square or X to talk, R to restart",
        "Bober the beaver at the start has a race for you!",
        "Mishka at the largest pool wants you to go fishing",
        "Chonk the raccoon at the trailhead wonders about the standing stones"};
    // A line per tenth of the window, below the frame time
    for (int line = 0; line < 7; ++line) {
      RenderText(text_shader.get(), help[line], window_width / 10.f,
                 window_height - (2.f + line) * window_height / 10.f, 0.6f,
                 vec3(0.5, 0.8f, 0.2f));
    }
  }
  if (settings.show_frame_time) {
    RenderText(text_shader.get(),
               "FPS: " + to_string(1.f / time_delta).substr(0, 4),
               window_width / 10.f, window_height - 1.f * window_height / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    RenderText(text_shader.get(),
               "Frametime: " + to_string(time_delta).substr(0, 5),
               window_width / 3.f, window_height - 1.f * window_height / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    // Fewer objects are drawn with view frustum culling (F8)
    string drawn_objects = to_string(game_scene->GetDrawnGeometryCount()) +
                           "/" + to_string(game_scene->GetGeometryCount());
    RenderText(text_shader.get(), "Objects: " + drawn_objects,
               window_width * 0.62f, window_height - 1.f * window_height / 10.f,
               0.6f, vec3(0.9, 0.5f, 0.5f));
    // The streamed world: chunks drawn and loaded (per level of detail), drawn
    // plants and stones, jobs of the worker threads
    world::World::Statistics world = game_scene->GetWorld().GetStatistics();
    string lods;
    for (int count : world.streaming.chunks_at_lod) {
      lods += (lods.empty() ? "" : "/") + to_string(count);
    }
    RenderText(text_shader.get(),
               "Chunks: " + to_string(world.drawn_chunks) + " drawn (LOD " +
                   lods + ")  Plants: " + to_string(world.drawn_instances) +
                   "  Jobs: " + to_string(world.streaming.pending_jobs),
               window_width / 10.f,
               window_height - 1.45f * window_height / 10.f, 0.4f,
               vec3(0.9, 0.5f, 0.5f));
  }

  // Not under the conversation box
  if (ShownDialog().IsOpen() && menu->HasHud()) {
    return;
  }
  if (message_time_left > 0) {
    RenderText(text_shader.get(), last_message, window_width / 10.f,
               window_height / 10.f, 0.4f, vec3(0.7, 0.5f, 0.8f));
  }

  // Without the HUD document (e.g. missing UI folder) the quests are shown as
  // plain text
  if (!menu->HasHud()) {
    const RaceQuest& quest = game_scene->GetRaceQuest();
    const SalmonQuest& fishing = game_scene->GetSalmonQuest();
    const Dialog& dialog = ShownDialog();
    vec3 color(0.95f, 0.85f, 0.4f);
    if (dialog.IsOpen()) {
      RenderText(text_shader.get(),
                 dialog.GetSpeaker() + ": " + dialog.GetText(),
                 window_width / 10.f, 3.f * window_height / 10.f, 0.4f, color);
      string choices;
      for (std::size_t i = 0;
           i < dialog.GetChoices().size() && dialog.IsChoosing(); ++i) {
        bool selected = static_cast<int>(i) == dialog.GetSelectedChoice();
        choices += (selected ? "[" : " ") + dialog.GetChoices()[i] +
                   (selected ? "]  " : "   ");
      }
      RenderText(text_shader.get(), choices, window_width / 10.f,
                 2.5f * window_height / 10.f, 0.45f, color);
    } else if (!TalkPrompt().empty()) {
      RenderText(text_shader.get(), "E: " + TalkPrompt(), window_width / 10.f,
                 3.f * window_height / 10.f, 0.45f, color);
    }
    if (!game_scene->GetStonesQuest().GetObjective().empty()) {
      RenderText(text_shader.get(), game_scene->GetStonesQuest().GetObjective(),
                 window_width / 10.f, 2.f * window_height / 10.f, 0.5f, color);
    }
    if (fishing.IsActive()) {
      string salmon = RaceQuest::FormatTime(fishing.GetTime()) +
                      "   Salmon: " + to_string(fishing.GetEatenCount()) + "/" +
                      to_string(fishing.GetPoolCount()) +
                      "   Score: " + to_string(fishing.GetScore());
      RenderText(text_shader.get(), salmon, window_width / 10.f,
                 2.f * window_height / 10.f, 0.5f, color);
    }
    int countdown = quest.GetCountdownNumber();
    if (countdown >= 0) {
      RenderText(text_shader.get(),
                 countdown > 0 ? to_string(countdown) : "GO!",
                 window_width / 2.f, window_height / 2.f, 1.5f, color);
    }
    if (quest.GetStage() == RaceQuest::Stage::kRacing) {
      string race =
          RaceQuest::FormatTime(quest.GetRaceTime()) +
          "   Checkpoints: " + to_string(quest.GetCheckpointsPassed()) + "/" +
          to_string(quest.GetCheckpointCount());
      RenderText(text_shader.get(), race, window_width / 10.f,
                 2.f * window_height / 10.f, 0.5f, color);
    }
  }
}

void FKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                  int mode) {
  // The F-keys change the same settings as the settings menu
  if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
    settings.show_help = !settings.show_help;
  }

  if (key == GLFW_KEY_F2 && action == GLFW_PRESS) {
    settings.show_frame_time = !settings.show_frame_time;
    string frame_time_msg = settings.show_frame_time ? "enabled" : "disabled";
    last_message = "Frame/time display " + frame_time_msg;
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F3 && action == GLFW_PRESS) {
    settings.wireframe = !settings.wireframe;
    ApplyGraphicsSettings();
    string wire_frame_mode_msg = settings.wireframe ? "enabled" : "disabled";
    last_message = "Wireframe mode " + wire_frame_mode_msg;
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F4 && action == GLFW_PRESS) {
    settings.bilinear_filtering = !settings.bilinear_filtering;
    ApplyGraphicsSettings();
    string bilinear_msg =
        settings.bilinear_filtering ? "bilinear" : "nearest neighbor";
    last_message = "Texture sampling: " + bilinear_msg;
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F5 && action == GLFW_PRESS) {
    settings.mipmapping = (settings.mipmapping + 1) % 3;
    ApplyGraphicsSettings();

    string mipmap_msg;
    switch (settings.mipmapping) {
      case 0:
        mipmap_msg = "OFF";
        break;
      case 1:
        mipmap_msg = "Nearest Neighbor";
        break;
      case 2:
        mipmap_msg = "Bilinear";
        break;
    }
    last_message = "Texture sampling: " + mipmap_msg;
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F6 && action == GLFW_PRESS) {
    settings.normal_mapping = !settings.normal_mapping;
    ApplyGraphicsSettings();
    last_message =
        "Normal Mapping enabled: " + to_string(settings.normal_mapping);
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F7 && action == GLFW_PRESS) {
    settings.shadows = !settings.shadows;
    ApplyGraphicsSettings();
    last_message =
        "Shadow Mapping (PCF) enabled: " + to_string(settings.shadows);
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F8 && action == GLFW_PRESS) {
    bool view_frustum_culling_enabled =
        !game_scene->IsViewFrustumCullingEnabled();
    game_scene->SetViewFrustumCullingEnabled(view_frustum_culling_enabled);
    last_message = "View Frustum Culling enabled: " +
                   to_string(view_frustum_culling_enabled);
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F9 && action == GLFW_PRESS) {
    settings.transparency = !settings.transparency;
    ApplyGraphicsSettings();
    last_message = "Transparency enabled: " + to_string(settings.transparency);
    message_time_left = 3;
  }

  if (key == GLFW_KEY_F10 && action == GLFW_PRESS) {
    settings.hdr = !settings.hdr;
    last_message = "HDR enabled: " + to_string(settings.hdr);
    message_time_left = 3;
  }

  if (settings.hdr && key == GLFW_KEY_F11 && action == GLFW_PRESS) {
    settings.bloom = !settings.bloom;
    last_message = "Bloom (requires HDR) enabled: " + to_string(settings.bloom);
    message_time_left = 3;
  }
}

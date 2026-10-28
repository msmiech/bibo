#include "Menu.hpp"

#include <RmlUi/Core.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "../Util/Display.hpp"

namespace bibo::ui {

using std::cerr;
using std::cout;
using std::endl;
using std::function;
using std::max;
using std::string;
using std::to_string;
using std::vector;

namespace {
// The menus are laid out for a height of 720 dp, which is scaled to the height
// of the framebuffer
const float kLayoutHeight = 720.0f;

// Settings that are applied to the game as soon as they are changed in the
// settings menu
const char* const kGraphicsVariables[] = {"vsync",
                                          "hdr",
                                          "bloom",
                                          "shadows",
                                          "normal_mapping",
                                          "transparency",
                                          "bilinear_filtering",
                                          "mipmapping",
                                          "wireframe",
                                          "world_detail",
                                          "show_frame_time",
                                          "show_help",
                                          "music",
                                          "sound_effects"};

/**
 * GLFW system interface of RmlUi that prints its messages (e.g. errors in the
 * RML and RCSS files) to the console
 */
class ConsoleSystemInterface : public SystemInterface_GLFW {
 public:
  using SystemInterface_GLFW::SystemInterface_GLFW;

  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
    switch (type) {
      case Rml::Log::LT_ERROR:
      case Rml::Log::LT_ASSERT:
        cerr << "RmlUi ERROR: " << message << endl;
        break;
      case Rml::Log::LT_WARNING:
        cerr << "RmlUi WARNING: " << message << endl;
        break;
      case Rml::Log::LT_INFO:
        cout << "RmlUi: " << message << endl;
        break;
      default:
        break;
    }
    return true;
  }
};
}  // namespace

struct Menu::Impl : public Rml::EventListener {
  Impl(GLFWwindow* window, Settings& settings,
       function<void(MenuAction)> on_action);
  ~Impl();

  void BindDataModel();
  void BindHudModel();
  Rml::ElementDocument* LoadDocument(const string& path);
  void Show(Rml::ElementDocument* document,
            Rml::FocusFlag focus = Rml::FocusFlag::Auto);
  void ShowSettings();
  bool Back();
  void RefreshDisplayOptions();
  void UpdateDisplayChanged();
  void ApplyDisplay();
  void Resize(int width, int height);
  // Mouse move listener of the documents
  void ProcessEvent(Rml::Event& event) override;

  GLFWwindow* window;
  Settings& settings;
  function<void(MenuAction)> on_action;
  // Actions of the buttons are collected while RmlUi processes the input and
  // are handled in Update()
  vector<MenuAction> pending_actions;

  ConsoleSystemInterface system_interface;
  RenderInterface_GL3 render_interface;
  Rml::Context* context = nullptr;
  Rml::DataModelHandle model;
  // GLFW only passes the modifier keys to the key and mouse button callbacks
  int modifiers = 0;

  Rml::ElementDocument* main_menu = nullptr;
  Rml::ElementDocument* pause_menu = nullptr;
  Rml::ElementDocument* settings_menu = nullptr;
  Rml::ElementDocument* visible_menu = nullptr;
  Rml::ElementDocument* settings_opened_from = nullptr;

  // Window mode and resolution selected in the settings menu, only applied with
  // its Apply button
  int window_mode = kWindowed;
  int resolution = 0;  // index in resolutions
  vector<display::Resolution> resolutions;
  Rml::Vector<Rml::String> resolution_names;
  bool display_changed = false;

  // The HUD of the quests (data model "hud")
  Rml::DataModelHandle hud_model;
  Rml::ElementDocument* hud_document = nullptr;
  Hud hud;
};

Menu::Impl::Impl(GLFWwindow* window, Settings& settings,
                 function<void(MenuAction)> on_action)
    : window(window),
      settings(settings),
      on_action(std::move(on_action)),
      system_interface(window) {
  if (!render_interface) {
    cerr << "ERROR: Could not create the RmlUi renderer, the menus are not "
            "available"
         << endl;
    return;
  }
  Rml::SetSystemInterface(&system_interface);
  Rml::SetRenderInterface(&render_interface);
  Rml::Initialise();

  // Fallback face: also used for characters that are missing in other fonts
  Rml::LoadFontFace("Font/DejaVuSans.ttf", true);

  int width, height;
  glfwGetFramebufferSize(window, &width, &height);
  context =
      Rml::CreateContext("menu", Rml::Vector2i(max(width, 1), max(height, 1)));
  if (!context) {
    return;
  }
  Resize(width, height);

  // The data model has to exist before the documents that use it are loaded
  BindDataModel();
  BindHudModel();
  main_menu = LoadDocument("UI/main_menu.rml");
  pause_menu = LoadDocument("UI/pause_menu.rml");
  settings_menu = LoadDocument("UI/settings.rml");
  // Not a menu: it gets no input and stays under the menus
  hud_document = context->LoadDocument("UI/hud.rml");
}

Menu::Impl::~Impl() {
  if (render_interface) {
    // Releases the documents and their textures and geometry, so it must happen
    // before the render interface is destroyed
    Rml::Shutdown();
  }
}

/**
 * Variables and event callbacks of the data model "menu", which the documents
 * use in their data bindings, e.g. <input type="checkbox" data-checked="hdr"/>
 * or <button data-event-click="start">
 */
void Menu::Impl::BindDataModel() {
  Rml::DataModelConstructor constructor = context->CreateDataModel("menu");
  if (!constructor) {
    return;
  }
  constructor.RegisterArray<Rml::Vector<Rml::String>>();

  constructor.Bind("vsync", &settings.vsync);
  constructor.Bind("hdr", &settings.hdr);
  constructor.Bind("bloom", &settings.bloom);
  constructor.Bind("shadows", &settings.shadows);
  constructor.Bind("normal_mapping", &settings.normal_mapping);
  constructor.Bind("transparency", &settings.transparency);
  constructor.Bind("bilinear_filtering", &settings.bilinear_filtering);
  constructor.Bind("mipmapping", &settings.mipmapping);
  constructor.Bind("wireframe", &settings.wireframe);
  constructor.Bind("world_detail", &settings.world_detail);
  constructor.Bind("show_frame_time", &settings.show_frame_time);
  constructor.Bind("show_help", &settings.show_help);
  constructor.Bind("camera_sensitivity", &settings.camera_sensitivity);
  constructor.Bind("invert_camera_y", &settings.invert_camera_y);

  constructor.Bind("window_mode", &window_mode);
  constructor.Bind("resolution", &resolution);
  constructor.Bind("resolutions", &resolution_names);
  constructor.Bind("display_changed", &display_changed);
  constructor.Bind("music", &settings.music);
  constructor.Bind("sound_effects", &settings.sound_effects);

  auto action = [this](MenuAction menu_action) {
    return [this, menu_action](Rml::DataModelHandle, Rml::Event&,
                               const Rml::VariantList&) {
      pending_actions.push_back(menu_action);
    };
  };
  constructor.BindEventCallback("start", action(MenuAction::kStart));
  constructor.BindEventCallback("resume", action(MenuAction::kResume));
  constructor.BindEventCallback("quit", action(MenuAction::kQuit));
  constructor.BindEventCallback("restart", action(MenuAction::kRestart));
  constructor.BindEventCallback("main_menu", action(MenuAction::kMainMenu));
  constructor.BindEventCallback(
      "settings", [this](Rml::DataModelHandle, Rml::Event&,
                         const Rml::VariantList&) { ShowSettings(); });
  constructor.BindEventCallback("back",
                                [this](Rml::DataModelHandle, Rml::Event&,
                                       const Rml::VariantList&) { Back(); });
  constructor.BindEventCallback(
      "apply_display", [this](Rml::DataModelHandle, Rml::Event&,
                              const Rml::VariantList&) { ApplyDisplay(); });

  model = constructor.GetModelHandle();
}

/**
 * Variables of the data model "hud", which the HUD of the quests (hud.rml)
 * shows
 */
void Menu::Impl::BindHudModel() {
  Rml::DataModelConstructor constructor = context->CreateDataModel("hud");
  if (!constructor) {
    return;
  }
  // Arrays of strings were registered for the menus (the types are shared by
  // the models of a context)
  constructor.Bind("prompt", &hud.prompt);
  constructor.Bind("prompt_key", &hud.prompt_key);
  constructor.Bind("speaker", &hud.speaker);
  constructor.Bind("text", &hud.text);
  constructor.Bind("can_continue", &hud.can_continue);
  constructor.Bind("continue_key", &hud.continue_key);
  constructor.Bind("choices", &hud.choices);
  constructor.Bind("selected_choice", &hud.selected_choice);
  constructor.Bind("countdown", &hud.countdown);
  constructor.Bind("racing", &hud.racing);
  constructor.Bind("race_time", &hud.race_time);
  constructor.Bind("checkpoints", &hud.checkpoints);
  constructor.Bind("time_limit", &hud.time_limit);
  constructor.Bind("fishing", &hud.fishing);
  constructor.Bind("fishing_time", &hud.fishing_time);
  constructor.Bind("salmon", &hud.salmon);
  constructor.Bind("score", &hud.score);
  constructor.Bind("objective", &hud.objective);
  constructor.Bind("objective_detail", &hud.objective_detail);
  constructor.Bind("banner", &hud.banner);
  constructor.Bind("banner_detail", &hud.banner_detail);
  constructor.Bind("banner_success", &hud.banner_success);
  hud_model = constructor.GetModelHandle();
}

Rml::ElementDocument* Menu::Impl::LoadDocument(const string& path) {
  // RmlUi reports documents that cannot be loaded or parsed through the system
  // interface
  Rml::ElementDocument* document = context->LoadDocument(path);
  if (document) {
    document->AddEventListener(Rml::EventId::Mousemove, this);
  }
  return document;
}

/**
 * The button under the mouse gets the focus, so that the mouse and the arrow
 * keys always highlight the same button
 */
void Menu::Impl::ProcessEvent(Rml::Event& event) {
  for (Rml::Element* element = event.GetTargetElement(); element;
       element = element->GetParentNode()) {
    bool is_button =
        element->GetTagName() == "button" ||
        (element->GetTagName() == "input" &&
         element->GetAttribute<Rml::String>("type", "") == "button");
    if (is_button) {
      if (!element->IsPseudoClassSet("focus") &&
          !element->IsPseudoClassSet("disabled")) {
        element->Focus(true);
      }
      return;
    }
  }
}

void Menu::Impl::Show(Rml::ElementDocument* document, Rml::FocusFlag focus) {
  for (Rml::ElementDocument* menu : {main_menu, pause_menu, settings_menu}) {
    if (menu && menu != document) {
      menu->Hide();
    }
  }
  if (document) {
    // FocusFlag::Auto focuses the element with the autofocus attribute (the
    // first button), Keep the last focused one
    document->Show(Rml::ModalFlag::None, focus);
    document->PullToFront();  // over the HUD
  }
  visible_menu = document;
}

void Menu::Impl::ShowSettings() {
  settings_opened_from = visible_menu;
  RefreshDisplayOptions();
  if (model) {
    // The settings may have been changed with the F-keys while the menus were
    // hidden
    model.DirtyAllVariables();
  }
  Show(settings_menu);
}

bool Menu::Impl::Back() {
  if (!settings_menu || visible_menu != settings_menu) {
    return false;
  }
  // Unapplied changes of the window mode and resolution are discarded, they are
  // reset when the menu is opened again
  Show(settings_opened_from, Rml::FocusFlag::Keep);
  pending_actions.push_back(MenuAction::kSettingsClosed);
  return true;
}

/**
 * Fills the resolution list with the video modes of the monitor the window is
 * on and selects the current window mode and resolution
 */
void Menu::Impl::RefreshDisplayOptions() {
  resolutions = display::GetResolutions(display::GetMonitor(window));
  display::Resolution current = {settings.width, settings.height};
  if (find(resolutions.begin(), resolutions.end(), current) ==
      resolutions.end()) {
    // e.g. the size of a window that was resized by dragging its border,
    // inserted so that the list stays sorted
    auto smaller = find_if(resolutions.begin(), resolutions.end(),
                           [&current](const display::Resolution& resolution) {
                             return resolution.width != current.width
                                        ? resolution.width < current.width
                                        : resolution.height < current.height;
                           });
    resolutions.insert(smaller, current);
  }

  resolution_names.clear();
  for (const auto& entry : resolutions) {
    resolution_names.push_back(
        // multiplication sign (UTF-8)
        to_string(entry.width) + " \xC3\x97 " + to_string(entry.height));
  }
  resolution =
      static_cast<int>(find(resolutions.begin(), resolutions.end(), current) -
                       resolutions.begin());
  window_mode = settings.window_mode;
  display_changed = false;

  if (model) {
    model.DirtyVariable("resolutions");
    model.DirtyVariable("resolution");
    model.DirtyVariable("window_mode");
    model.DirtyVariable("display_changed");
  }
}

void Menu::Impl::UpdateDisplayChanged() {
  bool resolution_changed =
      resolution >= 0 && resolution < static_cast<int>(resolutions.size()) &&
      !(resolutions[resolution] ==
        display::Resolution{settings.width, settings.height});
  // Borderless windows always have the resolution of the desktop
  display_changed = window_mode != settings.window_mode ||
                    (window_mode != kBorderless && resolution_changed);
  model.DirtyVariable("display_changed");
}

void Menu::Impl::ApplyDisplay() {
  if (!display_changed) {
    return;
  }
  settings.window_mode = window_mode;
  if (window_mode != kBorderless && resolution >= 0 &&
      resolution < static_cast<int>(resolutions.size())) {
    settings.width = resolutions[resolution].width;
    settings.height = resolutions[resolution].height;
  }
  pending_actions.push_back(MenuAction::kApplyDisplay);
}

void Menu::Impl::Resize(int width, int height) {
  if (width <= 0 || height <= 0) {
    return;  // minimized
  }
  render_interface.SetViewport(width, height);
  context->SetDimensions(Rml::Vector2i(width, height));
  context->SetDensityIndependentPixelRatio(height / kLayoutHeight);
}

Menu::Menu(GLFWwindow* window, Settings& settings,
           function<void(MenuAction)> on_action)
    : impl_(make_unique<Impl>(window, settings, std::move(on_action))) {}

Menu::~Menu() = default;

void Menu::ShowMainMenu() { impl_->Show(impl_->main_menu); }

void Menu::ShowPauseMenu() { impl_->Show(impl_->pause_menu); }

void Menu::SetHud(const Hud& hud) {
  Impl& menu = *impl_;
  if (!menu.hud_document || !menu.hud_model || hud == menu.hud) {
    return;
  }
  if (hud.visible != menu.hud.visible) {
    if (hud.visible) {
      menu.hud_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
      menu.hud_document->PushToBack();  // under the menus
    } else {
      menu.hud_document->Hide();
    }
  }
  menu.hud = hud;
  menu.hud_model.DirtyAllVariables();
}

bool Menu::HasHud() const {
  return impl_->hud_document != nullptr && impl_->hud_model;
}

void Menu::Hide() { impl_->Show(nullptr); }

bool Menu::Back() { return impl_->Back(); }

bool Menu::IsVisible() const { return impl_->visible_menu != nullptr; }

void Menu::OnKey(int key, int action, int mods) {
  impl_->modifiers = mods;
  RmlGLFW::ProcessKeyCallback(impl_->context, key, action, mods);
}

void Menu::OnChar(unsigned int codepoint) {
  RmlGLFW::ProcessCharCallback(impl_->context, codepoint);
}

void Menu::OnCursorPos(double x, double y) {
  RmlGLFW::ProcessCursorPosCallback(impl_->context, impl_->window, x, y,
                                    impl_->modifiers);
}

void Menu::OnCursorEnter(bool entered) {
  RmlGLFW::ProcessCursorEnterCallback(impl_->context, entered);
}

void Menu::OnMouseButton(int button, int action, int mods) {
  impl_->modifiers = mods;
  RmlGLFW::ProcessMouseButtonCallback(impl_->context, button, action, mods);
}

void Menu::OnScroll(double y_offset) {
  RmlGLFW::ProcessScrollCallback(impl_->context, y_offset, impl_->modifiers);
}

void Menu::OnFramebufferResized(int width, int height) {
  if (impl_->context) {
    impl_->Resize(width, height);
  }
}

void Menu::Update() {
  Impl& menu = *impl_;
  if (!menu.context) {
    return;
  }

  // The data bindings change the variables while RmlUi processes the input,
  // they stay dirty until the context is updated
  if (menu.model) {
    for (const char* variable : kGraphicsVariables) {
      if (menu.model.IsVariableDirty(variable)) {
        menu.pending_actions.push_back(MenuAction::kGraphicsChanged);
        break;
      }
    }
    if (menu.model.IsVariableDirty("window_mode") ||
        menu.model.IsVariableDirty("resolution")) {
      menu.UpdateDisplayChanged();
    }
  }

  // Handled outside of the input processing of RmlUi, because the game may e.g.
  // resize the window and thereby the context
  vector<MenuAction> actions;
  actions.swap(menu.pending_actions);
  for (MenuAction action : actions) {
    menu.on_action(action);
    if (action == MenuAction::kApplyDisplay) {
      // e.g. the window may be on another monitor now
      menu.RefreshDisplayOptions();
    }
  }

  menu.context->Update();
}

void Menu::Render() {
  if (!impl_->context || (!impl_->visible_menu && !impl_->hud.visible)) {
    return;
  }
  impl_->render_interface.BeginFrame();
  impl_->context->Render();
  impl_->render_interface.EndFrame();
}

}  // namespace bibo::ui

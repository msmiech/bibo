#include "Menu.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <RmlUi/Core.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>
#include "../Util/Display.hpp"

using namespace std;
using namespace bibo;
using namespace ui;

namespace
{
	//The menus are laid out for a height of 720 dp, which is scaled to the height of the framebuffer
	const float LAYOUT_HEIGHT = 720.0f;

	//Settings that are applied to the game as soon as they are changed in the settings menu
	const char* const GRAPHICS_VARIABLES[] = { "vsync", "hdr", "bloom", "shadows", "normal_mapping", "transparency",
		"bilinear_filtering", "mipmapping", "wireframe", "world_detail", "show_frame_time", "show_help" };

	/**
	* GLFW system interface of RmlUi that prints its messages (e.g. errors in the RML and RCSS files) to the console
	*/
	class ConsoleSystemInterface : public SystemInterface_GLFW
	{
	public:
		using SystemInterface_GLFW::SystemInterface_GLFW;

		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			switch (type)
			{
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
}

struct Menu::Impl : public Rml::EventListener
{
	Impl(GLFWwindow* window, Settings& settings, function<void(MenuAction)> onAction);
	~Impl();

	void bindDataModel();
	Rml::ElementDocument* loadDocument(const string& path);
	void show(Rml::ElementDocument* document, Rml::FocusFlag focus = Rml::FocusFlag::Auto);
	void showSettings();
	bool back();
	void refreshDisplayOptions();
	void updateDisplayChanged();
	void applyDisplay();
	void resize(int width, int height);
	//Mouse move listener of the documents
	void ProcessEvent(Rml::Event& event) override;

	GLFWwindow* window;
	Settings& settings;
	function<void(MenuAction)> onAction;
	//Actions of the buttons are collected while RmlUi processes the input and are handled in update()
	vector<MenuAction> pendingActions;

	ConsoleSystemInterface systemInterface;
	RenderInterface_GL3 renderInterface;
	Rml::Context* context = nullptr;
	Rml::DataModelHandle model;
	int modifiers = 0; //GLFW only passes the modifier keys to the key and mouse button callbacks

	Rml::ElementDocument* mainMenu = nullptr;
	Rml::ElementDocument* pauseMenu = nullptr;
	Rml::ElementDocument* gameOverMenu = nullptr;
	Rml::ElementDocument* settingsMenu = nullptr;
	Rml::ElementDocument* visibleMenu = nullptr;
	Rml::ElementDocument* settingsOpenedFrom = nullptr;

	//Window mode and resolution selected in the settings menu, only applied with its Apply button
	int windowMode = WINDOWED;
	int resolution = 0; //index in resolutions
	vector<display::Resolution> resolutions;
	Rml::Vector<Rml::String> resolutionNames;
	bool displayChanged = false;

	bool gameWon = false; //title of the game over menu
};

Menu::Impl::Impl(GLFWwindow* window, Settings& settings, function<void(MenuAction)> onAction)
	: window(window), settings(settings), onAction(std::move(onAction)), systemInterface(window)
{
	if (!renderInterface)
	{
		cerr << "ERROR: Could not create the RmlUi renderer, the menus are not available" << endl;
		return;
	}
	Rml::SetSystemInterface(&systemInterface);
	Rml::SetRenderInterface(&renderInterface);
	Rml::Initialise();

	//Fallback face: also used for characters that are missing in other fonts
	Rml::LoadFontFace("Font/DejaVuSans.ttf", true);

	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	context = Rml::CreateContext("menu", Rml::Vector2i(max(width, 1), max(height, 1)));
	if (!context)
	{
		return;
	}
	resize(width, height);

	//The data model has to exist before the documents that use it are loaded
	bindDataModel();
	mainMenu = loadDocument("UI/main_menu.rml");
	pauseMenu = loadDocument("UI/pause_menu.rml");
	gameOverMenu = loadDocument("UI/game_over.rml");
	settingsMenu = loadDocument("UI/settings.rml");
}

Menu::Impl::~Impl()
{
	if (renderInterface)
	{
		//Releases the documents and their textures and geometry, so it must happen before the render interface is destroyed
		Rml::Shutdown();
	}
}

/**
* Variables and event callbacks of the data model "menu", which the documents use in their data bindings,
* e.g. <input type="checkbox" data-checked="hdr"/> or <button data-event-click="start">
*/
void Menu::Impl::bindDataModel()
{
	Rml::DataModelConstructor constructor = context->CreateDataModel("menu");
	if (!constructor)
	{
		return;
	}
	constructor.RegisterArray<Rml::Vector<Rml::String>>();

	constructor.Bind("vsync", &settings.vsync);
	constructor.Bind("hdr", &settings.hdr);
	constructor.Bind("bloom", &settings.bloom);
	constructor.Bind("shadows", &settings.shadows);
	constructor.Bind("normal_mapping", &settings.normalMapping);
	constructor.Bind("transparency", &settings.transparency);
	constructor.Bind("bilinear_filtering", &settings.bilinearFiltering);
	constructor.Bind("mipmapping", &settings.mipmapping);
	constructor.Bind("wireframe", &settings.wireframe);
	constructor.Bind("world_detail", &settings.worldDetail);
	constructor.Bind("show_frame_time", &settings.showFrameTime);
	constructor.Bind("show_help", &settings.showHelp);
	constructor.Bind("camera_sensitivity", &settings.cameraSensitivity);
	constructor.Bind("invert_camera_y", &settings.invertCameraY);

	constructor.Bind("window_mode", &windowMode);
	constructor.Bind("resolution", &resolution);
	constructor.Bind("resolutions", &resolutionNames);
	constructor.Bind("display_changed", &displayChanged);
	constructor.Bind("game_won", &gameWon);

	auto action = [this](MenuAction menuAction) {
		return [this, menuAction](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { pendingActions.push_back(menuAction); };
	};
	constructor.BindEventCallback("start", action(MenuAction::Start));
	constructor.BindEventCallback("resume", action(MenuAction::Resume));
	constructor.BindEventCallback("quit", action(MenuAction::Quit));
	constructor.BindEventCallback("restart", action(MenuAction::Restart));
	constructor.BindEventCallback("main_menu", action(MenuAction::MainMenu));
	constructor.BindEventCallback("settings", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { showSettings(); });
	constructor.BindEventCallback("back", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { back(); });
	constructor.BindEventCallback("apply_display", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { applyDisplay(); });

	model = constructor.GetModelHandle();
}

Rml::ElementDocument* Menu::Impl::loadDocument(const string& path)
{
	//RmlUi reports documents that cannot be loaded or parsed through the system interface
	Rml::ElementDocument* document = context->LoadDocument(path);
	if (document)
	{
		document->AddEventListener(Rml::EventId::Mousemove, this);
	}
	return document;
}

/**
* The button under the mouse gets the focus, so that the mouse and the arrow keys always highlight the same button
*/
void Menu::Impl::ProcessEvent(Rml::Event& event)
{
	for (Rml::Element* element = event.GetTargetElement(); element; element = element->GetParentNode())
	{
		bool isButton = element->GetTagName() == "button" ||
			(element->GetTagName() == "input" && element->GetAttribute<Rml::String>("type", "") == "button");
		if (isButton)
		{
			if (!element->IsPseudoClassSet("focus") && !element->IsPseudoClassSet("disabled"))
			{
				element->Focus(true);
			}
			return;
		}
	}
}

void Menu::Impl::show(Rml::ElementDocument* document, Rml::FocusFlag focus)
{
	for (Rml::ElementDocument* menu : { mainMenu, pauseMenu, gameOverMenu, settingsMenu })
	{
		if (menu && menu != document)
		{
			menu->Hide();
		}
	}
	if (document)
	{
		//FocusFlag::Auto focuses the element with the autofocus attribute (the first button), Keep the last focused one
		document->Show(Rml::ModalFlag::None, focus);
	}
	visibleMenu = document;
}

void Menu::Impl::showSettings()
{
	settingsOpenedFrom = visibleMenu;
	refreshDisplayOptions();
	if (model)
	{
		//The settings may have been changed with the F-keys while the menus were hidden
		model.DirtyAllVariables();
	}
	show(settingsMenu);
}

bool Menu::Impl::back()
{
	if (!settingsMenu || visibleMenu != settingsMenu)
	{
		return false;
	}
	//Unapplied changes of the window mode and resolution are discarded, they are reset when the menu is opened again
	show(settingsOpenedFrom, Rml::FocusFlag::Keep);
	pendingActions.push_back(MenuAction::SettingsClosed);
	return true;
}

/**
* Fills the resolution list with the video modes of the monitor the window is on and selects the current window mode and resolution
*/
void Menu::Impl::refreshDisplayOptions()
{
	resolutions = display::getResolutions(display::getMonitor(window));
	display::Resolution current = { settings.width, settings.height };
	if (find(resolutions.begin(), resolutions.end(), current) == resolutions.end())
	{
		//e.g. the size of a window that was resized by dragging its border, inserted so that the list stays sorted
		auto smaller = find_if(resolutions.begin(), resolutions.end(), [&current](const display::Resolution& resolution) {
			return resolution.width != current.width ? resolution.width < current.width : resolution.height < current.height;
		});
		resolutions.insert(smaller, current);
	}

	resolutionNames.clear();
	for (const auto& entry : resolutions)
	{
		resolutionNames.push_back(to_string(entry.width) + " \xC3\x97 " + to_string(entry.height)); //multiplication sign (UTF-8)
	}
	resolution = static_cast<int>(find(resolutions.begin(), resolutions.end(), current) - resolutions.begin());
	windowMode = settings.windowMode;
	displayChanged = false;

	if (model)
	{
		model.DirtyVariable("resolutions");
		model.DirtyVariable("resolution");
		model.DirtyVariable("window_mode");
		model.DirtyVariable("display_changed");
	}
}

void Menu::Impl::updateDisplayChanged()
{
	bool resolutionChanged = resolution >= 0 && resolution < static_cast<int>(resolutions.size()) &&
		!(resolutions[resolution] == display::Resolution{ settings.width, settings.height });
	//Borderless windows always have the resolution of the desktop
	displayChanged = windowMode != settings.windowMode || (windowMode != BORDERLESS && resolutionChanged);
	model.DirtyVariable("display_changed");
}

void Menu::Impl::applyDisplay()
{
	if (!displayChanged)
	{
		return;
	}
	settings.windowMode = windowMode;
	if (windowMode != BORDERLESS && resolution >= 0 && resolution < static_cast<int>(resolutions.size()))
	{
		settings.width = resolutions[resolution].width;
		settings.height = resolutions[resolution].height;
	}
	pendingActions.push_back(MenuAction::ApplyDisplay);
}

void Menu::Impl::resize(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return; //minimized
	}
	renderInterface.SetViewport(width, height);
	context->SetDimensions(Rml::Vector2i(width, height));
	context->SetDensityIndependentPixelRatio(height / LAYOUT_HEIGHT);
}

Menu::Menu(GLFWwindow* window, Settings& settings, function<void(MenuAction)> onAction)
	: impl(make_unique<Impl>(window, settings, std::move(onAction)))
{
}

Menu::~Menu() = default;

void Menu::showMainMenu()
{
	impl->show(impl->mainMenu);
}

void Menu::showPauseMenu()
{
	impl->show(impl->pauseMenu);
}

void Menu::showGameOverMenu(bool won)
{
	impl->gameWon = won;
	if (impl->model)
	{
		impl->model.DirtyVariable("game_won");
	}
	impl->show(impl->gameOverMenu);
}

void Menu::hide()
{
	impl->show(nullptr);
}

bool Menu::back()
{
	return impl->back();
}

bool Menu::isVisible() const
{
	return impl->visibleMenu != nullptr;
}

void Menu::onKey(int key, int action, int mods)
{
	impl->modifiers = mods;
	RmlGLFW::ProcessKeyCallback(impl->context, key, action, mods);
}

void Menu::onChar(unsigned int codepoint)
{
	RmlGLFW::ProcessCharCallback(impl->context, codepoint);
}

void Menu::onCursorPos(double x, double y)
{
	RmlGLFW::ProcessCursorPosCallback(impl->context, impl->window, x, y, impl->modifiers);
}

void Menu::onCursorEnter(bool entered)
{
	RmlGLFW::ProcessCursorEnterCallback(impl->context, entered);
}

void Menu::onMouseButton(int button, int action, int mods)
{
	impl->modifiers = mods;
	RmlGLFW::ProcessMouseButtonCallback(impl->context, button, action, mods);
}

void Menu::onScroll(double yOffset)
{
	RmlGLFW::ProcessScrollCallback(impl->context, yOffset, impl->modifiers);
}

void Menu::onFramebufferResized(int width, int height)
{
	if (impl->context)
	{
		impl->resize(width, height);
	}
}

void Menu::update()
{
	Impl& menu = *impl;
	if (!menu.context)
	{
		return;
	}

	//The data bindings change the variables while RmlUi processes the input, they stay dirty until the context is updated
	if (menu.model)
	{
		for (const char* variable : GRAPHICS_VARIABLES)
		{
			if (menu.model.IsVariableDirty(variable))
			{
				menu.pendingActions.push_back(MenuAction::GraphicsChanged);
				break;
			}
		}
		if (menu.model.IsVariableDirty("window_mode") || menu.model.IsVariableDirty("resolution"))
		{
			menu.updateDisplayChanged();
		}
	}

	//Handled outside of the input processing of RmlUi, because the game may e.g. resize the window and thereby the context
	vector<MenuAction> actions;
	actions.swap(menu.pendingActions);
	for (MenuAction action : actions)
	{
		menu.onAction(action);
		if (action == MenuAction::ApplyDisplay)
		{
			menu.refreshDisplayOptions(); //e.g. the window may be on another monitor now
		}
	}

	menu.context->Update();
}

void Menu::render()
{
	if (!impl->context || !impl->visibleMenu)
	{
		return;
	}
	impl->renderInterface.BeginFrame();
	impl->context->Render();
	impl->renderInterface.EndFrame();
}

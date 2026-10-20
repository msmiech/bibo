#pragma once
#include <functional>
#include <memory>
#include <GLFW/glfw3.h>
#include "../Util/Settings.hpp"

namespace bibo
{
	namespace ui
	{
		enum class MenuAction
		{
			Start,           //Main menu: start the game
			Resume,          //Pause menu: continue the game
			Quit,            //Main and pause menu: quit the game
			Restart,         //Game over menu: start the run again
			MainMenu,        //Game over menu: back to the main menu
			GraphicsChanged, //Settings menu: a graphics setting was changed (they are applied immediately)
			ApplyDisplay,    //Settings menu: a new window mode or resolution was applied
			SettingsClosed   //Settings menu: closed with Back or ESC
		};

		/**
		* Main menu, pause menu and settings menu of the game, rendered with RmlUi on top of the scene.
		* The menus are RML documents with an RCSS style sheet (similar to HTML and CSS) in the UI folder.
		* The settings menu changes the Settings directly and reports the changes with a MenuAction.
		*/
		class Menu
		{
		public:
			/**
			* @param onAction Called from update() when a button was pressed or a setting was changed
			*/
			Menu(GLFWwindow* window, Settings& settings, std::function<void(MenuAction)> onAction);
			~Menu();
			Menu(const Menu&) = delete;
			Menu& operator=(const Menu&) = delete;

			void showMainMenu();
			void showPauseMenu();
			/**
			* Shown when the run is over (the bear fell off the track) or won
			* @param score Bonus points of the run, salmon: how many salmon the bear caught
			*/
			void showGameOverMenu(bool won, int score, int salmon);
			void hide();
			/**
			* Returns from the settings menu to the menu it was opened from (like its Back button)
			* @return false if the settings menu was not open
			*/
			bool back();
			bool isVisible() const;

			//Input, forwarded from the GLFW callbacks
			void onKey(int key, int action, int mods);
			void onChar(unsigned int codepoint);
			void onCursorPos(double x, double y);
			void onCursorEnter(bool entered);
			void onMouseButton(int button, int action, int mods);
			void onScroll(double yOffset);
			/**
			* Must be called when the framebuffer size (in pixels) changes. The menus are scaled to its height.
			*/
			void onFramebufferResized(int width, int height);

			void update();
			/**
			* Draws the visible menu over the content of the default framebuffer
			*/
			void render();

		private:
			struct Impl; //Keeps the RmlUi headers out of the game code
			std::unique_ptr<Impl> impl;
		};
	}
}

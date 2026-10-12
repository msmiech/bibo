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
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Scene/Scene.hpp"
#include "Physics/Physics.hpp"
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

//Only registered in debug builds
[[maybe_unused]] static void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam);
[[maybe_unused]] static void GLAPIENTRY DebugCallbackAMD(GLuint id, GLenum category, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam);
static string FormatDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, const char* msg);

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
void renderQuad();
void renderText(Shader* shader, string text, GLfloat x, GLfloat y, GLfloat scale, vec3 color);
void resizeScreenBuffers();
void applyGraphicsSettings();
bool pauseGame();
void resumeGame();
void showGameOver();
void restartGame();
void onMenuAction(ui::MenuAction action);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void fKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void framebufferSizeCallback(GLFWwindow* window, int width, int height);
void windowFocusCallback(GLFWwindow* window, int focused);
void joystickCallback(int joystick, int event);
void handleGamepad();

//vars
const char* const SETTINGS_FILE = "settings.cfg"; //in the working directory, like the assets
Settings settings;
unsigned int windowWidth = 800; //Size of the framebuffer in pixels
unsigned int windowHeight = 600;
GLFWwindow* window;
float time_delta; //Last frame time
//Longer frames (e.g. while the window is dragged) slow the game down instead of simulating a few huge steps
const float MAX_TIME_STEP = 0.1f;

//Demo (--demo) and recording (--record): the game runs with a fixed time step and the default settings,
//so that a script always plays the same way (e.g. tools/readme_demo.txt for the GIF of the README)
float fixedTimeStep = 0; //0: the time step is the duration of the last frame
bool useSavedSettings = true;
unique_ptr<DemoScript> demoScript;
unique_ptr<FrameRecorder> recorder;
bool recordingFailed = false;

enum GameState
{
	MAIN_MENU,
	PLAYING,
	PAUSED,
	GAME_OVER //lost or won, the game over menu offers to restart
};
GameState gameState = MAIN_MENU;
unique_ptr<ui::Menu> menu;
//Keyboard, mouse and gamepad of the player
unique_ptr<input::Input> controls;

unique_ptr<Shader> mainShader;
unique_ptr<Shader> depthShader;
unique_ptr<Shader> textShader;
unique_ptr<Shader> lightShader;
unique_ptr<Shader> blurShader;
unique_ptr<Shader> postProcessingShader;

unique_ptr<Scene> gameScene;

GLuint shadowFBO; //Frame Buffer Object (FBO) for depth (from Light POV!)
GLuint shadowDepthTexture;
GLuint hdrFBO;
GLuint colorBuffers[2];
GLuint depthBloomRBO; //Render Buffer Object (RBO) for depth (from Camera POV!)
GLuint blurFBO[2];
GLuint blurColorbuffers[2];
const GLuint SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;

#ifdef _WIN32
//Ask laptops with switchable graphics to run the game on the dedicated Nvidia/AMD GPU
extern "C" {
	__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
//Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
//Holds all state information relevant to a character as loaded using FreeType
struct Character {
	GLuint textureID;   // ID handle of the glyph texture
	ivec2 size;    // Size of glyph
	ivec2 bearing;  // Offset from baseline to left/top of glyph
	GLuint advance;    // Horizontal offset to advance to next glyph
};
map<GLchar, Character> Characters;
GLuint textVAO;
GLuint textVBO;
//Quad VAO/VBO for screen (space) drawing
GLuint quadVAO = 0;
GLuint quadVBO;
string lastMessage = "";
float messageTimeLeft = 0;

int main(int argc, char** argv)
{
	//Options anywhere on the command line: --demo <script> plays the input of the script, --record <file> writes every
	//frame to the file (raw video, see FrameRecorder), --fps <frames per second> of both (default 15)
	vector<string> arguments; //width height [fullscreen]
	string demoPath;
	string recordPath;
	int fps = 15;
	for (int i = 1; i < argc; ++i)
	{
		string argument = argv[i];
		bool hasValue = i + 1 < argc;
		if (argument == "--demo" && hasValue)
		{
			demoPath = argv[++i];
		}
		else if (argument == "--record" && hasValue)
		{
			recordPath = argv[++i];
		}
		else if (argument == "--fps" && hasValue)
		{
			if ((stringstream(argv[++i]) >> fps).fail() || fps <= 0 || fps > 1000)
			{
				util::fatalError("--fps needs the number of frames per second");
			}
		}
		else if (argument.rfind("--", 0) == 0)
		{
			util::fatalError("Unknown option " + argument + " (BIBO [--demo <script>] [--record <file>] [--fps <frames per second>] [width height [fullscreen]])");
		}
		else
		{
			arguments.push_back(argument);
		}
	}

	if (!demoPath.empty() || !recordPath.empty())
	{
		fixedTimeStep = 1.0f / fps;
		useSavedSettings = false;
		//It would show the fixed time step instead of the time it takes to draw a frame
		settings.showFrameTime = false;
	}
	if (!demoPath.empty())
	{
		demoScript = make_unique<DemoScript>();
		string error;
		if (!demoScript->load(demoPath, fps, error))
		{
			util::fatalError("Demo script " + demoPath + ": " + error);
		}
	}

	//Saved by the settings menu, the command line arguments take precedence
	if (useSavedSettings)
	{
		settings.load(SETTINGS_FILE);
	}

	if (arguments.size() >= 2) {
		cout << "You are running" << endl;

		if ((stringstream(arguments[0]) >> settings.width).fail() || settings.width <= 0)
		{
			cerr << "ERROR: Could not parse first element, try again!" << endl;
			util::pause();
			exit(EXIT_FAILURE);
		}

		if ((stringstream(arguments[1]) >> settings.height).fail() || settings.height <= 0) {
			cerr << "ERROR: Could not parse second element, try again!" << endl;
			util::pause();
			exit(EXIT_FAILURE);
		}

		if (arguments.size() >= 3) {
			bool fullscreen;
			if ((stringstream(arguments[2]) >> fullscreen).fail()) {

				cerr << "ERROR: Could not parse third command-line-arg, try again!" << endl;
				util::pause();
				exit(EXIT_FAILURE);
			}
			settings.windowMode = fullscreen ? FULLSCREEN : WINDOWED;
		}
	}

	cout << "Starting BIBO game" << endl;

	initialize();

	if (!recordPath.empty())
	{
		recorder = make_unique<FrameRecorder>(recordPath, windowWidth, windowHeight);
		if (!recorder->isOpen())
		{
			util::fatalError("Could not create " + recordPath);
		}
		cout << "Recording " << windowWidth << "x" << windowHeight << " pixels at " << fps << " fps to " << recordPath << endl;
	}

	gameLoop();

	if (recorder)
	{
		cout << "Recorded " << recorder->getFrameCount() << " frames" << endl;
		recorder.reset(nullptr);
	}

	cleanup();

	glfwDestroyWindow(window);

	glfwTerminate();

	return recordingFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void initialize()
{
	if (!glfwInit()) {
		cerr << "ERROR: Could not init glfw." << endl;
		util::pause();
		exit(EXIT_FAILURE);

	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifndef NDEBUG
	//Debug output (see initDebug), the extra checks of a debug context can slow down the driver
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	//Windowed, full screen or borderless (see settings)
	window = display::createWindow(settings.windowMode, settings.width, settings.height, "BIBO Project");

	if (!window) // window == nullptr
	{
		cerr << "ERROR: Could not create a window with an OpenGL 4.3 core context" << endl;
		glfwTerminate();
		util::pause();
		exit(EXIT_FAILURE);
	}
	//The framebuffer can be smaller than requested (e.g. limited to the screen) or larger (scaled on high DPI screens)
	int framebufferWidth, framebufferHeight;
	glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
	windowWidth = std::max(framebufferWidth, 1);
	windowHeight = std::max(framebufferHeight, 1);

	//Input goes to the game or, while it is visible, to the menu
	glfwSetKeyCallback(window, keyCallback);
	glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
		if (menu && menu->isVisible()) menu->onChar(codepoint);
	});
	glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int mods) {
		if (menu && menu->isVisible()) menu->onMouseButton(button, action, mods);
	});
	glfwSetScrollCallback(window, [](GLFWwindow*, double, double yOffset) {
		if (menu && menu->isVisible()) menu->onScroll(yOffset);
		else if (controls) controls->onScroll(yOffset); //zooms the camera
	});
	//The menu always knows where the mouse is, so that it highlights the right button as soon as it opens.
	//During the game the mouse is captured and turns the camera.
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

	//GLEW initialization
	glewExperimental = true; //Important for loading in old OSes
	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
	{
		cerr << "OpenGL Error: " << error << endl;
	}
	GLenum glewStatus = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
	//On Wayland there is no GLX display, but the OpenGL functions were loaded nevertheless
	if (glewStatus == GLEW_ERROR_NO_GLX_DISPLAY)
	{
		glewStatus = GLEW_OK;
	}
#endif
	if (glewStatus != GLEW_OK) {
		cerr << "ERROR: Could not load glew: " << glewGetErrorString(glewStatus) << endl;
		glfwTerminate();
		util::pause();
		exit(EXIT_FAILURE);
	}
	if (glewIsSupported("GL_VERSION_4_3"))
	{
		cout << "GLEW: OpenGL Version 4.3 is supported!" << endl;
	}
	else
	{
		cerr << "GLEW: OpenGL Version is below 4.3! :(" << endl;
	}

	initDebug();

	initPhysics();

	init(window);

}

void initDebug()
{
	//Following if code is from wiki
#ifndef NDEBUG
	// GLEW already loaded the debug output functions (if the driver supports them).
	// Register your callback function.
	if (glDebugMessageCallback != nullptr) {
		glDebugMessageCallback(DebugCallback, nullptr);
	}
	else if (glDebugMessageCallbackARB != nullptr) {
		glDebugMessageCallbackARB(DebugCallback, nullptr);
	}
	else if (glDebugMessageCallbackAMD != nullptr) {
		glDebugMessageCallbackAMD(DebugCallbackAMD, nullptr);
	}

	// Enable synchronous callback. This ensures that your callback function is called
	// right after an error has occurred. This capability is not defined in the AMD
	// version.
	if ((glDebugMessageCallback != nullptr) || (glDebugMessageCallbackARB != nullptr)) {
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}
#endif

}


//The following debug callbacks are from wiki: https://lva.cg.tuwien.ac.at/cgue/wiki/doku.php?id=students:debugcontext
static void GLAPIENTRY DebugCallbackAMD(GLuint id, GLenum category, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam) {
	string error = FormatDebugOutput(category, category, id, severity, message);
	cout << error << endl;
}

static void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam) {
	string error = FormatDebugOutput(source, type, id, severity, message);
	cout << error << endl;
}

static string FormatDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, const char* msg) {
	stringstream stringStream;
	string sourceString;
	string typeString;
	string severityString;

	// The AMD variant of this extension provides a less detailed classification of the error,
	// which is why some arguments might be "Unknown".
	switch (source) {
	case GL_DEBUG_CATEGORY_API_ERROR_AMD:
	case GL_DEBUG_SOURCE_API: {
		sourceString = "API";
		break;
	}
	case GL_DEBUG_CATEGORY_APPLICATION_AMD:
	case GL_DEBUG_SOURCE_APPLICATION: {
		sourceString = "Application";
		break;
	}
	case GL_DEBUG_CATEGORY_WINDOW_SYSTEM_AMD:
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: {
		sourceString = "Window System";
		break;
	}
	case GL_DEBUG_CATEGORY_SHADER_COMPILER_AMD:
	case GL_DEBUG_SOURCE_SHADER_COMPILER: {
		sourceString = "Shader Compiler";
		break;
	}
	case GL_DEBUG_SOURCE_THIRD_PARTY: {
		sourceString = "Third Party";
		break;
	}
	case GL_DEBUG_CATEGORY_OTHER_AMD:
	case GL_DEBUG_SOURCE_OTHER: {
		sourceString = "Other";
		break;
	}
	default: {
		sourceString = "Unknown";
		break;
	}
	}

	switch (type) {
	case GL_DEBUG_TYPE_ERROR: {
		typeString = "Error";
		break;
	}
	case GL_DEBUG_CATEGORY_DEPRECATION_AMD:
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: {
		typeString = "Deprecated Behavior";
		break;
	}
	case GL_DEBUG_CATEGORY_UNDEFINED_BEHAVIOR_AMD:
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: {
		typeString = "Undefined Behavior";
		break;
	}
	case GL_DEBUG_TYPE_PORTABILITY_ARB: {
		typeString = "Portability";
		break;
	}
	case GL_DEBUG_CATEGORY_PERFORMANCE_AMD:
	case GL_DEBUG_TYPE_PERFORMANCE: {
		typeString = "Performance";
		break;
	}
	case GL_DEBUG_CATEGORY_OTHER_AMD:
	case GL_DEBUG_TYPE_OTHER: {
		typeString = "Other";
		break;
	}
	default: {
		typeString = "Unknown";
		break;
	}
	}

	switch (severity) {
	case GL_DEBUG_SEVERITY_HIGH: {
		severityString = "High";
		break;
	}
	case GL_DEBUG_SEVERITY_MEDIUM: {
		severityString = "Medium";
		break;
	}
	case GL_DEBUG_SEVERITY_LOW: {
		severityString = "Low";
		break;
	}
	default: {
		severityString = "Unknown";
		break;
	}
	}

	stringStream << "OpenGL DEBUG: " << msg;
	stringStream << " [Source = " << sourceString;
	stringStream << ", Type = " << typeString;
	stringStream << ", Severity = " << severityString;
	stringStream << ", ID = " << id << "]";

	return stringStream.str();
}

void initShaders()
{
	cout << "Initializing shaders..." << endl;
	//Depth/Shadow Shader (Pass 1)
	cout << "Depth shader..." << endl;
	depthShader = make_unique<Shader>("Shader/depth_shader.vert", "Shader/depth_shader.frag");
	//Main Shader (Pass 2)
	cout << "Main shader..." << endl;
	mainShader = make_unique<Shader>("Shader/main_shader.vert", "Shader/main_shader.frag");
	//Text Shader
	cout << "Text shader..." << endl;
	textShader = make_unique<Shader>("Shader/text_shader.vert", "Shader/text_shader.frag");
	//Light (source) Shader
	cout << "Light source shader..." << endl;
	lightShader = make_unique<Shader>("Shader/light_source_shader.vert", "Shader/light_source_shader.frag");
	//Blur Shader
	cout << "Blur shader..." << endl;
	blurShader = make_unique<Shader>("Shader/gaussian_blur_shader.vert", "Shader/gaussian_blur_shader.frag");
	//HDR/Bloom (post processing) Shader
	cout << "Post processing shader..." << endl;
	postProcessingShader = make_unique<Shader>("Shader/post_processing_shader.vert", "Shader/post_processing_shader.frag");


	//Preparing shaders
	//Final Shader - based on http://learnopengl.com/#!Advanced-Lighting/Bloom
	postProcessingShader->enable();
	glUniform1i(postProcessingShader->getUniformLocation("sceneTex"), 0);
	glUniform1i(postProcessingShader->getUniformLocation("bloomTex"), 1);

	glGenFramebuffers(1, &hdrFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

	glGenTextures(2, colorBuffers);
	for (GLuint i = 0; i < 2; i++)
	{
		glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, windowWidth, windowHeight, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorBuffers[i], 0);
	}

	// - Create and attach depth buffer (renderbuffer)
	glGenRenderbuffers(1, &depthBloomRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBloomRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, windowWidth, windowHeight);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBloomRBO);

	// Attach draw buffers for rendering so OpenGL nows which to draw to
	GLuint attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, attachments);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		cout << "Framebuffer not complete!" << endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Ping pong framebuffer for blurring
	glGenFramebuffers(2, blurFBO);
	glGenTextures(2, blurColorbuffers);
	for (GLuint i = 0; i < 2; ++i)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[i]);
		glBindTexture(GL_TEXTURE_2D, blurColorbuffers[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, windowWidth, windowHeight, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // We clamp to the edge as the blur filter would otherwise sample repeated texture values!
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurColorbuffers[i], 0);
		// Also check if framebuffers are complete (no need for depth buffer)
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			cout << "Framebuffer not complete!" << endl;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);


	//Main Shader
	mainShader->enable();
	glUniform1i(mainShader->getUniformLocation("colorTextureSampler"), 0);
	glUniform1i(mainShader->getUniformLocation("shadowMap"), 1);
	glUniform1i(mainShader->getUniformLocation("normalMap"), 2);


	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	//Generating depth map fbo
	glGenFramebuffers(1, &shadowFBO);

	//Generating depth map
	glGenTextures(1, &shadowDepthTexture);
	glBindTexture(GL_TEXTURE_2D, shadowDepthTexture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	GLfloat borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

	//Attaching to shadowFBO (only depth)
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTexture, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("Framebuffer error, status: 0x%x\n", status);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void init(GLFWwindow* window) {
	glEnable(GL_DEPTH_TEST);
	controls = make_unique<input::Input>(window);

	initShaders();
	initTextRendering();

	cout << "Loading assets from files via assimp and setting up scene..." << endl;
	gameScene = make_unique<Scene>(window, mainShader.get(), physicsWorld.get());
	gameScene->setViewportSize(windowWidth, windowHeight);
	//The scene stands still behind the main menu. Simulating the first second (without input) lets the bear land
	//on the ground and the camera follow it, as at the start of the game.
	for (int i = 0; i < 60; ++i)
	{
		gameScene->update(1.0f / 60.0f);
	}

	applyGraphicsSettings();
	initMenu();
}

void initMenu()
{
	cout << "Initializing menus (RmlUi)..." << endl;
	menu = make_unique<ui::Menu>(window, settings, onMenuAction);
	menu->showMainMenu();
	if (!menu->isVisible())
	{
		//Without menus (e.g. missing UI folder) the game starts right away, ESC quits as in earlier versions
		gameState = PLAYING;
	}
}

//Applies the settings that are not read directly when drawing (like settings.hdr)
void applyGraphicsSettings()
{
	glfwSwapInterval(settings.vsync ? 1 : 0);
	glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
	texSettings::bilinear = settings.bilinearFiltering;
	texSettings::mipMappingQuality = settings.mipmapping;
	gameScene->setNormalMappingEnabled(settings.normalMapping);
	gameScene->setShadowMappingEnabled(settings.shadows);
	gameScene->setTransparencyEnabled(settings.transparency);
}

void onMenuAction(ui::MenuAction action)
{
	switch (action)
	{
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
		//Start in the main menu begins a new run
		gameScene->restart();
		gameState = MAIN_MENU;
		menu->showMainMenu();
		break;
	case ui::MenuAction::GraphicsChanged:
		applyGraphicsSettings();
		break;
	case ui::MenuAction::ApplyDisplay:
		//The new framebuffer size arrives in framebufferSizeCallback
		display::applyWindowMode(window, settings.windowMode, settings.width, settings.height);
		applyGraphicsSettings(); //the swap interval (VSync) may be reset by a new video mode
		[[fallthrough]];
	case ui::MenuAction::SettingsClosed:
		if (useSavedSettings && !settings.save(SETTINGS_FILE))
		{
			cerr << "WARNING: Could not save the settings to " << SETTINGS_FILE << endl;
		}
		break;
	}
}

/**
* @return false if there is no pause menu (e.g. missing UI folder), the game keeps running then
*/
bool pauseGame()
{
	menu->showPauseMenu();
	if (!menu->isVisible())
	{
		return false;
	}
	gameState = PAUSED;
	return true;
}

void resumeGame()
{
	gameState = PLAYING;
	menu->hide();
}

//Without menus the game shows GAME OVER or YOU WON and R restarts as in earlier versions
void showGameOver()
{
	menu->showGameOverMenu(gameScene->isGameWon());
	if (menu->isVisible())
	{
		gameState = GAME_OVER;
	}
}

void restartGame()
{
	gameScene->restart();
	resumeGame();
}


//Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initTextRendering()
{
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

//Text is positioned in pixels, (0, 0) is the bottom left corner of the window
void updateTextProjection()
{
	mat4 textProjectionMatrix = ortho(0.0f, static_cast<GLfloat>(windowWidth), 0.0f, static_cast<GLfloat>(windowHeight));
	textShader->enable();
	glUniformMatrix4fv(textShader->getUniformLocation("projection"), 1, GL_FALSE, value_ptr(textProjectionMatrix));
}

//The render targets of the HDR and bloom passes have the size of the window, so they are reallocated when it changes
void resizeScreenBuffers()
{
	for (GLuint i = 0; i < 2; i++)
	{
		glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, windowWidth, windowHeight, 0, GL_RGB, GL_FLOAT, nullptr);
		glBindTexture(GL_TEXTURE_2D, blurColorbuffers[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, windowWidth, windowHeight, 0, GL_RGB, GL_FLOAT, nullptr);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBloomRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, windowWidth, windowHeight);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	updateTextProjection();
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return; //minimized
	}
	windowWidth = width;
	windowHeight = height;
	if (gameScene)
	{
		resizeScreenBuffers();
		gameScene->setViewportSize(width, height);
	}
	if (menu)
	{
		menu->onFramebufferResized(width, height);
	}
	if (settings.windowMode == WINDOWED && !glfwGetWindowMonitor(window))
	{
		//Keeps the size of a window resized by the user (e.g. by dragging its border) for the settings
		glfwGetWindowSize(window, &settings.width, &settings.height);
	}
}

void windowFocusCallback(GLFWwindow* window, int focused)
{
	//e.g. when switching to another window with Alt+Tab (without menus the game keeps running, a demo does not need the keyboard)
	if (!focused && gameState == PLAYING && !demoScript)
	{
		pauseGame();
	}
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (!menu)
	{
		return; //still loading
	}
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		switch (gameState)
		{
		case PLAYING:
			if (!pauseGame())
			{
				glfwSetWindowShouldClose(window, true); //without menus ESC quits as in earlier versions
			}
			break;
		case PAUSED:
			if (!menu->back())
			{
				resumeGame();
			}
			break;
		case MAIN_MENU:
		case GAME_OVER:
			menu->back();
			break;
		}
		return;
	}
	if (gameState == GAME_OVER && key == GLFW_KEY_R && action == GLFW_PRESS)
	{
		restartGame(); //like during the game
		return;
	}

	if (gameState == PLAYING)
	{
		fKeyCallback(window, key, scancode, action, mods);
	}
	else
	{
		menu->onKey(key, action, mods);
	}
}

void gameLoop()
{
	auto time = glfwGetTime();
	int frame = 0;
	while (!glfwWindowShouldClose(window)) {
		auto time_new = glfwGetTime();
		if (fixedTimeStep > 0)
		{
			//Every frame advances the game by the same time: a recording as fast as the frames can be drawn, a demo in real time
			if (!recorder)
			{
				this_thread::sleep_for(chrono::duration<double>(time + fixedTimeStep - time_new));
				time_new = glfwGetTime();
			}
			time_delta = fixedTimeStep;
		}
		else
		{
			time_delta = static_cast<float>(time_new - time);
		}
		time = time_new;

		glfwPollEvents();

		//Nothing to draw while the window is minimized
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
		{
			glfwWaitEvents();
			continue;
		}

		if (demoScript)
		{
			playDemoFrame(frame);
		}
		++frame;

		controls->update();
		handleGamepad();
		//Handles the buttons pressed in the menu (e.g. Start changes the game state)
		menu->update();
		//The mouse turns the camera during the game and is a normal cursor in the menus
		controls->setMouseCaptured(gameState == PLAYING && glfwGetWindowAttrib(window, GLFW_FOCUSED));

		messageTimeLeft = std::max(0.0f, messageTimeLeft - time_delta);

		//Do game logic here, the game stands still while a menu is open
		if (gameState == PLAYING)
		{
			mainShader->enable();
			update(std::min(time_delta, MAX_TIME_STEP));
			if (gameScene->isGameOver() || gameScene->isGameWon())
			{
				showGameOver();
			}
		}
		//Do drawing of game here
		draw();

		if (menu->isVisible())
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); //also in wireframe mode
			menu->render();
			glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);
		}

		if (recorder && !recordingFailed && !recorder->recordFrame(windowWidth, windowHeight))
		{
			cerr << "ERROR: Could not record the frame (the disk is full or the window size changed)" << endl;
			recordingFailed = true;
			glfwSetWindowShouldClose(window, true);
		}

		glfwSwapBuffers(window);


	}
}

//The keys of the demo script are pressed as on the keyboard, for the menus and the game
void playDemoFrame(int frame)
{
	for (const DemoScript::Event& event : demoScript->eventsAt(frame))
	{
		switch (event.type)
		{
		case DemoScript::Event::KeyDown:
		case DemoScript::Event::KeyUp:
		{
			bool down = event.type == DemoScript::Event::KeyDown;
			input::setScriptedKey(event.key, down);
			keyCallback(window, event.key, glfwGetKeyScancode(event.key), down ? GLFW_PRESS : GLFW_RELEASE, 0);
			break;
		}
		case DemoScript::Event::Quit:
			glfwSetWindowShouldClose(window, true);
			break;
		}
	}
}

void update(float time_delta) {
	//WASD moves the free camera (L) instead of the bear
	gameScene->update(time_delta, controls->playerInput(time_delta, settings, !gameScene->isCameraLocked()));
}

/**
* Start (Options on a DualSense) pauses and resumes the game, the D-pad (or left stick), A (Cross) and B (Circle) work the menus
* like the arrow keys, ENTER and ESC
*/
void handleGamepad()
{
	if (!controls->hasGamepad())
	{
		return;
	}
	if (gameState == PLAYING)
	{
		if (controls->wasPressed(GLFW_GAMEPAD_BUTTON_START))
		{
			pauseGame();
		}
		return;
	}
	if (gameState == PAUSED && controls->wasPressed(GLFW_GAMEPAD_BUTTON_START))
	{
		resumeGame();
		return;
	}
	for (int key : controls->getMenuKeys())
	{
		keyCallback(window, key, 0, GLFW_PRESS, 0);
		keyCallback(window, key, 0, GLFW_RELEASE, 0);
	}
}

void joystickCallback(int joystick, int event)
{
	if (event == GLFW_CONNECTED)
	{
		const char* name = glfwGetJoystickName(joystick);
		lastMessage = glfwJoystickIsGamepad(joystick)
			? string("Gamepad connected: ") + (name ? name : "")
			: string("Controller without a gamepad mapping: ") + (name ? name : "") + " (add it to " + input::Input::MAPPINGS_FILE + ")";
		messageTimeLeft = 4;
	}
	else if (event == GLFW_DISCONNECTED)
	{
		lastMessage = "Gamepad disconnected";
		messageTimeLeft = 4;
		if (gameState == PLAYING && menu)
		{
			pauseGame(); //e.g. the battery is empty
		}
	}
}

void draw() {
	////Shadow Mapping Pass 1 (Preparation)///
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	depthShader->enable();
	//Drawing entire scene in depthShader
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);

	glClear(GL_DEPTH_BUFFER_BIT);
	gameScene->renderFromLightSource(depthShader.get());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	//The bloom of bright fragments is only added to the scene by the HDR tone mapping (and not in wireframe mode)
	bool bloom = settings.hdr && settings.bloom && !settings.wireframe;

	////Pass 2 - Main Scene///
	if (!settings.wireframe)
	{
		// 1. Render scene into floating point framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	}
	glViewport(0, 0, windowWidth, windowHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	//Rendering light sources (opaque, before the scene with its transparent water)
	lightShader->enable();
	gameScene->renderLightSourcesOnly(lightShader.get());

	//drawing entire scene in main shader
	mainShader->enable();
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, shadowDepthTexture);
	gameScene->renderFromCamera(mainShader.get());
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	if (!bloom)
	{
		textRendering();
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!settings.wireframe)
	{
		bool bloomBlurHorizontal = true;
		if (bloom)
		{
			// Horizontal/Vertical gaussian blur
			bool firstPass = true;
			int gaussPasses = 10; //2 passes: horizontal and vertical (5 horizontal iterations, 5 vertical)
			blurShader->enable();
			for (int i = 0; i < gaussPasses; ++i)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[bloomBlurHorizontal]);
				glUniform1i(blurShader->getUniformLocation("horizontal"), bloomBlurHorizontal);
				glBindTexture(GL_TEXTURE_2D, firstPass ? colorBuffers[1] : blurColorbuffers[!bloomBlurHorizontal]);  // Bind result of previous framebuffer as texture
				renderQuad();
				bloomBlurHorizontal = !bloomBlurHorizontal;
				if (firstPass)
				{
					firstPass = false;
				}
			}
			//The text is added to the scene with the bloom (and therefore not blurred)
			textRendering();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		// Rendering colorBuffer to simple quad to get drawing across entire screen and mapping HDR tones
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		postProcessingShader->enable();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, colorBuffers[0]);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, blurColorbuffers[!bloomBlurHorizontal]);
		glUniform1i(postProcessingShader->getUniformLocation("hdrEnabled"), settings.hdr);
		glUniform1i(postProcessingShader->getUniformLocation("bloomEnabled"), bloom);
		renderQuad();
	}


	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

// Based on: http://learnopengl.com/#!Advanced-Lighting/Shadows/Shadow-Mapping
void renderQuad()
{
	if (quadVAO == 0)
	{
		GLfloat quadVertices[] = {
			// Positions        // Texture Coords
			-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
			1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// Setup plane VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void cleanup() {
	cout << "Cleaning up memory..." << endl;
	//Releases the OpenGL resources of the menus, so it must happen before the window (and its context) is destroyed
	menu.reset(nullptr);
	gameScene.reset(nullptr);
	//The scene (and the character controller of the bear) must be released before the physics world
	physicsWorld.reset(nullptr);
}


void initPhysics()
{
	cout << "Initializing Jolt Physics..." << endl;
	physicsWorld = make_unique<physics::PhysicsWorld>();
}

// Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initFreeType()
{
	// FreeType
	FT_Library ft;
	// All functions return a value different than 0 whenever an error occurred
	if (FT_Init_FreeType(&ft))
	{
		cout << "ERROR::FREETYPE: Could not init FreeType Library" << endl;
		return;
	}

	// Load font as face (without a font the game still runs, just without HUD text)
	FT_Face face;
	if (FT_New_Face(ft, "Font/DejaVuSans.ttf", 0, &face))
	{
		cout << "ERROR::FREETYPE: Failed to load font" << endl;
		FT_Done_FreeType(ft);
		return;
	}

	// Set size to load glyphs as
	FT_Set_Pixel_Sizes(face, 0, 48);

	// Disable byte-alignment restriction
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// Load first 128 characters of ASCII set
	for (GLubyte c = 0; c < 128; c++)
	{
		// Load character glyph 
		if (FT_Load_Char(face, c, FT_LOAD_RENDER))
		{
			cout << "ERROR::FREETYTPE: Failed to load Glyph" << endl;
			continue;
		}
		// Generate texture
		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			face->glyph->bitmap.width,
			face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			face->glyph->bitmap.buffer
			);
		// Set texture options
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// Now store character for later use
		Character character = {
			texture,
			ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
			ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
			static_cast<GLuint>(face->glyph->advance.x)
		};
		Characters.insert(pair<GLchar, Character>(c, character));
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	// Destroy FreeType once we're finished
	FT_Done_Face(face);
	FT_Done_FreeType(ft);
}

//Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void renderText(Shader* shader, string text, GLfloat x, GLfloat y, GLfloat scale, vec3 color)
{
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST); //The text is always in front of the scene

	// Activate corresponding render state
	shader->enable();
	glUniform3f(shader->getUniformLocation("textColor"), color.x, color.y, color.z);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(textVAO);

	// Iterate through all characters
	string::const_iterator c;
	for (c = text.begin(); c != text.end(); ++c)
	{
		Character ch = Characters[*c];

		GLfloat xpos = x + ch.bearing.x * scale;
		GLfloat ypos = y - (ch.size.y - ch.bearing.y) * scale;

		GLfloat w = ch.size.x * scale;
		GLfloat h = ch.size.y * scale;
		// Update VBO for each character
		GLfloat vertices[6][4] = {
			{ xpos, ypos + h, 0.0, 0.0 },
			{ xpos, ypos, 0.0, 1.0 },
			{ xpos + w, ypos, 1.0, 1.0 },

			{ xpos, ypos + h, 0.0, 0.0 },
			{ xpos + w, ypos, 1.0, 1.0 },
			{ xpos + w, ypos + h, 1.0, 0.0 }
		};
		// Render glyph texture over quad
		glBindTexture(GL_TEXTURE_2D, ch.textureID);
		// Update content of VBO memory
		glBindBuffer(GL_ARRAY_BUFFER, textVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); // Be sure to use glBufferSubData and not glBufferData

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		// Render quad
		glDrawArrays(GL_TRIANGLES, 0, 6);
		// Now advance cursors for next glyph (note that advance is number of 1/64 pixels)
		x += (ch.advance >> 6) * scale; // Bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void textRendering()
{
	if (gameState == MAIN_MENU)
	{
		return; //the main menu covers the HUD
	}
	if (settings.showHelp)
	{
		renderText(textShader.get(), "WASD or arrow keys / left stick to run", windowWidth / 10.f, windowHeight - 2.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Mouse / right stick to look around, wheel to zoom", windowWidth / 10.f, windowHeight - 3.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "SPACE / Cross or A to jump (hold to jump higher)", windowWidth / 10.f, windowHeight - 4.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Press R key to restart", windowWidth / 10.f, windowHeight - 5.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Pass all checkpoints and make it around the track!", windowWidth / 10.f, windowHeight - 6.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Watch out: the rocks knock the bear over", windowWidth / 10.f, windowHeight - 7.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
	}
	if (settings.showFrameTime)
	{
		renderText(textShader.get(), "FPS: " + to_string(1.f / time_delta).substr(0, 4), windowWidth / 10.f, windowHeight - 1.f * windowHeight / 10.f, 0.6f, vec3(0.9, 0.5f, 0.5f));
		renderText(textShader.get(), "Frametime: " + to_string(time_delta).substr(0, 5), windowWidth / 3.f, windowHeight - 1.f * windowHeight / 10.f, 0.6f, vec3(0.9, 0.5f, 0.5f));
		//Fewer objects are drawn with view frustum culling (F8)
		string drawnObjects = to_string(gameScene->getDrawnGeometryCount()) + "/" + to_string(gameScene->getGeometryCount());
		renderText(textShader.get(), "Objects: " + drawnObjects, windowWidth * 0.62f, windowHeight - 1.f * windowHeight / 10.f, 0.6f, vec3(0.9, 0.5f, 0.5f));
	}

	//Lap progress (the finish line only counts after all checkpoints)
	int checkpointsPassed = gameScene->getCheckpointsPassed();
	int checkpointCount = gameScene->getCheckpointCount();
	string lapProgress = "Checkpoints: " + to_string(checkpointsPassed) + "/" + to_string(checkpointCount);
	if (checkpointsPassed == checkpointCount && !gameScene->isGameWon())
	{
		lapProgress += " - cross the finish line!";
	}
	renderText(textShader.get(), lapProgress, windowWidth / 10.f, 2.f * windowHeight / 10.f, 0.5f, vec3(0.9f, 0.8f, 0.3f));

	if (messageTimeLeft > 0)
	{
		renderText(textShader.get(), lastMessage, windowWidth / 10.f, windowHeight / 10.f, 0.4f, vec3(0.7, 0.5f, 0.8f));
	}

	//The game over menu shows the result if there are menus
	if (gameState == GAME_OVER)
	{
		return;
	}
	if (gameScene->isGameWon())
	{
		renderText(textShader.get(), "YOU WON!", windowWidth / 2.f - windowWidth / 5.f, windowHeight / 2.f, 0.6f, vec3(1.0, 0.1f, 0.1f));
	}
	else if (gameScene->isGameOver())
	{
		renderText(textShader.get(), "GAME OVER!", windowWidth / 2.f - windowWidth / 5.f, windowHeight / 2.f, 0.6f, vec3(1.0, 0.1f, 0.1f));
	}
}

void fKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	//The F-keys change the same settings as the settings menu
	if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
	{
		settings.showHelp = !settings.showHelp;
	}

	if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
	{
		settings.showFrameTime = !settings.showFrameTime;
		string frameTimeMsg = settings.showFrameTime ? "enabled" : "disabled";
		lastMessage = "Frame/time display " + frameTimeMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
	{
		settings.wireframe = !settings.wireframe;
		applyGraphicsSettings();
		string wireFrameModeMsg = settings.wireframe ? "enabled" : "disabled";
		lastMessage = "Wireframe mode " + wireFrameModeMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
	{
		settings.bilinearFiltering = !settings.bilinearFiltering;
		applyGraphicsSettings();
		string bilinearMsg = settings.bilinearFiltering ? "bilinear" : "nearest neighbor";
		lastMessage = "Texture sampling: " + bilinearMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F5 && action == GLFW_PRESS)
	{
		settings.mipmapping = (settings.mipmapping + 1) % 3;
		applyGraphicsSettings();

		string mipmapMsg;
		switch (settings.mipmapping)
		{
		case 0: mipmapMsg = "OFF";
			break;
		case 1: mipmapMsg = "Nearest Neighbor";
			break;
		case 2: mipmapMsg = "Bilinear";
			break;
		}
		lastMessage = "Texture sampling: " + mipmapMsg;
		messageTimeLeft = 3;

	}


	if (key == GLFW_KEY_F6 && action == GLFW_PRESS)
	{
		settings.normalMapping = !settings.normalMapping;
		applyGraphicsSettings();
		lastMessage = "Normal Mapping enabled: " + to_string(settings.normalMapping);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F7 && action == GLFW_PRESS)
	{
		settings.shadows = !settings.shadows;
		applyGraphicsSettings();
		lastMessage = "Shadow Mapping (PCF) enabled: " + to_string(settings.shadows);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F8 && action == GLFW_PRESS)
	{
		bool viewFrustumCullingEnabled = !gameScene->isViewFrustumCullingEnabled();
		gameScene->setViewFrustumCullingEnabled(viewFrustumCullingEnabled);
		lastMessage = "View Frustum Culling enabled: " + to_string(viewFrustumCullingEnabled);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F9 && action == GLFW_PRESS)
	{
		settings.transparency = !settings.transparency;
		applyGraphicsSettings();
		lastMessage = "Transparency enabled: " + to_string(settings.transparency);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F10 && action == GLFW_PRESS)
	{
		settings.hdr = !settings.hdr;
		lastMessage = "HDR enabled: " + to_string(settings.hdr);
		messageTimeLeft = 3;
	}

	if (settings.hdr && key == GLFW_KEY_F11 && action == GLFW_PRESS)
	{
		settings.bloom = !settings.bloom;
		lastMessage = "Bloom (requries HDR) enabled: " + to_string(settings.bloom);
		messageTimeLeft = 3;
	}
}
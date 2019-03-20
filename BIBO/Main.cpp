#pragma comment(lib, "glew32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glfw3.lib")
#pragma comment(lib, "DevIL.lib")
#pragma comment(lib, "ILU.lib")
#pragma comment(lib, "ILUT.lib")

#include <iostream>
#include <memory>
#include <sstream>
#ifndef GLEW_INCLUDED
#define GLEW_INCLUDED
#include <GL\glew.h>
#include <gl\wglew.h>
#include <GL\GL.h>
#endif
#ifndef GLFW_INCLUDED
#define GLFW_INCLUDED
#include <GLFW/glfw3.h>
#endif

#include "Scene/Scene.hpp"
#include <map>
#include "Util/PvdCallback.hpp"

using namespace bibo;


#ifndef PX_INCLUDED
#define PX_INCLUDED
#include "PhysX/PxPhysicsAPI.h"
#include "PhysX/extensions/PxExtensionsAPI.h"
#endif

#include <ft2build.h>
#include FT_FREETYPE_H

using namespace scene;
using namespace physx;

static void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam);
static void APIENTRY DebugCallbackAMD(GLuint id, GLenum category, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam);
static string FormatDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, const char* msg);

static PxDefaultErrorCallback gDefaultErrorCallback;
static PxDefaultAllocator gDefaultAllocatorCallback;
PxFoundation *mFoundation;
PxPhysics *mPhysics;
PxCooking* mCooking;


void initialize();
void init(GLFWwindow* window);
void initPX();
void initTextRendering();
void initFreeType();
void textRendering();
void initDebug();
void cleanup();
void blurShadowMap();
void draw();
void update(float time_delta);
void gameLoop();
void renderQuad();
void renderText(Shader* shader, string text, GLfloat x, GLfloat y, GLfloat scale, vec3 color);
void togglePvdConnection();
void createPvdConnection();
void fKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);

//vars
unsigned int windowWidth = 800;
unsigned int windowHeight = 600;
bool fullscreen = false;
GLFWwindow* window;
float time_delta; //Last frame time

unique_ptr<Shader> mainShader;
unique_ptr<Shader> depthShader;
unique_ptr<Shader> debugShader;
unique_ptr<Shader> textShader;
unique_ptr<Shader> lightShader;
unique_ptr<Shader> blurShader;
unique_ptr<Shader> blurShader2;
unique_ptr<Shader> postProcessingShader;

unique_ptr<Scene> gameScene;

GLuint shadowFBO; //Frame Buffer Object (FBO) for depth (from Light POV!)
GLuint shadowDepthTexture;
GLuint hdrFBO;
GLuint colorBuffers[2];
GLuint depthBloomRBO; //Render Buffer Object (RBO) for depth (from Camera POV!)
GLuint blurFBO[3];
GLuint blurColorbuffers[3];
GLuint shadowColorTexture; //used for variance shadow mapping, is usually blurred
const GLuint SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;

extern "C" {
	_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
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
//HUD options
bool displayHelp = false;
bool displayFrameTime = true;
bool wireFrameMode = false;
bool viewFrustumCulling = true;
bool hdrEnabled = true;
bool bloomEnabled = true;
//float hdrExposure = 1.0f;
bool varianceShadowMappingEnabled = false;
string lastMessage = "";
float messageTimeLeft = 0;

struct PvdParameters
{
	char							ip[256];
	PxU32							port;
	PxU32							timeout;
	bool							useFullPvdConnection;

	PvdParameters()
		: port(5425)
		, timeout(10)
		, useFullPvdConnection(true)
	{
		PxStrcpy(ip, 256, "127.0.0.1");
	}
};

int main(int argc, char** argv)
{

	if (argc >= 3) {
		cout << "You are running" << endl;

		if ((stringstream(argv[1]) >> windowWidth).fail())
		{
			cerr << "ERROR: Could not parse first element, try again!" << endl;
			system("PAUSE");
			exit(EXIT_FAILURE);
		}

		if ((stringstream(argv[2]) >> windowHeight).fail()) {
			cerr << "ERROR: Could not parse second element, try again!" << endl;
			system("PAUSE");
			exit(EXIT_FAILURE);
		}

		if (argc >= 4) {
			if ((stringstream(argv[3]) >> fullscreen).fail()) {

				cerr << "ERROR: Could not parse third command-line-arg, try again!" << endl;
				system("PAUSE");
				exit(EXIT_FAILURE);
			}
		}
	}

	cout << "Starting BIBO game" << endl;

	initialize();

	gameLoop();

	cleanup();

	glfwDestroyWindow(window);

	glfwTerminate();

	return EXIT_SUCCESS;
}

void initialize()
{
	if (!glfwInit()) {
		cerr << "ERROR: Could not init glfw." << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);

	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWmonitor* monitor = nullptr;

	if (fullscreen) {
		monitor = glfwGetPrimaryMonitor();
		//Set monitor refresh rate
		auto refresh_rate = 60;
		glfwWindowHint(GLFW_REFRESH_RATE, refresh_rate);
	}

	window = glfwCreateWindow(windowWidth, windowHeight, "Hello CGUE", monitor, nullptr);
	glfwSetKeyCallback(window, fKeyCallback);

	if (!window) // window == nullptr
	{
		cerr << "ERROR;" << endl;
		glfwTerminate();
		system("PAUSE");
		exit(EXIT_FAILURE);
	}
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwMakeContextCurrent(window);

	//GLEW initialization
	glewExperimental = true; //Important for loading in old OSes
	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
	{
		cerr << "OpenGL Error: " << error << endl;
	}
	if (glewInit() != GLEW_OK) {
		cerr << "ERROR: Could not load glew" << endl;
		glfwTerminate();
		system("PAUSE");
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

	initPX();

	init(window);

}

void initDebug()
{
	//Following if code is from wiki 
#if _DEBUG
	// Query the OpenGL function to register your callback function.
	PFNGLDEBUGMESSAGECALLBACKPROC _glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)wglGetProcAddress("glDebugMessageCallback");
	PFNGLDEBUGMESSAGECALLBACKARBPROC _glDebugMessageCallbackARB = (PFNGLDEBUGMESSAGECALLBACKARBPROC)wglGetProcAddress("glDebugMessageCallbackARB");
	PFNGLDEBUGMESSAGECALLBACKAMDPROC _glDebugMessageCallbackAMD = (PFNGLDEBUGMESSAGECALLBACKAMDPROC)wglGetProcAddress("glDebugMessageCallbackAMD");

	// Register your callback function.
	if (_glDebugMessageCallback != nullptr) {
		_glDebugMessageCallback(DebugCallback, nullptr);
	}
	else if (_glDebugMessageCallbackARB != nullptr) {
		_glDebugMessageCallbackARB(DebugCallback, nullptr);
	}
	else if (_glDebugMessageCallbackAMD != nullptr) {
		_glDebugMessageCallbackAMD(DebugCallbackAMD, nullptr);
	}

	// Enable synchronous callback. This ensures that your callback function is called
	// right after an error has occurred. This capability is not defined in the AMD
	// version.
	if ((_glDebugMessageCallback != nullptr) || (_glDebugMessageCallbackARB != nullptr)) {
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}
#endif

}


//The following debug callbacks are from wiki: https://lva.cg.tuwien.ac.at/cgue/wiki/doku.php?id=students:debugcontext
static void APIENTRY DebugCallbackAMD(GLuint id, GLenum category, GLenum severity, GLsizei length, const GLchar* message, GLvoid* userParam) {
	string error = FormatDebugOutput(category, category, id, severity, message);
	cout << error << endl;
}

static void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam) {
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
	//Debug Shader
	cout << "Debug shader..." << endl;
	debugShader = make_unique<Shader>("Shader/debug_depth_shader.vert", "Shader/debug_depth_shader.frag");
	//Text Shader
	cout << "Text shader..." << endl;
	textShader = make_unique<Shader>("Shader/text_shader.vert", "Shader/text_shader.frag");
	//Light (source) Shader
	cout << "Light source shader..." << endl;
	lightShader = make_unique<Shader>("Shader/light_source_shader.vert", "Shader/light_source_shader.frag");
	//Blur Shader
	cout << "Blur shader..." << endl;
	blurShader = make_unique<Shader>("Shader/gaussian_blur_shader.vert", "Shader/gaussian_blur_shader.frag");
	//Alternative blur shader
	cout << "Blur shader 2..." << endl;
	blurShader2 = make_unique<Shader>("Shader/alternative_blur_shader.vert", "Shader/alternative_blur_shader.frag");
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
	glGenFramebuffers(3, blurFBO);
	glGenTextures(3, blurColorbuffers);
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

	//Initializing blurred depth map
	glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[2]);
	glBindTexture(GL_TEXTURE_2D, blurColorbuffers[2]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_RG, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurColorbuffers[2], 0);
	// Also check if framebuffers are complete (no need for depth buffer)
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		cout << "Framebuffer not complete!" << endl;


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

	//Generating shadow map (color) texture
	glGenTextures(1, &shadowColorTexture);
	glBindTexture(GL_TEXTURE_2D, shadowColorTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_RG, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//Attaching to shadowFBO
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTexture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shadowColorTexture, 0);
	glDrawBuffer(GL_NONE);
	//glReadBuffer(GL_NONE);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		printf("Framebuffer error, status: 0x%x\n", status);
	}
}

void init(GLFWwindow* window) {
	glEnable(GL_DEPTH_TEST);
	glfwSetWindowTitle(window, "BIBO Project");

	initShaders();
	initTextRendering();

	cout << "Loading assets from files via assimp and setting up scene..." << endl;
	gameScene = make_unique<Scene>(window, mainShader.get(), mPhysics, mFoundation, mCooking);

}


//Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initTextRendering()
{
	cout << "Initializing FreeType..." << endl;
	// Set OpenGL options
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	mat4 textProjectionMatrix = ortho(0.0f, static_cast<GLfloat>(windowWidth), 0.0f, static_cast<GLfloat>(windowHeight));
	textShader->enable();
	glUniformMatrix4fv(textShader->getUniformLocation("projection"), 1, GL_FALSE, value_ptr(textProjectionMatrix));

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


void gameLoop()
{
	auto time = glfwGetTime();
	while (!glfwWindowShouldClose(window)) {
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		auto time_new = glfwGetTime();
		time_delta = static_cast<float>(time_new - time);
		time = time_new;

		//cout << "frametime: " << time_delta * 1000 << "ms"
		//	<< "   = " << 1.0 / time_delta << " fps" << endl;

		glfwPollEvents();

		if (glfwGetKey(window, GLFW_KEY_ESCAPE))
			glfwSetWindowShouldClose(window, true);


		//Do game logic here
		mainShader->enable();
		update(time_delta);
		//Do drawing of game here
		draw();

		glfwSwapBuffers(window);


	}
}

void update(float time_delta) {
	gameScene->update(time_delta);
}

void draw() {
	////Shadow Mapping Pass 1 (Preparation)///
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	depthShader->enable();
	//Drawing entire scene in depthShader
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);

	//glEnable(GL_POLYGON_OFFSET_FILL);
	//glPolygonOffset(3.f, 0.f);

	glClear(GL_DEPTH_BUFFER_BIT);
	gameScene->renderFromLightSource(depthShader.get());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (varianceShadowMappingEnabled)
	{
		blurShadowMap();
	}

	////Pass 2 - Main Scene///
	if (!wireFrameMode)
	{
		// 1. Render scene into floating point framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
	}
	//glClearColor(0.35f, 0.36f, 0.37f, 0.5f);
	glViewport(0, 0, windowWidth, windowHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	mainShader->enable();

	//drawing entire scene in main shader
	glUniform1i(mainShader->getUniformLocation("varianceShadowMappingEnabled"), varianceShadowMappingEnabled);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, varianceShadowMappingEnabled ? shadowColorTexture : shadowDepthTexture);
	gameScene->renderFromCamera(mainShader.get());
	glBindTexture(GL_TEXTURE_2D, 0);
	//Rendering light sources
	lightShader->enable();
	gameScene->renderLightSourcesOnly(lightShader.get());
	if (!hdrEnabled || !bloomEnabled || wireFrameMode)
	{
		textRendering();
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!wireFrameMode)
	{
		// Horizontal/Vertical gaussian blur  
		bool bloomBlurHorizontal = true, firstPass = true;
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
		if (hdrEnabled)
		{
			textRendering();
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Rendering colorBuffer to simple quad to get drawing across entire screen and mapping HDR tones
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		postProcessingShader->enable();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, colorBuffers[0]);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, blurColorbuffers[!bloomBlurHorizontal]);
		glUniform1i(postProcessingShader->getUniformLocation("hdrEnabled"), hdrEnabled);
		glUniform1i(postProcessingShader->getUniformLocation("bloomEnabled"), bloomEnabled);
		//glUniform1f(postProcessingShader->getUniformLocation("hdrExposure"), hdrExposure);
		renderQuad();
	}


	//textRendering();


	////Debug Shadow Mapping///
	//debugShader->enable();
	//glUniform1f(debugShader->getUniformLocation("near_plane"), 1.0f);
	//glUniform1f(debugShader->getUniformLocation("far_plane"), 75.0f);
	//glActiveTexture(GL_TEXTURE0); //since it only has one sampler
	//glBindTexture(GL_TEXTURE_2D, shadowDepthTexture);
	//renderQuad();

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
	gameScene.reset(nullptr);
	mPhysics->release();
	mFoundation->release();
	mCooking->release();
}


void initPX()
{
	cout << "Initializing Nvidia PhysX..." << endl;
	mFoundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		gDefaultAllocatorCallback,
		gDefaultErrorCallback);


	PxTolerancesScale scale;

	mPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation, scale);

	PxCookingParams params(scale);
	params.meshWeldTolerance = 0.001f;
	params.meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES | PxMeshPreprocessingFlag::eREMOVE_UNREFERENCED_VERTICES | PxMeshPreprocessingFlag::eREMOVE_DUPLICATED_TRIANGLES);
	mCooking = PxCreateCooking(PX_PHYSICS_VERSION, *mFoundation, params);
	if (!mCooking)
	{
		cerr << "Error: PxCreateCooking failed!" << endl;
	}

	togglePvdConnection();

	if (mPhysics->getPvdConnectionManager()) {
		PvdCallback* myPvdCallback = new PvdCallback(mPhysics);
		mPhysics->getPvdConnectionManager()->addHandler(*myPvdCallback);
	}

	//mPhysics->registerDeletionListener(*this, PxDeletionEventFlag::eUSER_RELEASE);
}

// Based on: http://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void initFreeType()
{
	// FreeType
	FT_Library ft;
	// All functions return a value different than 0 whenever an error occurred
	if (FT_Init_FreeType(&ft))
		cout << "ERROR::FREETYPE: Could not init FreeType Library" << endl;

	// Load font as face
	FT_Face face;
	if (FT_New_Face(ft, "Font/arial.ttf", 0, &face))
		cout << "ERROR::FREETYPE: Failed to load font" << endl;

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
			face->glyph->advance.x
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

	// Activate corresponding render state	
	shader->enable();
	glBindRenderbuffer(GL_RENDERBUFFER, depthBloomRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, windowWidth, windowHeight);
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
}

void togglePvdConnection()
{
	if (!mPhysics->getPvdConnectionManager()) return;
	if (mPhysics->getPvdConnectionManager()->isConnected())
		mPhysics->getPvdConnectionManager()->disconnect();
	else
		createPvdConnection();
}

void createPvdConnection()
{
	PxVisualDebuggerConnectionManager* pvd = mPhysics->getPvdConnectionManager();
	if (!pvd)
		return;

	//The connection flags state overall what data is to be sent to PVD.  Currently
	//the Debug connection flag requires support from the implementation (don't send
	//the data when debug isn't set) but the other two flags, profile and memory
	//are taken care of by the PVD SDK.

	//Use these flags for a clean profile trace with minimal overhead
	PxVisualDebuggerConnectionFlags theConnectionFlags(PxVisualDebuggerConnectionFlag::eDEBUG | PxVisualDebuggerConnectionFlag::ePROFILE | PxVisualDebuggerConnectionFlag::eMEMORY);
	PvdParameters mPvdParams;

	if (!mPvdParams.useFullPvdConnection)
	{
		theConnectionFlags = PxVisualDebuggerConnectionFlag::ePROFILE;
	}

	//Create a pvd connection that writes data straight to the filesystem.  This is
	//the fastest connection on windows for various reasons.  First, the transport is quite fast as
	//pvd writes data in blocks and filesystems work well with that abstraction.
	//Second, you don't have the PVD application parsing data and using CPU and memory bandwidth
	//while your application is running.
	//PxVisualDebuggerExt::createConnection(mPhysics->getPvdConnectionManager(), "c:\\temp.pxd2", theConnectionFlags);

	//The normal way to connect to pvd.  PVD needs to be running at the time this function is called.
	//We don't worry about the return value because we are already registered as a listener for connections
	//and thus our onPvdConnected call will take care of setting up our basic connection state.

	PxVisualDebuggerExt::createConnection(pvd, mPvdParams.ip, mPvdParams.port, mPvdParams.timeout, theConnectionFlags);
}

void textRendering()
{
	if (displayHelp)
	{
		renderText(textShader.get(), "Press UP or DOWN (arrow) keys to walk", windowWidth / 10.f, windowHeight - 2.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Press LEFT or RIGHT (arrow) keys to turn left or right", windowWidth / 10.f, windowHeight - 3.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Press SPACE key to jump", windowWidth / 10.f, windowHeight - 4.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
		renderText(textShader.get(), "Survive the dangerous trial and make it in time!", windowWidth / 10.f, windowHeight - 5.f * windowHeight / 10.f, 0.6f, vec3(0.5, 0.8f, 0.2f));
	}
	if (displayFrameTime)
	{
		renderText(textShader.get(), "FPS: " + to_string(1.f / time_delta).substr(0, 4), windowWidth / 10.f, windowHeight - 1.f * windowHeight / 10.f, 0.6f, vec3(0.9, 0.5f, 0.5f));
		renderText(textShader.get(), "Frametime: " + to_string(time_delta).substr(0, 5), windowWidth / 3.f, windowHeight - 1.f * windowHeight / 10.f, 0.6f, vec3(0.9, 0.5f, 0.5f));
	}

	if (messageTimeLeft > 0)
	{
		messageTimeLeft -= time_delta;
		renderText(textShader.get(), lastMessage, windowWidth / 10.f, windowHeight / 10.f, 0.4f, vec3(0.7, 0.5f, 0.8f));
	}
	else
	{
		messageTimeLeft = 0;
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

//Based on https://github.com/cforfang/opengl-shadowmapping
void blurShadowMap()
{
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glDisable(GL_DEPTH_TEST);
	blurShader2->enable();

	// Blur shadowDepthTexture (horizontally) to blurColorbuffer[2]
	glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[2]);
	glBindTexture(GL_TEXTURE_2D, shadowColorTexture); //Input-texture
	vec2 shadowRatioX = vec2(1.0f / SHADOW_WIDTH * 2.0f, 0);
	glUniform2fv(blurShader2->getUniformLocation("ScaleU"), 1, value_ptr(shadowRatioX));
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderQuad();

	// Blur blurColorbuffer[2] vertically and write to shadowDepthTexture
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glBindTexture(GL_TEXTURE_2D, blurColorbuffers[2]);
	vec2 shadowRatioY = vec2(0, 1.0f / SHADOW_WIDTH * 2.0f);
	glUniform2fv(blurShader2->getUniformLocation("ScaleU"), 1, value_ptr(shadowRatioY));
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderQuad();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glEnable(GL_DEPTH_TEST);
}

void fKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
	{
		displayHelp = !displayHelp;
	}

	if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
	{
		displayFrameTime = !displayFrameTime;
		string frameTimeMsg = displayFrameTime ? "enabled" : "disabled";
		lastMessage = "Frame/time display " + frameTimeMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
	{
		wireFrameMode = !wireFrameMode;
		if (wireFrameMode)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
		string wireFrameModeMsg = wireFrameMode ? "enabled" : "disabled";
		lastMessage = "Wireframe mode " + wireFrameModeMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
	{
		texSettings::bilinear = !texSettings::bilinear;
		string bilinearMsg = texSettings::bilinear ? "bilinear" : "nearest neighbor";
		lastMessage = "Texture sampling: " + bilinearMsg;
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F5 && action == GLFW_PRESS)
	{
		texSettings::mipMappingQuality = (texSettings::mipMappingQuality + 1) % 3;

		string mipmapMsg;
		switch (texSettings::mipMappingQuality)
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
		bool normalMappingEnabled = !gameScene->isNormalMappingEnabled();
		gameScene->setNormalMappingEnabled(normalMappingEnabled);
		lastMessage = "Normal Mapping enabled: " + to_string(normalMappingEnabled);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F7 && action == GLFW_PRESS)
	{
		bool shadowMappingEnabled = !gameScene->isShadowMappingEnabled();
		gameScene->setShadowMappingEnabled(shadowMappingEnabled);
		lastMessage = "Shadow Mapping (PCF) enabled: " + to_string(shadowMappingEnabled);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F8 && action == GLFW_PRESS)
	{
		//bool viewFrustumCullingEnabled = gameScene->isNormalMappingEnabled();
		viewFrustumCulling = !viewFrustumCulling;
		/*if (viewFrustumCulling)
		{
			glEnable(GL_CULL_FACE);
			glFrontFace(GL_CW);
			glCullFace(GL_BACK);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}*/

		//gameScene->setNormalMappingEnabled(!normalMappingEnabled);
		lastMessage = "View Frustum Culling enabled: " + to_string(viewFrustumCulling);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F9 && action == GLFW_PRESS)
	{
		bool transparencyEnabled = !gameScene->isTransparencyEnabled();
		gameScene->setTransparencyEnabled(transparencyEnabled);
		lastMessage = "Transparency enabled: " + to_string(transparencyEnabled);
		messageTimeLeft = 3;
	}

	if (key == GLFW_KEY_F10 && action == GLFW_PRESS)
	{
		hdrEnabled = !hdrEnabled;
		lastMessage = "HDR enabled: " + to_string(hdrEnabled);
		messageTimeLeft = 3;
	}

	if (hdrEnabled && key == GLFW_KEY_F11 && action == GLFW_PRESS)
	{
		bloomEnabled = !bloomEnabled;
		lastMessage = "Bloom (requries HDR) enabled: " + to_string(bloomEnabled);
		messageTimeLeft = 3;
	}

	/*if (hdrEnabled && glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS)
	{
	varianceShadowMappingEnabled = !varianceShadowMappingEnabled;
	string shadowMappingType = varianceShadowMappingEnabled ? "Variance" : "PCF";
	lastMessage = "Shadow Mapping Mode: " + shadowMappingType;
	messageTimeLeft = 3;
	}

	if (hdrEnabled && glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)
	{
	hdrExposure = hdrExposure + 0.1f;
	if (hdrExposure > 6)
	{
	hdrExposure = 0.5;
	}
	lastMessage = "HDR Exposure: " + to_string(hdrExposure);
	messageTimeLeft = 3;
	}

	if (hdrEnabled && glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
	{
	hdrExposure = hdrExposure - 0.1f;
	if (hdrExposure < 0.5)
	{
	hdrExposure = 6;
	}
	lastMessage = "HDR Exposure: " + to_string(hdrExposure);
	messageTimeLeft = 3;
	}*/
}
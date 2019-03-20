#include "Shader.hpp"

#include <iostream>
#include <fstream>
#include <memory>

using namespace bibo;
using namespace std;

const bool LOCATION_NOT_FOUND_OUTPUT = FALSE;

Shader::Shader(const string& vertexShader, const string& fragmentShader)
	: programHandle(0)
	, vertexHandle(0)
	, fragmentHandle(0)

{
	programHandle = glCreateProgram();
	if (programHandle == 0)
	{
		cerr << "Failed to create shader program" << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);
	}
	loadShader(vertexShader, GL_VERTEX_SHADER, vertexHandle);
	loadShader(fragmentShader, GL_FRAGMENT_SHADER, fragmentHandle);
	link();
}


Shader::~Shader() {//Destructor
	glDeleteProgram(programHandle);
	glDeleteShader(vertexHandle);
	glDeleteShader(fragmentHandle);
}

void Shader::enable() const {
	glUseProgram(programHandle);
}

GLint Shader::getUniformLocation(const char* strUniform)
{
	auto loc = glGetUniformLocation(programHandle, strUniform);
	if (LOCATION_NOT_FOUND_OUTPUT && loc == 0xFFFFFFFF)
	{
		cerr << "Unable to get uniform location for " << strUniform << endl;
	}
	return loc;
}

GLint Shader::getAttribLocation(const char* strAttrib)
{
	auto loc = glGetAttribLocation(programHandle, strAttrib);
	if (loc == 0xFFFFFFFF)
	{
		cerr << "Unable to get attribute location for " << strAttrib << endl;
	}
	return loc;
}

void Shader::loadShader(const string& shader,
	GLenum shaderType,
	GLuint& handle)
{
	ifstream shaderFile(shader); //inputFileStream
	if (!shaderFile) {
		cerr << "Shader file could not be found or does not exist: " << shader << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);
	}

	if (!shaderFile.is_open()) {
		cerr << "Could not open file (maybe unauthorized): " << shader << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);
	}
	if (!shaderFile.good()) {
		cerr << "Failed to parse shader file (bad/damaged): " << shader << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);
	}

	auto code = string(istreambuf_iterator<char>(shaderFile),
		istreambuf_iterator<char>());

	shaderFile.close();

	handle = glCreateShader(shaderType);
	if (handle == 0)
	{
		cerr << "Failed to create shader" << endl;
		system("PAUSE");
		exit(EXIT_FAILURE);
	}

	auto codePtr = code.c_str();
	glShaderSource(handle, 1, &codePtr, nullptr);
	glCompileShader(handle);

	GLint succeeded;
	//glGetProgramiv(programHandle, GL_LINK_STATUS, &succeded);
	glGetShaderiv(handle, GL_COMPILE_STATUS, &succeeded);
	if (!succeeded || !glIsShader(handle)) {
		GLint logSize;
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logSize);

		auto msg = make_unique<char[]>(logSize);
		glGetShaderInfoLog(handle, logSize, nullptr, msg.get());

		cerr << "Failed to compile shader" << endl << msg.get() << endl;

		system("PAUSE");

		msg.reset(nullptr);

		exit(EXIT_FAILURE);

	}

}

void Shader::link() {
	glAttachShader(programHandle, vertexHandle);
	glAttachShader(programHandle, fragmentHandle);

	glLinkProgram(programHandle);

	GLint succeeded;
	glGetProgramiv(programHandle, GL_LINK_STATUS, &succeeded);

	if (!succeeded) {
		GLint logSize;
		glGetProgramiv(programHandle, GL_INFO_LOG_LENGTH, &logSize);
		//
		auto msg = make_unique<char[]>(logSize);
		glGetProgramInfoLog(programHandle, logSize, nullptr, msg.get());

		cerr << "Failed to link prog" << endl << msg.get() << endl;
		system("PAUSE");


		msg.reset(nullptr); //just eye-candy
		exit(EXIT_FAILURE);
	}
}
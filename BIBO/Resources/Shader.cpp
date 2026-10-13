#include "Shader.hpp"

#include <iostream>
#include <fstream>
#include <memory>
#include <cstdlib>
#include "../Util/Console.hpp"

using namespace bibo;
using namespace std;

const bool LOCATION_NOT_FOUND_OUTPUT = false;

Shader::Shader(const string& vertexShader, const string& fragmentShader)
	: programHandle(0)
	, vertexHandle(0)
	, fragmentHandle(0)

{
	programHandle = glCreateProgram();
	if (programHandle == 0)
	{
		cerr << "Failed to create shader program" << endl;
		util::pause();
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

namespace
{
	string readIncluding(const string& path, int depth, string& error)
	{
		ifstream file(path);
		if (!file)
		{
			error = "Shader file could not be found or does not exist: " + path;
			return "";
		}
		if (depth > 8)
		{
			error = "Shader files include each other: " + path;
			return "";
		}
		string folder;
		size_t slash = path.find_last_of("/\\");
		if (slash != string::npos)
		{
			folder = path.substr(0, slash + 1);
		}
		string code, line;
		while (getline(file, line))
		{
			size_t start = line.find_first_not_of(" \t");
			if (start != string::npos && line.compare(start, 8, "#include") == 0)
			{
				size_t open = line.find('"', start), close = line.find('"', open + 1);
				if (open == string::npos || close == string::npos)
				{
					error = "Invalid #include in " + path + ": " + line;
					return "";
				}
				string included = readIncluding(folder + line.substr(open + 1, close - open - 1), depth + 1, error);
				if (!error.empty())
				{
					return "";
				}
				code += included + "\n";
			}
			else
			{
				code += line + "\n";
			}
		}
		return code;
	}
}

string Shader::preprocess(const string& path, string& error)
{
	error.clear();
	return readIncluding(path, 0, error);
}

void Shader::loadShader(const string& shader,
	GLenum shaderType,
	GLuint& handle)
{
	string error;
	string code = preprocess(shader, error);
	if (!error.empty()) {
		cerr << error << endl;
		util::pause();
		exit(EXIT_FAILURE);
	}

	handle = glCreateShader(shaderType);
	if (handle == 0)
	{
		cerr << "Failed to create shader" << endl;
		util::pause();
		exit(EXIT_FAILURE);
	}

	auto codePtr = code.c_str();
	glShaderSource(handle, 1, &codePtr, nullptr);
	glCompileShader(handle);

	GLint succeeded;
	glGetShaderiv(handle, GL_COMPILE_STATUS, &succeeded);
	if (!succeeded || !glIsShader(handle)) {
		GLint logSize;
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logSize);

		auto msg = make_unique<char[]>(logSize);
		glGetShaderInfoLog(handle, logSize, nullptr, msg.get());

		cerr << "Failed to compile shader " << shader << endl << msg.get() << endl;

		util::pause();

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
		auto msg = make_unique<char[]>(logSize);
		glGetProgramInfoLog(programHandle, logSize, nullptr, msg.get());

		cerr << "Failed to link prog" << endl << msg.get() << endl;
		util::pause();


		msg.reset(nullptr); //just eye-candy
		exit(EXIT_FAILURE);
	}
}
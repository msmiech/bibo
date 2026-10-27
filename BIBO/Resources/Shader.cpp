#include "Shader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include "../Util/Console.hpp"

using namespace bibo;
using namespace std;

Shader::Shader(const string& vertexShader, const string& fragmentShader,
               const string& defines)
    : programHandle(glCreateProgram()) {
  if (programHandle == 0) {
    util::fatalError("Failed to create shader program");
  }
  loadShader(vertexShader, GL_VERTEX_SHADER, vertexHandle, defines);
  loadShader(fragmentShader, GL_FRAGMENT_SHADER, fragmentHandle, defines);
  link();
}

Shader::~Shader() {
  // A program in use would only be flagged for deletion: whoever saved it as
  // the current one (render::StateGuard) would later bind a program that no
  // longer exists
  GLint current = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &current);
  if (static_cast<GLuint>(current) == programHandle) {
    glUseProgram(0);
  }
  glDeleteProgram(programHandle);
  glDeleteShader(vertexHandle);
  glDeleteShader(fragmentHandle);
}

void Shader::enable() const { glUseProgram(programHandle); }

GLint Shader::getUniformLocation(const char* name) {
  return glGetUniformLocation(programHandle, name);
}

GLint Shader::getAttribLocation(const char* name) {
  GLint location = glGetAttribLocation(programHandle, name);
  if (location == -1) {
    cerr << "Unable to get attribute location for " << name << endl;
  }
  return location;
}

namespace {
string readIncluding(const string& path, int depth, string& error) {
  ifstream file(path);
  if (!file) {
    error = "Shader file could not be found or does not exist: " + path;
    return "";
  }
  if (depth > 8) {
    error = "Shader files include each other: " + path;
    return "";
  }
  string folder;
  size_t slash = path.find_last_of("/\\");
  if (slash != string::npos) {
    folder = path.substr(0, slash + 1);
  }
  string code, line;
  while (getline(file, line)) {
    size_t start = line.find_first_not_of(" \t");
    if (start != string::npos && line.compare(start, 8, "#include") == 0) {
      size_t open = line.find('"', start), close = line.find('"', open + 1);
      if (open == string::npos || close == string::npos) {
        error = "Invalid #include in " + path + ": " + line;
        return "";
      }
      string included = readIncluding(
          folder + line.substr(open + 1, close - open - 1), depth + 1, error);
      if (!error.empty()) {
        return "";
      }
      code += included + "\n";
    } else {
      code += line + "\n";
    }
  }
  return code;
}
}  // namespace

string Shader::preprocess(const string& path, string& error) {
  error.clear();
  return readIncluding(path, 0, error);
}

string Shader::addDefines(const string& code, const string& defines) {
  string lines;
  istringstream names(defines);
  string name;
  while (names >> name) {
    lines += "#define " + name + "\n";
  }
  if (lines.empty()) {
    return code;
  }
  // The #version line must stay the first one
  size_t version = code.find("#version");
  size_t end =
      version == string::npos ? string::npos : code.find('\n', version);
  if (end == string::npos) {
    return lines + code;
  }
  return code.substr(0, end + 1) + lines + code.substr(end + 1);
}

void Shader::loadShader(const string& shader, GLenum shaderType, GLuint& handle,
                        const string& defines) {
  string error;
  string code = addDefines(preprocess(shader, error), defines);
  if (!error.empty()) {
    util::fatalError(error);
  }
  handle = glCreateShader(shaderType);
  if (handle == 0) {
    util::fatalError("Failed to create shader " + shader);
  }
  const char* source = code.c_str();
  glShaderSource(handle, 1, &source, nullptr);
  glCompileShader(handle);
  GLint succeeded;
  glGetShaderiv(handle, GL_COMPILE_STATUS, &succeeded);
  if (!succeeded) {
    GLint logSize;
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logSize);
    string log(logSize, '\0');
    glGetShaderInfoLog(handle, logSize, nullptr, log.data());
    util::fatalError("Failed to compile shader " + shader + "\n" + log);
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
    string log(logSize, '\0');
    glGetProgramInfoLog(programHandle, logSize, nullptr, log.data());
    util::fatalError("Failed to link the shader program\n" + log);
  }
}

#include "Shader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include "../Util/Console.hpp"

namespace bibo {

using std::cerr;
using std::endl;
using std::ifstream;
using std::istringstream;
using std::string;

Shader::Shader(const string& vertex_shader, const string& fragment_shader,
               const string& defines)
    : program_handle(glCreateProgram()) {
  if (program_handle == 0) {
    util::FatalError("Failed to create shader program");
  }
  LoadShader(vertex_shader, GL_VERTEX_SHADER, vertex_handle_, defines);
  LoadShader(fragment_shader, GL_FRAGMENT_SHADER, fragment_handle_, defines);
  Link();
}

Shader::~Shader() {
  // A program in use would only be flagged for deletion: whoever saved it as
  // the current one (render::StateGuard) would later bind a program that no
  // longer exists
  GLint current = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &current);
  if (static_cast<GLuint>(current) == program_handle) {
    glUseProgram(0);
  }
  glDeleteProgram(program_handle);
  glDeleteShader(vertex_handle_);
  glDeleteShader(fragment_handle_);
}

void Shader::Enable() const { glUseProgram(program_handle); }

GLint Shader::GetUniformLocation(const char* name) {
  return glGetUniformLocation(program_handle, name);
}

GLint Shader::GetAttribLocation(const char* name) {
  GLint location = glGetAttribLocation(program_handle, name);
  if (location == -1) {
    cerr << "Unable to get attribute location for " << name << endl;
  }
  return location;
}

namespace {
string ReadIncluding(const string& path, int depth, string& error) {
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
      string included = ReadIncluding(
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

string Shader::Preprocess(const string& path, string& error) {
  error.clear();
  return ReadIncluding(path, 0, error);
}

string Shader::AddDefines(const string& code, const string& defines) {
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

void Shader::LoadShader(const string& shader, GLenum shader_type,
                        GLuint& handle, const string& defines) {
  string error;
  string code = AddDefines(Preprocess(shader, error), defines);
  if (!error.empty()) {
    util::FatalError(error);
  }
  handle = glCreateShader(shader_type);
  if (handle == 0) {
    util::FatalError("Failed to create shader " + shader);
  }
  const char* source = code.c_str();
  glShaderSource(handle, 1, &source, nullptr);
  glCompileShader(handle);
  GLint succeeded;
  glGetShaderiv(handle, GL_COMPILE_STATUS, &succeeded);
  if (!succeeded) {
    GLint log_size;
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_size);
    string log(log_size, '\0');
    glGetShaderInfoLog(handle, log_size, nullptr, log.data());
    util::FatalError("Failed to compile shader " + shader + "\n" + log);
  }
}

void Shader::Link() {
  glAttachShader(program_handle, vertex_handle_);
  glAttachShader(program_handle, fragment_handle_);
  glLinkProgram(program_handle);
  GLint succeeded;
  glGetProgramiv(program_handle, GL_LINK_STATUS, &succeeded);
  if (!succeeded) {
    GLint log_size;
    glGetProgramiv(program_handle, GL_INFO_LOG_LENGTH, &log_size);
    string log(log_size, '\0');
    glGetProgramInfoLog(program_handle, log_size, nullptr, log.data());
    util::FatalError("Failed to link the shader program\n" + log);
  }
}

}  // namespace bibo

#pragma once

#include <GL/glew.h>

#include <string>

using namespace std;

namespace bibo {

/**
 * Program of a vertex and a fragment shader file (the game exits with the log
 * of the compiler if they do not compile)
 */
class Shader {
 public:
  /**
   * @param vertexShader Path of the *.vert file
   * @param fragmentShader Path of the *.frag file
   * @param defines Names separated by spaces that are defined in both shaders
   * (variants of a shader, e.g. "LOD_DITHER")
   */
  Shader(const string& vertexShader, const string& fragmentShader,
         const string& defines = "");
  ~Shader();
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  // Makes it the current program
  void enable() const;

  GLint getUniformLocation(const char* name);
  // Prints an error if the attribute does not exist
  GLint getAttribLocation(const char* name);
  GLuint programHandle = 0;

  /**
   * Reads a shader file and replaces the lines #include "file" with the content
   * of the file (relative to the folder of the including file, e.g. the shared
   * lighting code in Shader/include)
   * @param error Description of a missing file
   * @return The source code, empty if a file is missing
   */
  static string preprocess(const string& path, string& error);
  /**
   * @param defines Names separated by spaces
   * @return The code with a line #define NAME for every name after the #version
   * line
   */
  static string addDefines(const string& code, const string& defines);

 private:
  void loadShader(const string& shader, GLenum shaderType, GLuint& handle,
                  const string& defines);
  void link();

  GLuint vertexHandle = 0;
  GLuint fragmentHandle = 0;
};
}  // namespace bibo

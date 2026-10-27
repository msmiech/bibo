#pragma once

#include <GL/glew.h>

#include <string>

using namespace std;

namespace bibo {

/**
Reads mainShader file
*/
class Shader {
 public:
  /**
  @param vertexShader Path to vertex mainShader file *.vert
  @param fragmentShader Path to fragment mainShader file *.frag
  @param defines Names separated by spaces that are defined in both shaders
  (variants of a shader, e.g. "LOD_DITHER")
  **/
  Shader(const string& vertexShader, const string& fragmentShader,
         const string& defines = "");
  ~Shader();

  /**
   * useShader() formerly
   * sometimes only "Use()"
   */
  void enable() const;

  GLint getUniformLocation(const char* strUniform);
  GLint getAttribLocation(const char* strAttrib);
  GLuint programHandle;

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

  GLuint vertexHandle;
  GLuint fragmentHandle;
};
}  // namespace bibo

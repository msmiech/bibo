#ifndef BIBO_RESOURCES_SHADER_HPP_
#define BIBO_RESOURCES_SHADER_HPP_

#include <GL/glew.h>

#include <string>

namespace bibo {

/**
 * Program of a vertex and a fragment shader file (the game exits with the log
 * of the compiler if they do not compile)
 */
class Shader {
 public:
  /**
   * @param vertex_shader Path of the *.vert file
   * @param fragment_shader Path of the *.frag file
   * @param defines Names separated by spaces that are defined in both shaders
   * (variants of a shader, e.g. "LOD_DITHER")
   */
  Shader(const std::string& vertex_shader, const std::string& fragment_shader,
         const std::string& defines = "");
  ~Shader();
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  // Makes it the current program
  void Enable() const;

  GLint GetUniformLocation(const char* name);
  // Prints an error if the attribute does not exist
  GLint GetAttribLocation(const char* name);
  GLuint program_handle = 0;

  /**
   * Reads a shader file and replaces the lines #include "file" with the content
   * of the file (relative to the folder of the including file, e.g. the shared
   * lighting code in Shader/include)
   * @param error Description of a missing file
   * @return The source code, empty if a file is missing
   */
  static std::string Preprocess(const std::string& path, std::string& error);
  /**
   * @param defines Names separated by spaces
   * @return The code with a line #define NAME for every name after the #version
   * line
   */
  static std::string AddDefines(const std::string& code,
                                const std::string& defines);

 private:
  void LoadShader(const std::string& shader, GLenum shader_type, GLuint& handle,
                  const std::string& defines);
  void Link();

  GLuint vertex_handle_ = 0;
  GLuint fragment_handle_ = 0;
};
}  // namespace bibo

#endif  // BIBO_RESOURCES_SHADER_HPP_

#include "RenderUtil.hpp"

#include <string>

namespace bibo::render {

bool IsSoftwareRenderer() {
  const GLubyte* name = glGetString(GL_RENDERER);
  if (!name) {
    return false;
  }
  std::string renderer(reinterpret_cast<const char*>(name));
  for (const char* software : {"llvmpipe", "softpipe", "SwiftShader",
                               "Software Rasterizer", "Basic Render"}) {
    if (renderer.find(software) != std::string::npos) {
      return true;
    }
  }
  return false;
}

render::FullscreenTriangle::FullscreenTriangle() {
  glGenVertexArrays(1, &vertex_array_);
}

render::FullscreenTriangle::~FullscreenTriangle() {
  glDeleteVertexArrays(1, &vertex_array_);
}

void render::FullscreenTriangle::Draw() const {
  glBindVertexArray(vertex_array_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

render::StateGuard::StateGuard() {
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer_);
  glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
  glGetIntegerv(GL_VIEWPORT, viewport_);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array_);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
  depth_test_ = glIsEnabled(GL_DEPTH_TEST);
  blend_ = glIsEnabled(GL_BLEND);
  cull_face_ = glIsEnabled(GL_CULL_FACE);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_);
}

render::StateGuard::~StateGuard() {
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer_));
  glUseProgram(static_cast<GLuint>(program_));
  glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
  glBindVertexArray(static_cast<GLuint>(vertex_array_));
  glActiveTexture(static_cast<GLenum>(active_texture_));
  depth_test_ ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
  blend_ ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
  cull_face_ ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
  glDepthMask(depth_mask_);
}

}  // namespace bibo::render

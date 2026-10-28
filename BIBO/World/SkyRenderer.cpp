#include "SkyRenderer.hpp"

#include <cmath>

namespace bibo::world {

SkyRenderer::SkyRenderer(bool low_detail)
    : environment_shader_(std::make_unique<Shader>(
          "Shader/fullscreen.vert", "Shader/sky_environment.frag")),
      sky_shader_(
          std::make_unique<Shader>("Shader/sky.vert", "Shader/sky.frag")),
      size_(low_detail ? 64 : 512),
      aurora_steps_(low_detail ? 10 : 32) {
  // Filtering across the edges of the faces (the blurred mip levels would show
  // the seams)
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  glGenTextures(1, &environment_);
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment_);
  int levels = static_cast<int>(std::log2(static_cast<float>(size_))) + 1;
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels, GL_RGBA16F, size_, size_);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  glGenFramebuffers(1, &framebuffer_);
}

SkyRenderer::~SkyRenderer() {
  glDeleteFramebuffers(1, &framebuffer_);
  glDeleteTextures(1, &environment_);
}

void SkyRenderer::RenderFace(int face) {
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, environment_,
                         0);
  glUniform1i(environment_shader_->GetUniformLocation("face"), face);
  triangle_.Draw();
}

void SkyRenderer::Update() {
  render::StateGuard state;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  GLint polygon_mode[2];
  glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &draw_buffer);
  glViewport(0, 0, size_, size_);
  environment_shader_->Enable();
  glUniform1i(environment_shader_->GetUniformLocation("auroraSteps"),
              aurora_steps_);
  if (!complete_) {
    for (int face = 0; face < 6; ++face) {
      RenderFace(face);
    }
    complete_ = true;
  } else {
    RenderFace(next_face_);
    next_face_ = (next_face_ + 1) % 6;
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment_);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon_mode[0]));
}

void SkyRenderer::Bind() const {
  glActiveTexture(GL_TEXTURE0 + render::texture_unit::kEnvironment);
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment_);
  glActiveTexture(GL_TEXTURE0);
}

void SkyRenderer::Draw() {
  GLint depth_function;
  glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
  GLboolean depth_mask;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
  // Only where the depth buffer is still at the far plane
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  sky_shader_->Enable();
  Bind();
  triangle_.Draw();
  glDepthFunc(static_cast<GLenum>(depth_function));
  glDepthMask(depth_mask);
}

}  // namespace bibo::world

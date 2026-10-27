#include "WaterRenderer.hpp"

namespace bibo::world {

namespace {
const float kWaterExtent = 2400.0f;
const float kSnowBoxWidth = 70.0f, kSnowBoxHeight = 40.0f;
const float kSnowFallSpeed = 1.1f;
}  // namespace

WaterRenderer::WaterRenderer()
    : shader_(
          std::make_unique<Shader>("Shader/water.vert", "Shader/water.frag")) {
  glGenVertexArrays(1, &vertex_array_);
}

WaterRenderer::~WaterRenderer() { glDeleteVertexArrays(1, &vertex_array_); }

void WaterRenderer::Draw(bool transparent) {
  shader_->Enable();
  glUniform1f(shader_->GetUniformLocation("extent"), kWaterExtent);
  glUniform1i(shader_->GetUniformLocation("transparent"), transparent);
  GLboolean blend = glIsEnabled(GL_BLEND),
            cull_face = glIsEnabled(GL_CULL_FACE);
  if (transparent) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDisable(GL_CULL_FACE);  // also seen from below (from under the water)
  glBindVertexArray(vertex_array_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
  cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

SnowRenderer::SnowRenderer(int flakes)
    : shader_(std::make_unique<Shader>("Shader/snow.vert", "Shader/snow.frag")),
      flakes_(flakes) {
  glGenVertexArrays(1, &vertex_array_);
}

SnowRenderer::~SnowRenderer() { glDeleteVertexArrays(1, &vertex_array_); }

void SnowRenderer::Draw() {
  if (flakes_ <= 0) {
    return;
  }
  shader_->Enable();
  glUniform3f(shader_->GetUniformLocation("boxSize"), kSnowBoxWidth,
              kSnowBoxHeight, kSnowBoxWidth);
  glUniform1f(shader_->GetUniformLocation("fallSpeed"), kSnowFallSpeed);
  GLboolean blend = glIsEnabled(GL_BLEND),
            cull_face = glIsEnabled(GL_CULL_FACE);
  GLboolean depth_mask;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glBindVertexArray(vertex_array_);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, flakes_);
  glBindVertexArray(0);
  glDepthMask(depth_mask);
  blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
  cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

}  // namespace bibo::world

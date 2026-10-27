#include "SkyRenderer.hpp"

#include <cmath>

using namespace bibo;
using namespace bibo::world;

SkyRenderer::SkyRenderer(bool lowDetail)
    : environmentShader(std::make_unique<Shader>(
          "Shader/fullscreen.vert", "Shader/sky_environment.frag")),
      skyShader(std::make_unique<Shader>("Shader/sky.vert", "Shader/sky.frag")),
      size(lowDetail ? 64 : 512),
      auroraSteps(lowDetail ? 10 : 32) {
  // Filtering across the edges of the faces (the blurred mip levels would show
  // the seams)
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  glGenTextures(1, &environment);
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment);
  int levels = static_cast<int>(std::log2(static_cast<float>(size))) + 1;
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels, GL_RGBA16F, size, size);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  glGenFramebuffers(1, &framebuffer);
}

SkyRenderer::~SkyRenderer() {
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &environment);
}

void SkyRenderer::renderFace(int face) {
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, environment, 0);
  glUniform1i(environmentShader->getUniformLocation("face"), face);
  triangle.draw();
}

void SkyRenderer::update() {
  render::StateGuard state;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  GLint polygonMode[2];
  glGetIntegerv(GL_POLYGON_MODE, polygonMode);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &drawBuffer);
  glViewport(0, 0, size, size);
  environmentShader->enable();
  glUniform1i(environmentShader->getUniformLocation("auroraSteps"),
              auroraSteps);
  if (!complete) {
    for (int face = 0; face < 6; ++face) {
      renderFace(face);
    }
    complete = true;
  } else {
    renderFace(nextFace);
    nextFace = (nextFace + 1) % 6;
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
}

void SkyRenderer::bind() const {
  glActiveTexture(GL_TEXTURE0 + render::TextureUnit::ENVIRONMENT);
  glBindTexture(GL_TEXTURE_CUBE_MAP, environment);
  glActiveTexture(GL_TEXTURE0);
}

void SkyRenderer::draw() {
  GLint depthFunction;
  glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
  GLboolean depthMask;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  // Only where the depth buffer is still at the far plane
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  skyShader->enable();
  bind();
  triangle.draw();
  glDepthFunc(static_cast<GLenum>(depthFunction));
  glDepthMask(depthMask);
}

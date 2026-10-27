#include "MaterialTextures.hpp"

#include <cmath>
#include <memory>

#include "../Render/RenderUtil.hpp"
#include "../Resources/Shader.hpp"

namespace bibo::world {

namespace {
// How bumpy the normal maps of the materials are
const float kBumpStrength[kMaterialLayerCount] = {1.5f, 3.0f, 4.0f, 4.5f,
                                                  2.0f, 6.0f, 3.5f, 3.0f};

int MipLevels(int size) {
  return static_cast<int>(std::floor(std::log2(static_cast<float>(size)))) + 1;
}

void SetSampling(GLenum target) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
  if (GLEW_EXT_texture_filter_anisotropic) {
    GLfloat maximum = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
    glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    std::fmin(8.0f, maximum));
  }
}
}  // namespace

MaterialTextures::MaterialTextures(int size) : size_(size) {
  render::StateGuard state;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  glGenTextures(1, &albedo_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, MipLevels(size), GL_SRGB8_ALPHA8, size,
                 size, kMaterialLayerCount);
  SetSampling(GL_TEXTURE_2D_ARRAY);
  glGenTextures(1, &normal_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, MipLevels(size), GL_RGBA8, size, size,
                 kMaterialLayerCount);
  SetSampling(GL_TEXTURE_2D_ARRAY);

  GLuint height;
  glGenTextures(1, &height);
  glBindTexture(GL_TEXTURE_2D, height);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16F, size, size);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  Shader generator("Shader/fullscreen.vert", "Shader/material_generator.frag");
  Shader normals("Shader/fullscreen.vert", "Shader/material_normals.frag");
  render::FullscreenTriangle triangle;
  GLuint framebuffer;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, size, size);
  for (int layer = 0; layer < kMaterialLayerCount; ++layer) {
    // 1. Color, roughness and height (the sRGB texture gets the linear color
    // converted)
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, albedo_, 0,
                              layer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, height, 0);
    const GLenum both[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, both);
    glEnable(GL_FRAMEBUFFER_SRGB);
    generator.Enable();
    glUniform1i(generator.GetUniformLocation("material"), layer);
    triangle.Draw();
    glDisable(GL_FRAMEBUFFER_SRGB);

    // 2. Normal and occlusion from the height
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, normal_, 0,
                              layer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, 0, 0);
    glDrawBuffers(1, both);
    normals.Enable();
    glUniform1f(normals.GetUniformLocation("bumpStrength"),
                kBumpStrength[layer]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, height);
    triangle.Draw();
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &height);

  glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

MaterialTextures::~MaterialTextures() {
  glDeleteTextures(1, &albedo_);
  glDeleteTextures(1, &normal_);
}

void MaterialTextures::Bind() const {
  glActiveTexture(GL_TEXTURE0 + render::texture_unit::kMaterialAlbedo);
  glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
  glActiveTexture(GL_TEXTURE0 + render::texture_unit::kMaterialNormal);
  glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
  glActiveTexture(GL_TEXTURE0);
}

}  // namespace bibo::world

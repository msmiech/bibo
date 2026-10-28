#include "Renderer.hpp"

#include <algorithm>
#include <iostream>

namespace bibo::render {

namespace {
// View depth where the shadow cascades end
const std::vector<float> kCascadeSplits = {14.0f, 42.0f, 120.0f, 320.0f};
// Objects this far between the sun and a cascade still cast shadows into it
const float kCasterDistance = 220.0f;
const int kLuminanceSize = 128;
const float kBloomThreshold = 1.0f;  // after the exposure
const float kBloomStrength = 0.35f;
const int kBloomBlurPasses = 4;  // of each direction
// Average luminance that is shown as middle gray, and the range of the exposure
// (a bright day stays bright)
const float kExposureKey = 0.16f;
const float kMinExposure = 0.35f, kMaxExposure = 1.6f;

GLuint CreateTexture(GLenum internal_format, int width, int height, int levels,
                     GLenum filter) {
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D, levels, internal_format, width, height);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  filter == GL_LINEAR_MIPMAP_LINEAR ? GL_LINEAR : filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

GLuint CreateFramebuffer(GLuint color_texture) {
  GLuint framebuffer;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Framebuffer not complete!" << std::endl;
  }
  return framebuffer;
}

void BindTexture(int unit, GLuint texture) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texture);
}
}  // namespace

Renderer::Renderer(int width, int height)
    : width_(std::max(width, 1)),
      height_(std::max(height, 1)),
      shadow_size_(IsSoftwareRenderer() ? 1024 : 2048),
      bright_pass_shader_(std::make_unique<Shader>("Shader/fullscreen.vert",
                                                   "Shader/bright_pass.frag")),
      blur_shader_(std::make_unique<Shader>(
          "Shader/fullscreen.vert", "Shader/gaussian_blur_shader.frag")),
      luminance_shader_(std::make_unique<Shader>("Shader/fullscreen.vert",
                                                 "Shader/luminance.frag")),
      exposure_shader_(std::make_unique<Shader>("Shader/fullscreen.vert",
                                                "Shader/exposure.frag")),
      composite_shader_(std::make_unique<Shader>(
          "Shader/fullscreen.vert", "Shader/post_processing_shader.frag")) {
  // Shadow maps: one layer per cascade, compared with the depth by the hardware
  // (sampler2DArrayShadow)
  glGenTextures(1, &shadow_maps_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps_);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, shadow_size_,
                 shadow_size_, kCascadeCount);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glGenFramebuffers(1, &shadow_framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_maps_,
                            0, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Shadow framebuffer not complete!" << std::endl;
  }

  // Eye adaptation: two 1 x 1 textures, the previous exposure and the new one
  for (int i = 0; i < 2; ++i) {
    exposure_textures_[i] = CreateTexture(GL_R32F, 1, 1, 1, GL_NEAREST);
    exposure_framebuffers_[i] = CreateFramebuffer(exposure_textures_[i]);
    const float one = 1.0f;
    glClearBufferfv(GL_COLOR, 0, &one);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  CreateTargets();
}

Renderer::~Renderer() {
  ReleaseTargets();
  glDeleteTextures(1, &shadow_maps_);
  glDeleteFramebuffers(1, &shadow_framebuffer_);
  glDeleteTextures(2, exposure_textures_);
  glDeleteFramebuffers(2, exposure_framebuffers_);
}

void Renderer::CreateTargets() {
  // HDR image of the scene
  hdr_color_ = CreateTexture(GL_RGBA16F, width_, height_, 1, GL_LINEAR);
  glGenRenderbuffers(1, &hdr_depth_);
  glBindRenderbuffer(GL_RENDERBUFFER, hdr_depth_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  hdr_framebuffer_ = CreateFramebuffer(hdr_color_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, hdr_depth_);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Framebuffer not complete!" << std::endl;
  }
  // Bloom at half the resolution
  int bloom_width = std::max(width_ / 2, 1),
      bloom_height = std::max(height_ / 2, 1);
  for (int i = 0; i < 2; ++i) {
    bloom_textures_[i] =
        CreateTexture(GL_RGBA16F, bloom_width, bloom_height, 1, GL_LINEAR);
    bloom_framebuffers_[i] = CreateFramebuffer(bloom_textures_[i]);
  }
  // Luminance with all mip levels down to 1 x 1
  int levels = 1;
  while ((kLuminanceSize >> levels) > 0) {
    ++levels;
  }
  luminance_texture_ = CreateTexture(GL_R16F, kLuminanceSize, kLuminanceSize,
                                     levels, GL_LINEAR_MIPMAP_LINEAR);
  luminance_framebuffer_ = CreateFramebuffer(luminance_texture_);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::ReleaseTargets() {
  glDeleteTextures(1, &hdr_color_);
  glDeleteRenderbuffers(1, &hdr_depth_);
  glDeleteFramebuffers(1, &hdr_framebuffer_);
  glDeleteTextures(2, bloom_textures_);
  glDeleteFramebuffers(2, bloom_framebuffers_);
  glDeleteTextures(1, &luminance_texture_);
  glDeleteFramebuffers(1, &luminance_framebuffer_);
}

void Renderer::Resize(int new_width, int new_height) {
  if (new_width <= 0 || new_height <= 0 ||
      (new_width == width_ && new_height == height_)) {
    return;
  }
  width_ = new_width;
  height_ = new_height;
  ReleaseTargets();
  CreateTargets();
}

void Renderer::RenderShadows(RenderableScene& scene) {
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
  glViewport(0, 0, shadow_size_, shadow_size_);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.1f, 2.0f);
  for (int i = 0; i < static_cast<int>(cascades_.size()); ++i) {
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadow_maps_,
                              0, i);
    glClear(GL_DEPTH_BUFFER_BIT);
    scene.DrawShadowCasters(cascades_[i]);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::Render(RenderableScene& scene, const RenderSettings& settings,
                      float time_delta) {
  GLint output_framebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &output_framebuffer);
  GLint polygon_mode[2];
  glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // 1. Uniforms of the frame
  CameraView camera = scene.GetCameraView();
  glm::vec3 to_sun(0.0f, 1.0f, 0.0f);
  FrameUniforms uniforms;
  scene.FillFrameUniforms(uniforms);
  to_sun = glm::vec3(uniforms.sun_direction_shadows);
  cascades_ = ComputeCascades(
      camera.view, camera.field_of_view, camera.aspect_ratio, camera.near_plane,
      kCascadeSplits, to_sun, shadow_size_, kCasterDistance);
  uniforms.view = camera.view;
  uniforms.projection = camera.projection;
  uniforms.view_projection = camera.projection * camera.view;
  uniforms.inverse_view_projection = glm::inverse(uniforms.view_projection);
  for (int i = 0; i < kCascadeCount; ++i) {
    uniforms.light_space[i] = cascades_[i].light_space;
    uniforms.cascade_splits[i] = cascades_[i].split_far;
    uniforms.cascade_texel_sizes[i] = cascades_[i].texel_size;
  }
  uniforms.camera_position_time =
      glm::vec4(camera.position, uniforms.camera_position_time.w);
  uniforms.sun_direction_shadows.w = settings.shadows ? 1.0f : 0.0f;
  uniforms.sun_color_normal_mapping.w = settings.normal_mapping ? 1.0f : 0.0f;
  uniforms.viewport = glm::vec4(width_, height_, 1.0f / width_, 1.0f / height_);
  uniforms.shadow_settings.x = static_cast<float>(kCascadeCount);
  frame_uniforms_.Upload(uniforms);
  frame_uniforms_.Bind();
  scene.PrepareFrame();

  // 2. Shadow maps
  if (settings.shadows) {
    glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon_mode[0]));
    RenderShadows(scene);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  // 3. The scene into the HDR image
  glBindFramebuffer(GL_FRAMEBUFFER, hdr_framebuffer_);
  glViewport(0, 0, width_, height_);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glActiveTexture(GL_TEXTURE0 + texture_unit::kShadowMaps);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_maps_);
  glActiveTexture(GL_TEXTURE0);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon_mode[0]));
  scene.DrawMain(settings);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // 4. Bloom, exposure, tone mapping into the output
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  PostProcess(settings, time_delta);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(output_framebuffer));
  glViewport(0, 0, width_, height_);
  composite_shader_->Enable();
  BindTexture(0, hdr_color_);
  BindTexture(1, bloom_textures_[0]);
  BindTexture(2, exposure_textures_[exposure_index_]);
  glUniform1i(composite_shader_->GetUniformLocation("hdrEnabled"),
              settings.hdr);
  glUniform1i(composite_shader_->GetUniformLocation("bloomEnabled"),
              settings.hdr && settings.bloom && !settings.wireframe);
  glUniform1f(composite_shader_->GetUniformLocation("bloomStrength"),
              kBloomStrength);
  triangle_.Draw();
  for (int unit = 2; unit >= 0; --unit) {
    BindTexture(unit, 0);
  }
  glActiveTexture(GL_TEXTURE0 + texture_unit::kShadowMaps);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon_mode[0]));
}

void Renderer::PostProcess(const RenderSettings& settings, float time_delta) {
  if (!settings.hdr) {
    return;
  }
  // Average luminance and the exposure adapted to it
  glBindFramebuffer(GL_FRAMEBUFFER, luminance_framebuffer_);
  glViewport(0, 0, kLuminanceSize, kLuminanceSize);
  luminance_shader_->Enable();
  BindTexture(0, hdr_color_);
  triangle_.Draw();
  glBindTexture(GL_TEXTURE_2D, luminance_texture_);
  glGenerateMipmap(GL_TEXTURE_2D);
  int previous = exposure_index_;
  exposure_index_ = 1 - exposure_index_;
  glBindFramebuffer(GL_FRAMEBUFFER, exposure_framebuffers_[exposure_index_]);
  glViewport(0, 0, 1, 1);
  exposure_shader_->Enable();
  BindTexture(0, luminance_texture_);
  BindTexture(1, exposure_textures_[previous]);
  glUniform1f(exposure_shader_->GetUniformLocation("timeDelta"), time_delta);
  glUniform1i(exposure_shader_->GetUniformLocation("reset"), exposure_reset_);
  glUniform1f(exposure_shader_->GetUniformLocation("key"), kExposureKey);
  glUniform2f(exposure_shader_->GetUniformLocation("exposureRange"),
              kMinExposure, kMaxExposure);
  triangle_.Draw();
  exposure_reset_ = false;
  BindTexture(1, 0);

  if (!settings.bloom || settings.wireframe) {
    return;
  }
  // Bright parts at half the resolution, blurred
  int bloom_width = std::max(width_ / 2, 1),
      bloom_height = std::max(height_ / 2, 1);
  glViewport(0, 0, bloom_width, bloom_height);
  glBindFramebuffer(GL_FRAMEBUFFER, bloom_framebuffers_[0]);
  bright_pass_shader_->Enable();
  BindTexture(0, hdr_color_);
  BindTexture(2, exposure_textures_[exposure_index_]);
  glUniform1f(bright_pass_shader_->GetUniformLocation("threshold"),
              kBloomThreshold);
  triangle_.Draw();
  BindTexture(2, 0);
  blur_shader_->Enable();
  GLint horizontal_location = blur_shader_->GetUniformLocation("horizontal");
  for (int pass = 0; pass < 2 * kBloomBlurPasses; ++pass) {
    bool horizontal = pass % 2 == 0;
    glBindFramebuffer(GL_FRAMEBUFFER, bloom_framebuffers_[horizontal ? 1 : 0]);
    BindTexture(0, bloom_textures_[horizontal ? 0 : 1]);
    glUniform1i(horizontal_location, horizontal);
    triangle_.Draw();
  }
  BindTexture(0, 0);
}

}  // namespace bibo::render

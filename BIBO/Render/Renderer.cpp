#include "Renderer.hpp"

#include <algorithm>
#include <iostream>

using namespace bibo;
using namespace bibo::render;

namespace {
// View depth where the shadow cascades end
const std::vector<float> CASCADE_SPLITS = {14.0f, 42.0f, 120.0f, 320.0f};
// Objects this far between the sun and a cascade still cast shadows into it
const float CASTER_DISTANCE = 220.0f;
const int LUMINANCE_SIZE = 128;
const float BLOOM_THRESHOLD = 1.0f;  // after the exposure
const float BLOOM_STRENGTH = 0.35f;
const int BLOOM_BLUR_PASSES = 4;  // of each direction
// Average luminance that is shown as middle gray, and the range of the exposure
// (a bright day stays bright)
const float EXPOSURE_KEY = 0.16f;
const float MIN_EXPOSURE = 0.35f, MAX_EXPOSURE = 1.6f;

GLuint createTexture(GLenum internalFormat, int width, int height, int levels,
                     GLenum filter) {
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D, levels, internalFormat, width, height);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  filter == GL_LINEAR_MIPMAP_LINEAR ? GL_LINEAR : filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

GLuint createFramebuffer(GLuint colorTexture) {
  GLuint framebuffer;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colorTexture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Framebuffer not complete!" << std::endl;
  }
  return framebuffer;
}

void bindTexture(int unit, GLuint texture) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texture);
}
}  // namespace

Renderer::Renderer(int width, int height)
    : width(std::max(width, 1)),
      height(std::max(height, 1)),
      shadowSize(isSoftwareRenderer() ? 1024 : 2048),
      brightPassShader(std::make_unique<Shader>("Shader/fullscreen.vert",
                                                "Shader/bright_pass.frag")),
      blurShader(std::make_unique<Shader>("Shader/fullscreen.vert",
                                          "Shader/gaussian_blur_shader.frag")),
      luminanceShader(std::make_unique<Shader>("Shader/fullscreen.vert",
                                               "Shader/luminance.frag")),
      exposureShader(std::make_unique<Shader>("Shader/fullscreen.vert",
                                              "Shader/exposure.frag")),
      compositeShader(std::make_unique<Shader>(
          "Shader/fullscreen.vert", "Shader/post_processing_shader.frag")) {
  // Shadow maps: one layer per cascade, compared with the depth by the hardware
  // (sampler2DArrayShadow)
  glGenTextures(1, &shadowMaps);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMaps);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, shadowSize,
                 shadowSize, CASCADE_COUNT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glGenFramebuffers(1, &shadowFramebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMaps, 0,
                            0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Shadow framebuffer not complete!" << std::endl;
  }

  // Eye adaptation: two 1 x 1 textures, the previous exposure and the new one
  for (int i = 0; i < 2; ++i) {
    exposureTextures[i] = createTexture(GL_R32F, 1, 1, 1, GL_NEAREST);
    exposureFramebuffers[i] = createFramebuffer(exposureTextures[i]);
    const float one = 1.0f;
    glClearBufferfv(GL_COLOR, 0, &one);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  createTargets();
}

Renderer::~Renderer() {
  releaseTargets();
  glDeleteTextures(1, &shadowMaps);
  glDeleteFramebuffers(1, &shadowFramebuffer);
  glDeleteTextures(2, exposureTextures);
  glDeleteFramebuffers(2, exposureFramebuffers);
}

void Renderer::createTargets() {
  // HDR image of the scene
  hdrColor = createTexture(GL_RGBA16F, width, height, 1, GL_LINEAR);
  glGenRenderbuffers(1, &hdrDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, hdrDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  hdrFramebuffer = createFramebuffer(hdrColor);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, hdrDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "ERROR: Framebuffer not complete!" << std::endl;
  }
  // Bloom at half the resolution
  int bloomWidth = std::max(width / 2, 1),
      bloomHeight = std::max(height / 2, 1);
  for (int i = 0; i < 2; ++i) {
    bloomTextures[i] =
        createTexture(GL_RGBA16F, bloomWidth, bloomHeight, 1, GL_LINEAR);
    bloomFramebuffers[i] = createFramebuffer(bloomTextures[i]);
  }
  // Luminance with all mip levels down to 1 x 1
  int levels = 1;
  while ((LUMINANCE_SIZE >> levels) > 0) {
    ++levels;
  }
  luminanceTexture = createTexture(GL_R16F, LUMINANCE_SIZE, LUMINANCE_SIZE,
                                   levels, GL_LINEAR_MIPMAP_LINEAR);
  luminanceFramebuffer = createFramebuffer(luminanceTexture);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::releaseTargets() {
  glDeleteTextures(1, &hdrColor);
  glDeleteRenderbuffers(1, &hdrDepth);
  glDeleteFramebuffers(1, &hdrFramebuffer);
  glDeleteTextures(2, bloomTextures);
  glDeleteFramebuffers(2, bloomFramebuffers);
  glDeleteTextures(1, &luminanceTexture);
  glDeleteFramebuffers(1, &luminanceFramebuffer);
}

void Renderer::resize(int newWidth, int newHeight) {
  if (newWidth <= 0 || newHeight <= 0 ||
      (newWidth == width && newHeight == height)) {
    return;
  }
  width = newWidth;
  height = newHeight;
  releaseTargets();
  createTargets();
}

void Renderer::renderShadows(RenderableScene& scene) {
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer);
  glViewport(0, 0, shadowSize, shadowSize);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.1f, 2.0f);
  for (int i = 0; i < static_cast<int>(cascades.size()); ++i) {
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMaps,
                              0, i);
    glClear(GL_DEPTH_BUFFER_BIT);
    scene.drawShadowCasters(cascades[i]);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::render(RenderableScene& scene, const RenderSettings& settings,
                      float timeDelta) {
  GLint outputFramebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &outputFramebuffer);
  GLint polygonMode[2];
  glGetIntegerv(GL_POLYGON_MODE, polygonMode);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // 1. Uniforms of the frame
  CameraView camera = scene.cameraView();
  glm::vec3 toSun(0.0f, 1.0f, 0.0f);
  FrameUniforms uniforms;
  scene.fillFrameUniforms(uniforms);
  toSun = glm::vec3(uniforms.sunDirectionShadows);
  cascades = computeCascades(
      camera.view, camera.fieldOfView, camera.aspectRatio, camera.nearPlane,
      CASCADE_SPLITS, toSun, shadowSize, CASTER_DISTANCE);
  uniforms.view = camera.view;
  uniforms.projection = camera.projection;
  uniforms.viewProjection = camera.projection * camera.view;
  uniforms.inverseViewProjection = glm::inverse(uniforms.viewProjection);
  for (int i = 0; i < CASCADE_COUNT; ++i) {
    uniforms.lightSpace[i] = cascades[i].lightSpace;
    uniforms.cascadeSplits[i] = cascades[i].splitFar;
    uniforms.cascadeTexelSizes[i] = cascades[i].texelSize;
  }
  uniforms.cameraPositionTime =
      glm::vec4(camera.position, uniforms.cameraPositionTime.w);
  uniforms.sunDirectionShadows.w = settings.shadows ? 1.0f : 0.0f;
  uniforms.sunColorNormalMapping.w = settings.normalMapping ? 1.0f : 0.0f;
  uniforms.viewport = glm::vec4(width, height, 1.0f / width, 1.0f / height);
  uniforms.shadowSettings.x = static_cast<float>(CASCADE_COUNT);
  frameUniforms.upload(uniforms);
  frameUniforms.bind();
  scene.prepareFrame();

  // 2. Shadow maps
  if (settings.shadows) {
    glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
    renderShadows(scene);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  // 3. The scene into the HDR image
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFramebuffer);
  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glActiveTexture(GL_TEXTURE0 + TextureUnit::SHADOW_MAPS);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMaps);
  glActiveTexture(GL_TEXTURE0);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
  scene.drawMain(settings);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // 4. Bloom, exposure, tone mapping into the output
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  postProcess(settings, timeDelta);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(outputFramebuffer));
  glViewport(0, 0, width, height);
  compositeShader->enable();
  bindTexture(0, hdrColor);
  bindTexture(1, bloomTextures[0]);
  bindTexture(2, exposureTextures[exposureIndex]);
  glUniform1i(compositeShader->getUniformLocation("hdrEnabled"), settings.hdr);
  glUniform1i(compositeShader->getUniformLocation("bloomEnabled"),
              settings.hdr && settings.bloom && !settings.wireframe);
  glUniform1f(compositeShader->getUniformLocation("bloomStrength"),
              BLOOM_STRENGTH);
  triangle.draw();
  for (int unit = 2; unit >= 0; --unit) {
    bindTexture(unit, 0);
  }
  glActiveTexture(GL_TEXTURE0 + TextureUnit::SHADOW_MAPS);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygonMode[0]));
}

void Renderer::postProcess(const RenderSettings& settings, float timeDelta) {
  if (!settings.hdr) {
    return;
  }
  // Average luminance and the exposure adapted to it
  glBindFramebuffer(GL_FRAMEBUFFER, luminanceFramebuffer);
  glViewport(0, 0, LUMINANCE_SIZE, LUMINANCE_SIZE);
  luminanceShader->enable();
  bindTexture(0, hdrColor);
  triangle.draw();
  glBindTexture(GL_TEXTURE_2D, luminanceTexture);
  glGenerateMipmap(GL_TEXTURE_2D);
  int previous = exposureIndex;
  exposureIndex = 1 - exposureIndex;
  glBindFramebuffer(GL_FRAMEBUFFER, exposureFramebuffers[exposureIndex]);
  glViewport(0, 0, 1, 1);
  exposureShader->enable();
  bindTexture(0, luminanceTexture);
  bindTexture(1, exposureTextures[previous]);
  glUniform1f(exposureShader->getUniformLocation("timeDelta"), timeDelta);
  glUniform1i(exposureShader->getUniformLocation("reset"), exposureReset);
  glUniform1f(exposureShader->getUniformLocation("key"), EXPOSURE_KEY);
  glUniform2f(exposureShader->getUniformLocation("exposureRange"), MIN_EXPOSURE,
              MAX_EXPOSURE);
  triangle.draw();
  exposureReset = false;
  bindTexture(1, 0);

  if (!settings.bloom || settings.wireframe) {
    return;
  }
  // Bright parts at half the resolution, blurred
  int bloomWidth = std::max(width / 2, 1),
      bloomHeight = std::max(height / 2, 1);
  glViewport(0, 0, bloomWidth, bloomHeight);
  glBindFramebuffer(GL_FRAMEBUFFER, bloomFramebuffers[0]);
  brightPassShader->enable();
  bindTexture(0, hdrColor);
  bindTexture(2, exposureTextures[exposureIndex]);
  glUniform1f(brightPassShader->getUniformLocation("threshold"),
              BLOOM_THRESHOLD);
  triangle.draw();
  bindTexture(2, 0);
  blurShader->enable();
  GLint horizontalLocation = blurShader->getUniformLocation("horizontal");
  for (int pass = 0; pass < 2 * BLOOM_BLUR_PASSES; ++pass) {
    bool horizontal = pass % 2 == 0;
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFramebuffers[horizontal ? 1 : 0]);
    bindTexture(0, bloomTextures[horizontal ? 0 : 1]);
    glUniform1i(horizontalLocation, horizontal);
    triangle.draw();
  }
  bindTexture(0, 0);
}

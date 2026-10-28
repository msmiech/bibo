#include "BearRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

namespace bibo::scene {

namespace {
// Up to this distance from the camera all strands are drawn
const float kFullDetailDistance = 14.0f;
const float kMinDetail = 0.12f;
const std::uint32_t kStrandSeed = 1234;

void SetMatrix(Shader* shader, const char* name, const glm::mat4& matrix) {
  glUniformMatrix4fv(shader->GetUniformLocation(name), 1, GL_FALSE,
                     glm::value_ptr(matrix));
}
}  // namespace

BearRenderer::BearRenderer(const resources::SkinnedModel& model,
                           std::size_t strand_count)
    : skin_shader_(std::make_unique<Shader>("Shader/bear_skin.vert",
                                            "Shader/bear_skin.frag")),
      fur_shader_(std::make_unique<Shader>("Shader/bear_fur.vert",
                                           "Shader/bear_fur.frag")),
      vertex_count_(model.positions.size()) {
  staging_.resize(vertex_count_ * 4);

  glGenVertexArrays(1, &vertex_array_);
  glBindVertexArray(vertex_array_);

  glGenBuffers(1, &skin_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, skin_buffer_);
  glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4),
                        nullptr);  // position
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4),
                        reinterpret_cast<void*>(sizeof(glm::vec4)));  // normal
  glEnableVertexAttribArray(1);

  glGenBuffers(1, &color_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, color_buffer_);
  glBufferData(GL_ARRAY_BUFFER, model.colors.size() * sizeof(glm::vec4),
               model.colors.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(2);

  glGenBuffers(1, &surface_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, surface_buffer_);
  glBufferData(GL_ARRAY_BUFFER, model.surfaces.size() * sizeof(float),
               model.surfaces.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(3);

  glGenBuffers(1, &index_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               model.indices.size() * sizeof(unsigned int),
               model.indices.data(), GL_STATIC_DRAW);
  index_count_ = static_cast<GLsizei>(model.indices.size());
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  std::vector<fur::Strand> strands =
      fur::GenerateStrands(model.positions, model.colors,
                           model.Triangles(resources::SkinnedModel::kFur),
                           strand_count, kStrandSeed);
  this->strand_count_ = strands.size();
  glGenBuffers(1, &strand_buffer_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, strand_buffer_);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<std::size_t>(strands.size(), 1) * sizeof(fur::Strand),
               strands.empty() ? nullptr : strands.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glGenVertexArrays(1, &strand_vertex_array_);
}

BearRenderer::~BearRenderer() {
  GLuint buffers[] = {skin_buffer_, color_buffer_, surface_buffer_,
                      index_buffer_, strand_buffer_};
  glDeleteBuffers(5, buffers);
  glDeleteVertexArrays(1, &vertex_array_);
  glDeleteVertexArrays(1, &strand_vertex_array_);
}

void BearRenderer::Upload(const std::vector<glm::vec3>& positions,
                          const std::vector<glm::vec3>& normals,
                          const std::vector<glm::vec3>& fur_offsets,
                          const std::vector<glm::vec3>& comb_directions) {
  for (std::size_t i = 0; i < vertex_count_; ++i) {
    staging_[i * 4] = glm::vec4(positions[i], 1.0f);
    staging_[i * 4 + 1] = glm::vec4(normals[i], 0.0f);
    staging_[i * 4 + 2] = glm::vec4(
        i < fur_offsets.size() ? fur_offsets[i] : glm::vec3(0.0f), 0.0f);
    staging_[i * 4 + 3] = glm::vec4(
        i < comb_directions.size() ? comb_directions[i] : normals[i], 0.0f);
  }
  glBindBuffer(GL_ARRAY_BUFFER, skin_buffer_);
  // Orphaning: the driver can give us new memory while the GPU still draws the
  // previous frame
  glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, staging_.size() * sizeof(glm::vec4),
                  staging_.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void BearRenderer::DrawSurface(Shader* shader) {
  GLint model_location = shader->GetUniformLocation("model");
  if (model_location >= 0) {
    SetMatrix(shader, "model",
              glm::mat4(1.0f));  // the vertices are in world space
  }
  glBindVertexArray(vertex_array_);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void BearRenderer::DrawShaded(const BearRenderContext& context,
                              glm::vec3 center) {
  auto set_common_uniforms = [&](Shader* shader) {
    shader->Enable();
    SetMatrix(shader, "view", context.view);
    SetMatrix(shader, "projection", context.projection);
    glUniform3fv(shader->GetUniformLocation("worldEye"), 1,
                 glm::value_ptr(context.eye));
  };

  set_common_uniforms(skin_shader_.get());
  glBindVertexArray(vertex_array_);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);

  if (strand_count_ == 0) {
    glBindVertexArray(0);
    return;
  }
  // Level of detail: the strands are in random order, so the first ones are
  // spread over the whole body
  float distance = glm::distance(context.eye, center);
  float detail = std::clamp(
      std::pow(kFullDetailDistance / std::max(distance, 0.01f), 2.0f),
      kMinDetail, 1.0f);
  drawn_strand_count_ = std::max<std::size_t>(
      1, static_cast<std::size_t>(static_cast<float>(strand_count_) * detail));
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  float pixel_size = 2.0f / (context.projection[1][1] *
                             static_cast<float>(std::max(viewport[3], 1)));

  set_common_uniforms(fur_shader_.get());
  glUniform1i(fur_shader_->GetUniformLocation("segments"), kStrandSegments);
  glUniform1f(fur_shader_->GetUniformLocation("maxFurLength"), max_fur_length_);
  glUniform1f(fur_shader_->GetUniformLocation("strandWidth"),
              kStrandWidth / std::sqrt(detail));  // fewer strands are wider
  glUniform1f(fur_shader_->GetUniformLocation("pixelSize"), pixel_size);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, skin_buffer_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, color_buffer_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, strand_buffer_);

  GLboolean blend = glIsEnabled(GL_BLEND),
            cull_face = glIsEnabled(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);  // the ribbons face the camera anyway
  glBindVertexArray(strand_vertex_array_);
  const GLsizei vertices_per_strand = 2 * (kStrandSegments + 1) + 2;
  glDrawArrays(GL_TRIANGLE_STRIP, 0,
               static_cast<GLsizei>(drawn_strand_count_) * vertices_per_strand);
  glBindVertexArray(0);
  if (!blend) {
    glDisable(GL_BLEND);
  }
  if (cull_face) {
    glEnable(GL_CULL_FACE);
  }
  for (GLuint binding = 0; binding < 3; ++binding) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
  }
}

}  // namespace bibo::scene

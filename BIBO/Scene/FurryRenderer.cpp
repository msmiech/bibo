#include "FurryRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

#include "Fur.hpp"

namespace bibo::scene {

namespace {
// Up to this distance from the camera all strands are drawn (like the bear)
const float kFullDetailDistance = 24.0f;
const float kMinDetail = 0.12f;
// position, normal, bend, comb (like the skin buffer of the bear)
const GLsizei kVertexSize = 4 * sizeof(glm::vec4);

void SetMatrix(Shader* shader, const char* name, const glm::mat4& matrix) {
  glUniformMatrix4fv(shader->GetUniformLocation(name), 1, GL_FALSE,
                     glm::value_ptr(matrix));
}

GLuint StrandBuffer(const std::vector<fur::Strand>& strands) {
  GLuint buffer;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               std::max<std::size_t>(strands.size(), 1) * sizeof(fur::Strand),
               strands.empty() ? nullptr : strands.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  return buffer;
}
}  // namespace

FurryRenderer::FurryRenderer(FurryModel furry_model, std::size_t count,
                             const Strands& strands, const Bounds& bounds)
    : model_(std::move(furry_model)),
      count_(count),
      vertex_count_(model_.positions.size()),
      strands_(strands),
      bounds_(bounds),
      skin_shader_(std::make_unique<Shader>("Shader/bear_skin.vert",
                                            "Shader/bear_skin.frag")),
      fur_shader_(std::make_unique<Shader>("Shader/bear_fur.vert",
                                           "Shader/bear_fur.frag")),
      poses_(count),
      centers_(count, glm::vec3(0.0f)),
      radii_(count, bounds.radius) {
  // Every animal has its block in the skin buffer, which the fur shader reads
  // as a shader storage buffer from its start
  GLint alignment = 256;
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
  alignment = std::max(alignment, 16);
  GLsizeiptr bytes = static_cast<GLsizeiptr>(vertex_count_) * kVertexSize;
  block_size_ = (bytes + alignment - 1) / alignment * alignment;
  staging_.assign(
      count * static_cast<std::size_t>(block_size_) / sizeof(glm::vec4),
      glm::vec4(0.0f));

  glGenVertexArrays(1, &vertex_array_);
  glBindVertexArray(vertex_array_);
  glGenBuffers(1, &skin_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, skin_buffer_);
  glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glGenBuffers(1, &color_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, color_buffer_);
  glBufferData(GL_ARRAY_BUFFER, model_.colors.size() * sizeof(glm::vec4),
               model_.colors.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &surface_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, surface_buffer_);
  glBufferData(GL_ARRAY_BUFFER, model_.surfaces.size() * sizeof(float),
               model_.surfaces.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  // Binding 0: the skin of one animal (set for every animal), 1: the colors, 2:
  // the surfaces (the same for all)
  glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);  // position
  glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4));  // normal
  glVertexAttribBinding(0, 0);
  glVertexAttribBinding(1, 0);
  glVertexAttribFormat(2, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexAttribBinding(2, 1);
  glVertexAttribFormat(3, 1, GL_FLOAT, GL_FALSE, 0);
  glVertexAttribBinding(3, 2);
  for (GLuint attribute = 0; attribute < 4; ++attribute) {
    glEnableVertexAttribArray(attribute);
  }
  glBindVertexBuffer(0, skin_buffer_, 0, kVertexSize);
  glBindVertexBuffer(1, color_buffer_, 0, sizeof(glm::vec4));
  glBindVertexBuffer(2, surface_buffer_, 0, sizeof(float));
  glGenBuffers(1, &index_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               model_.indices.size() * sizeof(unsigned int),
               model_.indices.data(), GL_STATIC_DRAW);
  index_count_ = static_cast<GLsizei>(model_.indices.size());
  glBindVertexArray(0);

  std::vector<fur::Strand> spines;
  if (strands.spine_count > 0 && !model_.spine_triangles.empty()) {
    spines = fur::GenerateStrands(model_.positions, model_.colors,
                                  model_.spine_triangles, strands.spine_count,
                                  strands.spine_seed);
  }
  std::vector<fur::Strand> fur;
  if (strands.fur_count > 0 && !model_.fur_triangles.empty()) {
    fur = fur::GenerateStrands(model_.positions, model_.colors,
                               model_.fur_triangles, strands.fur_count,
                               strands.fur_seed);
  }
  spine_count_ = spines.size();
  fur_count_ = fur.size();
  spine_buffer_ = StrandBuffer(spines);
  fur_buffer_ = StrandBuffer(fur);
  glGenVertexArrays(1, &strand_vertex_array_);
}

FurryRenderer::~FurryRenderer() {
  GLuint buffers[] = {skin_buffer_,  color_buffer_, surface_buffer_,
                      index_buffer_, spine_buffer_, fur_buffer_};
  glDeleteBuffers(6, buffers);
  glDeleteVertexArrays(1, &vertex_array_);
  glDeleteVertexArrays(1, &strand_vertex_array_);
}

void FurryRenderer::SetPose(std::size_t index,
                            const std::vector<glm::mat4>& parts,
                            float bristle) {
  if (index >= count_ || parts.empty()) {
    return;
  }
  // The vertices are only moved when the animal is drawn (the scene may be
  // updated more often than drawn, e.g. in the tests)
  Pose& pose = poses_[index];
  pose.parts = parts;
  pose.bristle = bristle;
  pose.moved = false;
  radii_[index] = bounds_.radius * glm::length(glm::vec3(parts[0][0]));
  centers_[index] = glm::vec3(parts[0] * glm::vec4(bounds_.center, 1.0f));
  uploaded_ = false;
}

void FurryRenderer::MoveVertices(std::size_t index) {
  Pose& pose = poses_[index];
  if (pose.moved) {
    return;
  }
  pose.moved = true;
  std::vector<glm::mat3> rotations(pose.parts.begin(), pose.parts.end());
  glm::vec4* block =
      staging_.data() +
      index * static_cast<std::size_t>(block_size_) / sizeof(glm::vec4);
  for (std::size_t v = 0; v < vertex_count_; ++v) {
    std::size_t part = static_cast<std::size_t>(model_.parts[v]);
    part = part < pose.parts.size() ? part : 0;
    const glm::mat3& rotation = rotations[part];
    glm::vec3 normal = glm::normalize(rotation * model_.normals[v]);
    block[v * 4] = pose.parts[part] * glm::vec4(model_.positions[v], 1.0f);
    block[v * 4 + 1] = glm::vec4(normal, 0.0f);
    // Spines stand up (e.g. a startled hedgehog)
    block[v * 4 + 2] = glm::vec4(
        model_.spines[v] ? normal * (0.9f * pose.bristle) : glm::vec3(0.0f),
        0.0f);
    block[v * 4 + 3] =
        glm::vec4(glm::normalize(rotation * model_.combs[v]), 0.0f);
  }
}

void FurryRenderer::Upload() {
  if (uploaded_) {
    return;
  }
  for (std::size_t i = 0; i < count_; ++i) {
    MoveVertices(i);
  }
  glBindBuffer(GL_ARRAY_BUFFER, skin_buffer_);
  // Orphaning: new memory while the graphics card may still draw the last frame
  glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, staging_.size() * sizeof(glm::vec4),
                  staging_.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  uploaded_ = true;
}

void FurryRenderer::BindVertices(std::size_t index) {
  glBindVertexBuffer(0, skin_buffer_,
                     static_cast<GLintptr>(index) * block_size_, kVertexSize);
}

void FurryRenderer::DrawSurfaces(Shader* shader) {
  Upload();
  if (shader->GetUniformLocation("model") >= 0) {
    SetMatrix(shader, "model",
              glm::mat4(1.0f));  // the vertices are in world space
  }
  glBindVertexArray(vertex_array_);
  for (std::size_t i = 0; i < count_; ++i) {
    BindVertices(i);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

void FurryRenderer::DrawShaded(const BearRenderContext& context,
                               const ViewFrustum& frustum) {
  Upload();
  std::vector<std::size_t> visible;
  for (std::size_t i = 0; i < count_; ++i) {
    if (frustum.SphereInFrustum(centers_[i], radii_[i]) !=
        ViewFrustum::kOutside) {
      visible.push_back(i);
    }
  }
  drawn_count_ = static_cast<int>(visible.size());
  if (visible.empty()) {
    return;
  }
  auto set_common_uniforms = [&](Shader* shader) {
    shader->Enable();
    SetMatrix(shader, "view", context.view);
    SetMatrix(shader, "projection", context.projection);
    glUniform3fv(shader->GetUniformLocation("worldEye"), 1,
                 glm::value_ptr(context.eye));
  };

  set_common_uniforms(skin_shader_.get());
  glBindVertexArray(vertex_array_);
  for (std::size_t i : visible) {
    BindVertices(i);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  }

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  float pixel_size = 2.0f / (context.projection[1][1] *
                             static_cast<float>(std::max(viewport[3], 1)));
  set_common_uniforms(fur_shader_.get());
  glUniform1i(fur_shader_->GetUniformLocation("segments"),
              BearRenderer::kStrandSegments);
  glUniform1f(fur_shader_->GetUniformLocation("pixelSize"), pixel_size);
  GLint spines_location = fur_shader_->GetUniformLocation("spines");
  GLint length_location = fur_shader_->GetUniformLocation("maxFurLength");
  GLint width_location = fur_shader_->GetUniformLocation("strandWidth");
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, color_buffer_);

  GLboolean blend = glIsEnabled(GL_BLEND),
            cull_face = glIsEnabled(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);  // the ribbons face the camera anyway
  glBindVertexArray(strand_vertex_array_);
  const GLsizei vertices_per_strand =
      2 * (BearRenderer::kStrandSegments + 1) + 2;
  for (std::size_t i : visible) {
    // Level of detail: the strands are in random order, the first ones are
    // spread over the whole animal
    float distance = glm::distance(context.eye, centers_[i]);
    float detail = std::clamp(
        std::pow(kFullDetailDistance / std::max(distance, 0.01f), 2.0f),
        kMinDetail, 1.0f);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, skin_buffer_,
                      static_cast<GLintptr>(i) * block_size_,
                      static_cast<GLsizeiptr>(vertex_count_) * kVertexSize);
    // The fur first, the spines lie on it
    if (fur_count_ > 0) {
      glUniform1i(spines_location, 0);
      glUniform1f(length_location, fur::kMaxLength);
      glUniform1f(width_location,
                  BearRenderer::kStrandWidth / std::sqrt(detail));
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, fur_buffer_);
      std::size_t drawn = std::max<std::size_t>(
          1, static_cast<std::size_t>(static_cast<float>(fur_count_) * detail));
      glDrawArrays(GL_TRIANGLE_STRIP, 0,
                   static_cast<GLsizei>(std::min(drawn, fur_count_)) *
                       vertices_per_strand);
    }
    if (spine_count_ > 0) {
      glUniform1i(spines_location, 1);
      glUniform1f(length_location, strands_.spine_length);
      glUniform1f(width_location, strands_.spine_width / std::sqrt(detail));
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spine_buffer_);
      std::size_t drawn = std::max<std::size_t>(
          1,
          static_cast<std::size_t>(static_cast<float>(spine_count_) * detail));
      glDrawArrays(GL_TRIANGLE_STRIP, 0,
                   static_cast<GLsizei>(std::min(drawn, spine_count_)) *
                       vertices_per_strand);
    }
  }
  glUniform1i(spines_location, 0);
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

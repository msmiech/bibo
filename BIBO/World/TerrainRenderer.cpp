#include "TerrainRenderer.hpp"

#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

namespace bibo::world {

TerrainRenderer::TerrainRenderer()
    : shader_(std::make_unique<Shader>("Shader/terrain.vert",
                                       "Shader/terrain.frag")),
      dither_shader_(std::make_unique<Shader>(
          "Shader/terrain.vert", "Shader/terrain.frag", "LOD_DITHER")),
      depth_shader_(std::make_unique<Shader>("Shader/depth_shader.vert",
                                             "Shader/depth_shader.frag")) {}

TerrainRenderer::~TerrainRenderer() {
  for (auto& entry : chunks_) {
    Release(entry.second);
  }
  for (auto& entry : fading_) {
    Release(entry.second.old);
  }
}

void TerrainRenderer::Release(GpuChunk& chunk) {
  glDeleteVertexArrays(1, &chunk.vertex_array);
  GLuint buffers[] = {chunk.vertex_buffer, chunk.index_buffer};
  glDeleteBuffers(2, buffers);
}

void TerrainRenderer::Apply(const WorldStreamer::MeshUpdate& update,
                            float time) {
  bool replaced = update.mesh && !update.mesh->Empty();
  // A mesh that was still fading out is gone now, the current one fades out
  // instead (unless it just came, e.g. while loading)
  auto previous = fading_.find(update.key);
  if (previous != fading_.end()) {
    Release(previous->second.old);
    fading_.erase(previous);
  }
  auto existing = chunks_.find(update.key);
  if (existing != chunks_.end()) {
    if (replaced && existing->second.upload_time < time) {
      fading_[update.key] = {existing->second, time};
    } else {
      Release(existing->second);
    }
    chunks_.erase(existing);
  }
  if (!replaced) {
    return;
  }
  const ChunkMesh& mesh = *update.mesh;
  GpuChunk chunk;
  chunk.bounds = mesh.bounds;
  chunk.lod = mesh.lod;
  chunk.upload_time = time;
  chunk.index_count = static_cast<GLsizei>(mesh.indices.size());
  glGenVertexArrays(1, &chunk.vertex_array);
  glBindVertexArray(chunk.vertex_array);
  glGenBuffers(1, &chunk.vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, chunk.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(TerrainVertex),
               mesh.vertices.data(), GL_STATIC_DRAW);
  const GLsizei stride = sizeof(TerrainVertex);
  glVertexAttribPointer(
      0, 3, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
      reinterpret_cast<void*>(offsetof(TerrainVertex, material)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      3, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride,
      reinterpret_cast<void*>(offsetof(TerrainVertex, sky_visibility)));
  glEnableVertexAttribArray(3);
  glGenBuffers(1, &chunk.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               mesh.indices.size() * sizeof(std::uint32_t), mesh.indices.data(),
               GL_STATIC_DRAW);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  chunks_[update.key] = chunk;
}

bool TerrainRenderer::DrawChunk(const GpuChunk& chunk,
                                const scene::ViewFrustum* frustum) {
  glm::vec3 center = 0.5f * (chunk.bounds.minimum + chunk.bounds.maximum);
  float radius =
      0.5f * glm::length(chunk.bounds.maximum - chunk.bounds.minimum);
  if (frustum && frustum->SphereInFrustum(center, radius) ==
                     scene::ViewFrustum::kOutside) {
    return false;
  }
  glBindVertexArray(chunk.vertex_array);
  glDrawElements(GL_TRIANGLES, chunk.index_count, GL_UNSIGNED_INT, nullptr);
  drawn_triangles_ += static_cast<std::size_t>(chunk.index_count) / 3;
  return true;
}

int TerrainRenderer::Draw(const scene::ViewFrustum* frustum, float time) {
  for (auto it = fading_.begin(); it != fading_.end();) {
    if (time - it->second.start >= kFadeTime || time < it->second.start) {
      Release(it->second.old);
      it = fading_.erase(it);
    } else {
      ++it;
    }
  }
  int drawn = 0;
  drawn_triangles_ = 0;
  shader_->Enable();
  for (auto& entry : chunks_) {
    if (!fading_.contains(entry.first) && DrawChunk(entry.second, frustum)) {
      ++drawn;
    }
  }
  // The fading chunks: the old and the new mesh each on their part of the
  // pixels
  if (!fading_.empty()) {
    dither_shader_->Enable();
    GLint fade_location = dither_shader_->GetUniformLocation("lodFade");
    for (auto& entry : fading_) {
      float fade = (time - entry.second.start) / kFadeTime;
      glUniform1f(fade_location, fade - 1.0f);
      bool visible = DrawChunk(entry.second.old, frustum);
      auto current = chunks_.find(entry.first);
      if (current != chunks_.end()) {
        glUniform1f(fade_location, fade);
        visible = DrawChunk(current->second, frustum) || visible;
      }
      drawn += visible ? 1 : 0;
    }
  }
  glBindVertexArray(0);
  return drawn;
}

void TerrainRenderer::DrawShadow(const render::ShadowCascade& cascade) {
  depth_shader_->Enable();
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, glm::value_ptr(cascade.light_space));
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("model"), 1, GL_FALSE,
                     glm::value_ptr(glm::mat4(1.0f)));
  for (auto& entry : chunks_) {
    const GpuChunk& chunk = entry.second;
    glm::vec3 center = 0.5f * (chunk.bounds.minimum + chunk.bounds.maximum);
    float radius =
        0.5f * glm::length(chunk.bounds.maximum - chunk.bounds.minimum);
    if (!cascade.Contains(center, radius)) {
      continue;
    }
    glBindVertexArray(chunk.vertex_array);
    glDrawElements(GL_TRIANGLES, chunk.index_count, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

}  // namespace bibo::world

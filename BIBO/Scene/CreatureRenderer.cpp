#include "CreatureRenderer.hpp"

#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

namespace bibo::scene {

CreatureRenderer::CreatureRenderer()
    : shader_(std::make_unique<Shader>("Shader/vegetation.vert",
                                       "Shader/vegetation.frag")),
      depth_shader_(std::make_unique<Shader>("Shader/vegetation_depth.vert",
                                             "Shader/depth_shader.frag")),
      parts_(kCreatureModelCount) {
  glGenBuffers(1, &instance_buffer_);
  for (int m = 0; m < kCreatureModelCount; ++m) {
    world::VegetationMesh mesh =
        BuildCreatureModel(static_cast<CreatureModel>(m));
    GpuMesh gpu;
    gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
    gpu.radius = mesh.radius;
    glGenVertexArrays(1, &gpu.vertex_array);
    glBindVertexArray(gpu.vertex_array);
    glGenBuffers(1, &gpu.vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(world::VegetationVertex),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &gpu.index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(std::uint32_t),
                 mesh.indices.data(), GL_STATIC_DRAW);
    // The same vertex layout as the vegetation
    // (Shader/include/vegetation.glsl): binding 0 the vertices, binding 1 the
    // instances
    glBindVertexBuffer(0, gpu.vertex_buffer, 0,
                       sizeof(world::VegetationVertex));
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE,
                         offsetof(world::VegetationVertex, position));
    glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE,
                         offsetof(world::VegetationVertex, normal));
    glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE,
                         offsetof(world::VegetationVertex, uv));
    glVertexAttribFormat(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                         offsetof(world::VegetationVertex, color));
    glVertexAttribFormat(4, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                         offsetof(world::VegetationVertex, parameters));
    for (GLuint attribute = 0; attribute < 5; ++attribute) {
      glVertexAttribBinding(attribute, 0);
      glEnableVertexAttribArray(attribute);
    }
    glBindVertexBuffer(1, instance_buffer_, 0, sizeof(world::Instance));
    glVertexBindingDivisor(1, 1);
    glVertexAttribFormat(5, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, position_scale));
    glVertexAttribFormat(6, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, rotation));
    glVertexAttribFormat(7, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, parameters));
    for (GLuint attribute = 5; attribute < 8; ++attribute) {
      glVertexAttribBinding(attribute, 1);
      glEnableVertexAttribArray(attribute);
    }
    glBindVertexArray(0);
    meshes_.push_back(gpu);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

CreatureRenderer::~CreatureRenderer() {
  for (GpuMesh& mesh : meshes_) {
    glDeleteVertexArrays(1, &mesh.vertex_array);
    GLuint buffers[] = {mesh.vertex_buffer, mesh.index_buffer};
    glDeleteBuffers(2, buffers);
  }
  glDeleteBuffers(1, &instance_buffer_);
}

void CreatureRenderer::Clear() {
  for (std::vector<world::Instance>& list : parts_) {
    list.clear();
  }
  uploaded_ = false;
}

void CreatureRenderer::Add(CreatureModel model, glm::vec3 position,
                           glm::quat rotation, float scale) {
  world::Instance instance;
  instance.position_scale = glm::vec4(position, scale);
  glm::quat q = glm::normalize(rotation);
  instance.rotation = glm::vec4(q.x, q.y, q.z, q.w);
  instance.parameters = glm::vec4(0.0f);  // no tint
  parts_[static_cast<int>(model)].push_back(instance);
  uploaded_ = false;
}

std::size_t CreatureRenderer::GetPartCount() const {
  std::size_t count = 0;
  for (const std::vector<world::Instance>& list : parts_) {
    count += list.size();
  }
  return count;
}

void CreatureRenderer::Upload() {
  if (uploaded_) {
    return;
  }
  staging_.clear();
  firsts_.clear();
  for (const std::vector<world::Instance>& list : parts_) {
    firsts_.push_back(staging_.size());
    staging_.insert(staging_.end(), list.begin(), list.end());
  }
  if (!staging_.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, instance_buffer_);
    // New memory, the graphics card may still read the parts of the last frame
    glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(world::Instance),
                 staging_.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  uploaded_ = true;
}

void CreatureRenderer::DrawBatches(Shader& program) {
  Upload();
  glUniform1f(program.GetUniformLocation("windAmplitude"), 0.0f);
  glUniform2f(program.GetUniformLocation("fadeDistances"), 1e9f, 2e9f);
  for (int m = 0; m < kCreatureModelCount; ++m) {
    if (parts_[m].empty()) {
      continue;
    }
    glBindVertexArray(meshes_[m].vertex_array);
    glBindVertexBuffer(
        1, instance_buffer_,
        static_cast<GLintptr>(firsts_[m] * sizeof(world::Instance)),
        sizeof(world::Instance));
    glDrawElementsInstanced(GL_TRIANGLES, meshes_[m].index_count,
                            GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(parts_[m].size()));
  }
  glBindVertexArray(0);
}

void CreatureRenderer::Draw() {
  if (GetPartCount() == 0) {
    return;
  }
  GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_CULL_FACE);  // the fins are seen from both sides
  shader_->Enable();
  DrawBatches(*shader_);
  cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void CreatureRenderer::DrawShadow(const render::ShadowCascade& cascade) {
  if (GetPartCount() == 0) {
    return;
  }
  depth_shader_->Enable();
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, glm::value_ptr(cascade.light_space));
  DrawBatches(*depth_shader_);
}

}  // namespace bibo::scene

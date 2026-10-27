#include "CreatureRenderer.hpp"

#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

using namespace bibo;
using namespace bibo::scene;

CreatureRenderer::CreatureRenderer()
    : shader(std::make_unique<Shader>("Shader/vegetation.vert",
                                      "Shader/vegetation.frag")),
      depthShader(std::make_unique<Shader>("Shader/vegetation_depth.vert",
                                           "Shader/depth_shader.frag")),
      parts(CREATURE_MODEL_COUNT) {
  glGenBuffers(1, &instanceBuffer);
  for (int m = 0; m < CREATURE_MODEL_COUNT; ++m) {
    world::VegetationMesh mesh =
        buildCreatureModel(static_cast<CreatureModel>(m));
    GpuMesh gpu;
    gpu.indexCount = static_cast<GLsizei>(mesh.indices.size());
    gpu.radius = mesh.radius;
    glGenVertexArrays(1, &gpu.vertexArray);
    glBindVertexArray(gpu.vertexArray);
    glGenBuffers(1, &gpu.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(world::VegetationVertex),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &gpu.indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(std::uint32_t),
                 mesh.indices.data(), GL_STATIC_DRAW);
    // The same vertex layout as the vegetation
    // (Shader/include/vegetation.glsl): binding 0 the vertices, binding 1 the
    // instances
    glBindVertexBuffer(0, gpu.vertexBuffer, 0, sizeof(world::VegetationVertex));
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
    glBindVertexBuffer(1, instanceBuffer, 0, sizeof(world::Instance));
    glVertexBindingDivisor(1, 1);
    glVertexAttribFormat(5, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, positionScale));
    glVertexAttribFormat(6, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, rotation));
    glVertexAttribFormat(7, 4, GL_FLOAT, GL_FALSE,
                         offsetof(world::Instance, parameters));
    for (GLuint attribute = 5; attribute < 8; ++attribute) {
      glVertexAttribBinding(attribute, 1);
      glEnableVertexAttribArray(attribute);
    }
    glBindVertexArray(0);
    meshes.push_back(gpu);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

CreatureRenderer::~CreatureRenderer() {
  for (GpuMesh& mesh : meshes) {
    glDeleteVertexArrays(1, &mesh.vertexArray);
    GLuint buffers[] = {mesh.vertexBuffer, mesh.indexBuffer};
    glDeleteBuffers(2, buffers);
  }
  glDeleteBuffers(1, &instanceBuffer);
}

void CreatureRenderer::clear() {
  for (std::vector<world::Instance>& list : parts) {
    list.clear();
  }
  uploaded = false;
}

void CreatureRenderer::add(CreatureModel model, glm::vec3 position,
                           glm::quat rotation, float scale) {
  world::Instance instance;
  instance.positionScale = glm::vec4(position, scale);
  glm::quat q = glm::normalize(rotation);
  instance.rotation = glm::vec4(q.x, q.y, q.z, q.w);
  instance.parameters = glm::vec4(0.0f);  // no tint
  parts[static_cast<int>(model)].push_back(instance);
  uploaded = false;
}

std::size_t CreatureRenderer::getPartCount() const {
  std::size_t count = 0;
  for (const std::vector<world::Instance>& list : parts) {
    count += list.size();
  }
  return count;
}

void CreatureRenderer::upload() {
  if (uploaded) {
    return;
  }
  staging.clear();
  firsts.clear();
  for (const std::vector<world::Instance>& list : parts) {
    firsts.push_back(staging.size());
    staging.insert(staging.end(), list.begin(), list.end());
  }
  if (!staging.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    // New memory, the graphics card may still read the parts of the last frame
    glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(world::Instance),
                 staging.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  uploaded = true;
}

void CreatureRenderer::drawBatches(Shader& program) {
  upload();
  glUniform1f(program.getUniformLocation("windAmplitude"), 0.0f);
  glUniform2f(program.getUniformLocation("fadeDistances"), 1e9f, 2e9f);
  for (int m = 0; m < CREATURE_MODEL_COUNT; ++m) {
    if (parts[m].empty()) {
      continue;
    }
    glBindVertexArray(meshes[m].vertexArray);
    glBindVertexBuffer(
        1, instanceBuffer,
        static_cast<GLintptr>(firsts[m] * sizeof(world::Instance)),
        sizeof(world::Instance));
    glDrawElementsInstanced(GL_TRIANGLES, meshes[m].indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(parts[m].size()));
  }
  glBindVertexArray(0);
}

void CreatureRenderer::draw() {
  if (getPartCount() == 0) {
    return;
  }
  GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_CULL_FACE);  // the fins are seen from both sides
  shader->enable();
  drawBatches(*shader);
  cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void CreatureRenderer::drawShadow(const render::ShadowCascade& cascade) {
  if (getPartCount() == 0) {
    return;
  }
  depthShader->enable();
  glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, glm::value_ptr(cascade.lightSpace));
  drawBatches(*depthShader);
}

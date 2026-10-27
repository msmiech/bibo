#include "FurryRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

#include "Fur.hpp"

using namespace bibo;
using namespace bibo::scene;

namespace {
// Up to this distance from the camera all strands are drawn (like the bear)
const float FULL_DETAIL_DISTANCE = 24.0f;
const float MIN_DETAIL = 0.12f;
const GLsizei VERTEX_SIZE =
    4 * sizeof(glm::vec4);  // position, normal, bend, comb (like the skin
                            // buffer of the bear)

void setMatrix(Shader* shader, const char* name, const glm::mat4& matrix) {
  glUniformMatrix4fv(shader->getUniformLocation(name), 1, GL_FALSE,
                     glm::value_ptr(matrix));
}

GLuint strandBuffer(const std::vector<fur::Strand>& strands) {
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

FurryRenderer::FurryRenderer(FurryModel furryModel, std::size_t count,
                             const Strands& strands, const Bounds& bounds)
    : model(std::move(furryModel)),
      count(count),
      vertexCount(model.positions.size()),
      strands(strands),
      bounds(bounds),
      skinShader(std::make_unique<Shader>("Shader/bear_skin.vert",
                                          "Shader/bear_skin.frag")),
      furShader(std::make_unique<Shader>("Shader/bear_fur.vert",
                                         "Shader/bear_fur.frag")),
      poses(count),
      centers(count, glm::vec3(0.0f)),
      radii(count, bounds.radius) {
  // Every animal has its block in the skin buffer, which the fur shader reads
  // as a shader storage buffer from its start
  GLint alignment = 256;
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
  alignment = std::max(alignment, 16);
  GLsizeiptr bytes = static_cast<GLsizeiptr>(vertexCount) * VERTEX_SIZE;
  blockSize = (bytes + alignment - 1) / alignment * alignment;
  staging.assign(
      count * static_cast<std::size_t>(blockSize) / sizeof(glm::vec4),
      glm::vec4(0.0f));

  glGenVertexArrays(1, &vertexArray);
  glBindVertexArray(vertexArray);
  glGenBuffers(1, &skinBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, skinBuffer);
  glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glGenBuffers(1, &colorBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
  glBufferData(GL_ARRAY_BUFFER, model.colors.size() * sizeof(glm::vec4),
               model.colors.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &surfaceBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, surfaceBuffer);
  glBufferData(GL_ARRAY_BUFFER, model.surfaces.size() * sizeof(float),
               model.surfaces.data(), GL_STATIC_DRAW);
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
  glBindVertexBuffer(0, skinBuffer, 0, VERTEX_SIZE);
  glBindVertexBuffer(1, colorBuffer, 0, sizeof(glm::vec4));
  glBindVertexBuffer(2, surfaceBuffer, 0, sizeof(float));
  glGenBuffers(1, &indexBuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               model.indices.size() * sizeof(unsigned int),
               model.indices.data(), GL_STATIC_DRAW);
  indexCount = static_cast<GLsizei>(model.indices.size());
  glBindVertexArray(0);

  std::vector<fur::Strand> spines;
  if (strands.spineCount > 0 && !model.spineTriangles.empty()) {
    spines = fur::generateStrands(model.positions, model.colors,
                                  model.spineTriangles, strands.spineCount,
                                  strands.spineSeed);
  }
  std::vector<fur::Strand> fur;
  if (strands.furCount > 0 && !model.furTriangles.empty()) {
    fur =
        fur::generateStrands(model.positions, model.colors, model.furTriangles,
                             strands.furCount, strands.furSeed);
  }
  spineCount = spines.size();
  furCount = fur.size();
  spineBuffer = strandBuffer(spines);
  furBuffer = strandBuffer(fur);
  glGenVertexArrays(1, &strandVertexArray);
}

FurryRenderer::~FurryRenderer() {
  GLuint buffers[] = {skinBuffer,  colorBuffer, surfaceBuffer,
                      indexBuffer, spineBuffer, furBuffer};
  glDeleteBuffers(6, buffers);
  glDeleteVertexArrays(1, &vertexArray);
  glDeleteVertexArrays(1, &strandVertexArray);
}

void FurryRenderer::setPose(std::size_t index,
                            const std::vector<glm::mat4>& parts,
                            float bristle) {
  if (index >= count || parts.empty()) {
    return;
  }
  // The vertices are only moved when the animal is drawn (the scene may be
  // updated more often than drawn, e.g. in the tests)
  Pose& pose = poses[index];
  pose.parts = parts;
  pose.bristle = bristle;
  pose.moved = false;
  radii[index] = bounds.radius * glm::length(glm::vec3(parts[0][0]));
  centers[index] = glm::vec3(parts[0] * glm::vec4(bounds.center, 1.0f));
  uploaded = false;
}

void FurryRenderer::moveVertices(std::size_t index) {
  Pose& pose = poses[index];
  if (pose.moved) {
    return;
  }
  pose.moved = true;
  std::vector<glm::mat3> rotations(pose.parts.begin(), pose.parts.end());
  glm::vec4* block = staging.data() + index *
                                          static_cast<std::size_t>(blockSize) /
                                          sizeof(glm::vec4);
  for (std::size_t v = 0; v < vertexCount; ++v) {
    std::size_t part = static_cast<std::size_t>(model.parts[v]);
    part = part < pose.parts.size() ? part : 0;
    const glm::mat3& rotation = rotations[part];
    glm::vec3 normal = glm::normalize(rotation * model.normals[v]);
    block[v * 4] = pose.parts[part] * glm::vec4(model.positions[v], 1.0f);
    block[v * 4 + 1] = glm::vec4(normal, 0.0f);
    // Spines stand up (e.g. a startled hedgehog)
    block[v * 4 + 2] = glm::vec4(
        model.spines[v] ? normal * (0.9f * pose.bristle) : glm::vec3(0.0f),
        0.0f);
    block[v * 4 + 3] =
        glm::vec4(glm::normalize(rotation * model.combs[v]), 0.0f);
  }
}

void FurryRenderer::upload() {
  if (uploaded) {
    return;
  }
  for (std::size_t i = 0; i < count; ++i) {
    moveVertices(i);
  }
  glBindBuffer(GL_ARRAY_BUFFER, skinBuffer);
  // Orphaning: new memory while the graphics card may still draw the last frame
  glBufferData(GL_ARRAY_BUFFER, staging.size() * sizeof(glm::vec4), nullptr,
               GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, staging.size() * sizeof(glm::vec4),
                  staging.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  uploaded = true;
}

void FurryRenderer::bindVertices(std::size_t index) {
  glBindVertexBuffer(0, skinBuffer, static_cast<GLintptr>(index) * blockSize,
                     VERTEX_SIZE);
}

void FurryRenderer::drawSurfaces(Shader* shader) {
  upload();
  if (shader->getUniformLocation("model") >= 0) {
    setMatrix(shader, "model",
              glm::mat4(1.0f));  // the vertices are in world space
  }
  glBindVertexArray(vertexArray);
  for (std::size_t i = 0; i < count; ++i) {
    bindVertices(i);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

void FurryRenderer::drawShaded(const BearRenderContext& context,
                               const ViewFrustum& frustum) {
  upload();
  std::vector<std::size_t> visible;
  for (std::size_t i = 0; i < count; ++i) {
    if (frustum.sphereInFrustum(centers[i], radii[i]) != ViewFrustum::OUTSIDE) {
      visible.push_back(i);
    }
  }
  drawnCount = static_cast<int>(visible.size());
  if (visible.empty()) {
    return;
  }
  auto setCommonUniforms = [&](Shader* shader) {
    shader->enable();
    setMatrix(shader, "view", context.view);
    setMatrix(shader, "projection", context.projection);
    glUniform3fv(shader->getUniformLocation("worldEye"), 1,
                 glm::value_ptr(context.eye));
  };

  setCommonUniforms(skinShader.get());
  glBindVertexArray(vertexArray);
  for (std::size_t i : visible) {
    bindVertices(i);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
  }

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  float pixelSize = 2.0f / (context.projection[1][1] *
                            static_cast<float>(std::max(viewport[3], 1)));
  setCommonUniforms(furShader.get());
  glUniform1i(furShader->getUniformLocation("segments"),
              BearRenderer::STRAND_SEGMENTS);
  glUniform1f(furShader->getUniformLocation("pixelSize"), pixelSize);
  GLint spinesLocation = furShader->getUniformLocation("spines");
  GLint lengthLocation = furShader->getUniformLocation("maxFurLength");
  GLint widthLocation = furShader->getUniformLocation("strandWidth");
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, colorBuffer);

  GLboolean blend = glIsEnabled(GL_BLEND), cullFace = glIsEnabled(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);  // the ribbons face the camera anyway
  glBindVertexArray(strandVertexArray);
  const GLsizei verticesPerStrand = 2 * (BearRenderer::STRAND_SEGMENTS + 1) + 2;
  for (std::size_t i : visible) {
    // Level of detail: the strands are in random order, the first ones are
    // spread over the whole animal
    float distance = glm::distance(context.eye, centers[i]);
    float detail = std::clamp(
        std::pow(FULL_DETAIL_DISTANCE / std::max(distance, 0.01f), 2.0f),
        MIN_DETAIL, 1.0f);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, skinBuffer,
                      static_cast<GLintptr>(i) * blockSize,
                      static_cast<GLsizeiptr>(vertexCount) * VERTEX_SIZE);
    // The fur first, the spines lie on it
    if (furCount > 0) {
      glUniform1i(spinesLocation, 0);
      glUniform1f(lengthLocation, fur::MAX_LENGTH);
      glUniform1f(widthLocation,
                  BearRenderer::STRAND_WIDTH / std::sqrt(detail));
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, furBuffer);
      std::size_t drawn = std::max<std::size_t>(
          1, static_cast<std::size_t>(static_cast<float>(furCount) * detail));
      glDrawArrays(
          GL_TRIANGLE_STRIP, 0,
          static_cast<GLsizei>(std::min(drawn, furCount)) * verticesPerStrand);
    }
    if (spineCount > 0) {
      glUniform1i(spinesLocation, 1);
      glUniform1f(lengthLocation, strands.spineLength);
      glUniform1f(widthLocation, strands.spineWidth / std::sqrt(detail));
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spineBuffer);
      std::size_t drawn = std::max<std::size_t>(
          1, static_cast<std::size_t>(static_cast<float>(spineCount) * detail));
      glDrawArrays(GL_TRIANGLE_STRIP, 0,
                   static_cast<GLsizei>(std::min(drawn, spineCount)) *
                       verticesPerStrand);
    }
  }
  glUniform1i(spinesLocation, 0);
  glBindVertexArray(0);
  if (!blend) {
    glDisable(GL_BLEND);
  }
  if (cullFace) {
    glEnable(GL_CULL_FACE);
  }
  for (GLuint binding = 0; binding < 3; ++binding) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
  }
}

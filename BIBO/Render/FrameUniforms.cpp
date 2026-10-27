#include "FrameUniforms.hpp"

using namespace bibo::render;

FrameUniformBuffer::FrameUniformBuffer() {
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_UNIFORM_BUFFER, buffer);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

FrameUniformBuffer::~FrameUniformBuffer() { glDeleteBuffers(1, &buffer); }

void FrameUniformBuffer::upload(const FrameUniforms& uniforms) {
  glBindBuffer(GL_UNIFORM_BUFFER, buffer);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &uniforms);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void FrameUniformBuffer::bind() const {
  glBindBufferBase(GL_UNIFORM_BUFFER, BINDING, buffer);
}

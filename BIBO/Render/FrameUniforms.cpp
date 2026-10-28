#include "FrameUniforms.hpp"

namespace bibo::render {

FrameUniformBuffer::FrameUniformBuffer() {
  glGenBuffers(1, &buffer_);
  glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

FrameUniformBuffer::~FrameUniformBuffer() { glDeleteBuffers(1, &buffer_); }

void FrameUniformBuffer::Upload(const FrameUniforms& uniforms) {
  glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &uniforms);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void FrameUniformBuffer::Bind() const {
  glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, buffer_);
}

}  // namespace bibo::render

#include "FrameRecorder.hpp"

#include <GL/glew.h>

using namespace std;
using namespace bibo;

FrameRecorder::FrameRecorder(const string& path, int width, int height)
    : width(width),
      height(height),
      pixels(static_cast<size_t>(width) * height * 3) {
  file = fopen(path.c_str(), "wb");
}

FrameRecorder::~FrameRecorder() {
  if (file) {
    fclose(file);
  }
}

bool FrameRecorder::isOpen() const { return file != nullptr; }

bool FrameRecorder::recordFrame(int width, int height) {
  if (!file || width != this->width || height != this->height) {
    return false;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);  // rows without padding
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  if (fwrite(pixels.data(), 1, pixels.size(), file) != pixels.size()) {
    return false;
  }
  ++frameCount;
  return true;
}

int FrameRecorder::getFrameCount() const { return frameCount; }

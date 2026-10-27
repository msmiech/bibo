#include "FrameRecorder.hpp"

#include <GL/glew.h>

namespace bibo {

using std::string;

FrameRecorder::FrameRecorder(const string& path, int width, int height)
    : width_(width),
      height_(height),
      pixels_(static_cast<size_t>(width) * height * 3) {
  file_ = fopen(path.c_str(), "wb");
}

FrameRecorder::~FrameRecorder() {
  if (file_) {
    fclose(file_);
  }
}

bool FrameRecorder::IsOpen() const { return file_ != nullptr; }

bool FrameRecorder::RecordFrame(int width, int height) {
  if (!file_ || width != this->width_ || height != this->height_) {
    return false;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);  // rows without padding
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels_.data());
  if (fwrite(pixels_.data(), 1, pixels_.size(), file_) != pixels_.size()) {
    return false;
  }
  ++frame_count_;
  return true;
}

int FrameRecorder::GetFrameCount() const { return frame_count_; }

}  // namespace bibo

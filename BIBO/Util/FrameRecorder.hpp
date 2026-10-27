#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace bibo {
/**
 * Writes every frame of the window to a file as raw video (RGB with 8 bits per
 * channel, the rows from bottom to top), e.g. for tools/record_gameplay.sh,
 * which converts it to a GIF with FFmpeg: ffmpeg -f rawvideo -pixel_format
 * rgb24 -video_size <width>x<height> -framerate <fps> -i <file> -vf vflip ...
 */
class FrameRecorder {
 public:
  /**
   * @param width, height Size of the framebuffer in pixels, all frames have
   * this size
   */
  FrameRecorder(const std::string& path, int width, int height);
  ~FrameRecorder();
  FrameRecorder(const FrameRecorder&) = delete;
  FrameRecorder& operator=(const FrameRecorder&) = delete;

  bool isOpen() const;
  /**
   * Reads the back buffer of the default framebuffer, call it before swapping
   * the buffers.
   * @return false if the frame could not be written or the framebuffer has
   * another size than at the start
   */
  bool recordFrame(int width, int height);
  int getFrameCount() const;

 private:
  std::FILE* file = nullptr;
  int width;
  int height;
  int frameCount = 0;
  std::vector<unsigned char> pixels;
};
}  // namespace bibo

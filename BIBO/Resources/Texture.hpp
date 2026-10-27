#ifndef BIBO_RESOURCES_TEXTURE_HPP_
#define BIBO_RESOURCES_TEXTURE_HPP_

#include <GL/glew.h>

#include <memory>
#include <string>
#include <vector>

namespace tex_settings {
extern bool bilinear;  // 0 = NEAREST (neighbor), 1 = (BI)LINEAR
// 0 = Off, 1 = Nearest Neighbor, 2 = Linear
extern unsigned int mip_mapping_quality;
}  // namespace tex_settings

namespace bibo {

class Texture {
  GLuint handle_;
  int texture_unit_;
  void Upload(int width, int height, const unsigned char* rgba_pixels);

 public:
  /**
   * Loads an image file (PNG, JPG, TGA, ...). If the file cannot be loaded, a
   * magenta placeholder is used.
   */
  Texture(const std::string& path, int unit, bool repeat);
  Texture(const std::string& path, int unit);
  /**
   * Creates a texture from RGBA pixels (4 bytes per pixel)
   */
  Texture(int width, int height, const unsigned char* rgba_pixels, int unit,
          bool repeat);
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  /**
   * Black and white checkerboard (e.g. a chequered flag)
   */
  static std::unique_ptr<Texture> CreateCheckerboard(int size, int squares,
                                                     int unit);
  /**
   * Random grey pixels with dark scan lines, looks like the static of an analog
   * TV
   */
  static std::unique_ptr<Texture> CreateNoise(int size, unsigned int seed,
                                              int unit);
  /**
   * RGBA pixels of CreateCheckerboard and CreateNoise (4 bytes per pixel, row
   * by row)
   */
  static std::vector<unsigned char> CheckerboardPixels(int size, int squares);
  static std::vector<unsigned char> NoisePixels(int size, unsigned int seed);

  void Bind(int unit, bool bilinear, unsigned int mip_mapping_quality);
  void Bind(int unit);
  void Bind();
  void Unbind();
  GLuint GetHandle() { return handle_; }
  // Repeated by default: models like the bear have texture coordinates outside
  // of [0, 1]
  bool repeat = true;
};
}  // namespace bibo

#endif  // BIBO_RESOURCES_TEXTURE_HPP_

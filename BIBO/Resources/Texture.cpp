#include "Texture.hpp"

#include <iostream>
#include <random>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

bool tex_settings::bilinear = true;
unsigned int tex_settings::mip_mapping_quality = 2;

namespace bibo {

using std::cerr;
using std::endl;
using std::make_unique;
using std::mt19937;
using std::string;
using std::uniform_int_distribution;
using std::unique_ptr;
using std::vector;

Texture::Texture(const string& path, int unit, bool repeat)
    : Texture(path, unit) {
  this->repeat = repeat;
}

Texture::Texture(const string& path, int unit) : texture_unit_(unit) {
  int width, height, channels;
  // Always converted to RGBA, so that all image formats can be uploaded the
  // same way
  unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
  if (data) {
    Upload(width, height, data);
    stbi_image_free(data);
  } else {
    cerr << "WARNING: Could not load texture " << path << " ("
         << stbi_failure_reason() << "), using a placeholder" << endl;
    const unsigned char magenta[] = {255, 0, 255, 255};
    Upload(1, 1, magenta);
  }
}

Texture::Texture(int width, int height, const unsigned char* rgba_pixels,
                 int unit, bool repeat)
    : texture_unit_(unit) {
  this->repeat = repeat;
  Upload(width, height, rgba_pixels);
}

void Texture::Upload(int width, int height, const unsigned char* rgba_pixels) {
  glGenTextures(1, &handle_);

  glBindTexture(GL_TEXTURE_2D, handle_);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rgba_pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture() { glDeleteTextures(1, &handle_); }

unique_ptr<Texture> Texture::CreateCheckerboard(int size, int squares,
                                                int unit) {
  vector<unsigned char> pixels = CheckerboardPixels(size, squares);
  return make_unique<Texture>(size, size, pixels.data(), unit, true);
}

unique_ptr<Texture> Texture::CreateNoise(int size, unsigned int seed,
                                         int unit) {
  vector<unsigned char> pixels = NoisePixels(size, seed);
  return make_unique<Texture>(size, size, pixels.data(), unit, false);
}

vector<unsigned char> Texture::CheckerboardPixels(int size, int squares) {
  vector<unsigned char> pixels(size * size * 4);
  int square_size = size / squares;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      unsigned char value =
          ((x / square_size + y / square_size) % 2 == 0) ? 240 : 15;
      unsigned char* pixel = &pixels[(y * size + x) * 4];
      pixel[0] = pixel[1] = pixel[2] = value;
      pixel[3] = 255;
    }
  }
  return pixels;
}

vector<unsigned char> Texture::NoisePixels(int size, unsigned int seed) {
  mt19937 random(seed);
  uniform_int_distribution<int> brightness(0, 255);
  vector<unsigned char> pixels(size * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      int value = brightness(random);
      if (y % 2 == 1) {
        value = value * 3 / 4;  // scan lines
      }
      unsigned char* pixel = &pixels[(y * size + x) * 4];
      pixel[0] = pixel[1] = pixel[2] = static_cast<unsigned char>(value);
      pixel[3] = 255;
    }
  }
  return pixels;
}

void Texture::Bind(int unit, bool bilinear, unsigned int mip_mapping_quality) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, handle_);
  if (mip_mapping_quality == 0) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    bilinear ? GL_LINEAR : GL_NEAREST);
  } else {
    if (mip_mapping_quality == 1) {
      glTexParameteri(
          GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
          bilinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST);

    } else if (mip_mapping_quality == 2) {
      glTexParameteri(
          GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
          bilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
    }
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  bilinear ? GL_LINEAR : GL_NEAREST);

  if (repeat) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  } else {
    // GL_CLAMP does not exist in the OpenGL core profile
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

void Texture::Bind(int unit) {
  Bind(unit, tex_settings::bilinear, tex_settings::mip_mapping_quality);
}

void Texture::Bind() { Bind(texture_unit_); }

void Texture::Unbind() { glBindTexture(GL_TEXTURE_2D, 0); }

}  // namespace bibo

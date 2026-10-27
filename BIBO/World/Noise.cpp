#include "Noise.hpp"

#include <cmath>

namespace bibo::world::noise {

namespace {
// Directions of the 3D gradients: the midpoints of the edges of a cube
// (Perlin's improved noise), repeated to 16
const glm::vec3 kGradients3D[16] = {
    {1, 1, 0},  {-1, 1, 0},  {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
    {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1},
    {1, 1, 0},  {-1, 1, 0},  {0, -1, 1}, {0, -1, -1}};

// 16 directions around the circle
glm::vec2 Gradient2(std::uint32_t hash) {
  static const struct Table {
    glm::vec2 directions[16];
    Table() {
      for (int i = 0; i < 16; ++i) {
        float angle = static_cast<float>(i) * 6.28318531f / 16.0f + 0.1963f;
        directions[i] = glm::vec2(std::cos(angle), std::sin(angle));
      }
    }
  } kTable;
  return kTable.directions[hash & 15u];
}

// Quintic interpolation: the noise has continuous first and second derivatives
inline float Fade(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline std::int32_t FloorToInt(float value) {
  return static_cast<std::int32_t>(std::floor(value));
}
}  // namespace

std::uint32_t Hash(std::int32_t x, std::int32_t y, std::uint32_t seed) {
  // Based on the finalizer of MurmurHash3, fed with the coordinates one after
  // the other
  std::uint32_t h = seed * 0x9E3779B9u + 0x7F4A7C15u;
  h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
  h = (h << 13) | (h >> 19);
  h = h * 5u + 0xE6546B64u;
  h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
  h = (h << 13) | (h >> 19);
  h = h * 5u + 0xE6546B64u;
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

std::uint32_t Hash(std::int32_t x, std::int32_t y, std::int32_t z,
                   std::uint32_t seed) {
  return Hash(static_cast<std::int32_t>(Hash(x, y, seed)), z,
              seed ^ 0x51ED270Bu);
}

float HashToUnit(std::uint32_t hash) {
  return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
}

float Gradient(glm::vec2 position, std::uint32_t seed) {
  std::int32_t x0 = FloorToInt(position.x), y0 = FloorToInt(position.y);
  float fx = position.x - static_cast<float>(x0),
        fy = position.y - static_cast<float>(y0);
  float n00 = glm::dot(Gradient2(Hash(x0, y0, seed)), glm::vec2(fx, fy));
  float n10 =
      glm::dot(Gradient2(Hash(x0 + 1, y0, seed)), glm::vec2(fx - 1.0f, fy));
  float n01 =
      glm::dot(Gradient2(Hash(x0, y0 + 1, seed)), glm::vec2(fx, fy - 1.0f));
  float n11 = glm::dot(Gradient2(Hash(x0 + 1, y0 + 1, seed)),
                       glm::vec2(fx - 1.0f, fy - 1.0f));
  float u = Fade(fx), v = Fade(fy);
  // The largest possible value of 2D gradient noise is sqrt(0.5), scaled to
  // about 1
  return 1.41421356f * Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v);
}

float Gradient(glm::vec3 position, std::uint32_t seed) {
  std::int32_t x0 = FloorToInt(position.x), y0 = FloorToInt(position.y),
               z0 = FloorToInt(position.z);
  glm::vec3 f =
      position - glm::vec3(static_cast<float>(x0), static_cast<float>(y0),
                           static_cast<float>(z0));
  float corners[8];
  for (int i = 0; i < 8; ++i) {
    int dx = i & 1, dy = (i >> 1) & 1, dz = (i >> 2) & 1;
    const glm::vec3& g =
        kGradients3D[Hash(x0 + dx, y0 + dy, z0 + dz, seed) & 15u];
    corners[i] = glm::dot(
        g, f - glm::vec3(static_cast<float>(dx), static_cast<float>(dy),
                         static_cast<float>(dz)));
  }
  float u = Fade(f.x), v = Fade(f.y), w = Fade(f.z);
  float x00 = Lerp(corners[0], corners[1], u),
        x10 = Lerp(corners[2], corners[3], u);
  float x01 = Lerp(corners[4], corners[5], u),
        x11 = Lerp(corners[6], corners[7], u);
  return Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w);
}

float Fbm(glm::vec2 position, int octaves, std::uint32_t seed) {
  float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
  // Each octave is rotated a bit, so that the lattices of the octaves do not
  // line up
  const glm::mat2 rotation(0.8f, 0.6f, -0.6f, 0.8f);
  for (int i = 0; i < octaves; ++i) {
    sum += amplitude *
           Gradient(position, seed + static_cast<std::uint32_t>(i) * 1013u);
    total += amplitude;
    position = rotation * position * 2.0f + glm::vec2(17.13f, -9.71f);
    amplitude *= 0.5f;
  }
  return sum / total;
}

float Fbm(glm::vec3 position, int octaves, std::uint32_t seed) {
  float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amplitude *
           Gradient(position, seed + static_cast<std::uint32_t>(i) * 1013u);
    total += amplitude;
    position = position * 2.0f + glm::vec3(17.13f, -9.71f, 5.37f);
    amplitude *= 0.5f;
  }
  return sum / total;
}

float Ridged(glm::vec2 position, int octaves, std::uint32_t seed) {
  float sum = 0.0f, amplitude = 1.0f, total = 0.0f, weight = 1.0f;
  const glm::mat2 rotation(0.8f, 0.6f, -0.6f, 0.8f);
  for (int i = 0; i < octaves; ++i) {
    float ridge =
        1.0f - std::abs(Gradient(position,
                                 seed + static_cast<std::uint32_t>(i) * 7919u));
    ridge *= ridge;
    sum += amplitude * ridge * weight;
    total += amplitude;
    // details mostly on the crests
    weight = glm::clamp(ridge * 1.5f, 0.0f, 1.0f);
    position = rotation * position * 2.0f + glm::vec2(-3.1f, 11.7f);
    amplitude *= 0.5f;
  }
  return sum / total;
}

std::uint64_t noise::Random::Next() {
  std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

float noise::Random::Unit() {
  return static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f);
}

float noise::Random::Range(float minimum, float maximum) {
  return minimum + (maximum - minimum) * Unit();
}

}  // namespace bibo::world::noise

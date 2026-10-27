#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace bibo {
namespace world {
/**
 * Deterministic noise functions for the procedural world: the same seed and
 * position always give the same value (on every computer and in every thread),
 * so the world can be generated in pieces and in any order.
 */
namespace noise {
/**
 * @return A well mixed 32 bit hash of the values
 */
std::uint32_t hash(std::int32_t x, std::int32_t y, std::uint32_t seed);
std::uint32_t hash(std::int32_t x, std::int32_t y, std::int32_t z,
                   std::uint32_t seed);
/**
 * @return A random number in [0, 1) for the values
 */
float hashToUnit(std::uint32_t hash);

/**
 * Gradient noise (Perlin), smooth, about -1 to 1, 0 at the integer lattice
 * points
 */
float gradient(glm::vec2 position, std::uint32_t seed);
float gradient(glm::vec3 position, std::uint32_t seed);

/**
 * Fractal Brownian motion: octaves of gradient noise, each with double the
 * frequency and half the amplitude. The result is normalized to about -1 to 1.
 */
float fbm(glm::vec2 position, int octaves, std::uint32_t seed);
float fbm(glm::vec3 position, int octaves, std::uint32_t seed);
/**
 * Ridged noise: sharp crests where the gradient noise crosses 0 (mountain
 * ridges), about 0 to 1
 */
float ridged(glm::vec2 position, int octaves, std::uint32_t seed);

/**
 * Small, fast random number generator (SplitMix64) for scattering objects
 */
class Random {
 public:
  explicit Random(std::uint64_t seed) : state(seed) {}
  std::uint64_t next();
  /**
   * @return A number in [0, 1)
   */
  float unit();
  /**
   * @return A number in [minimum, maximum)
   */
  float range(float minimum, float maximum);

 private:
  std::uint64_t state;
};
}  // namespace noise
}  // namespace world
}  // namespace bibo

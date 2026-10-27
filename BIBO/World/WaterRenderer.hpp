#pragma once
#include <GL/glew.h>

#include <memory>

#include "../Resources/Shader.hpp"

namespace bibo {
namespace world {
/**
 * The sea around the island (and the puddles on the track, which are at the
 * same level): a plane with simple waves
 */
class WaterRenderer {
 public:
  WaterRenderer();
  ~WaterRenderer();
  WaterRenderer(const WaterRenderer&) = delete;
  WaterRenderer& operator=(const WaterRenderer&) = delete;

  /**
   * @param transparent Blended with what is below (shallow water shows the
   * sand)
   */
  void draw(bool transparent);

 private:
  std::unique_ptr<Shader> shader;
  GLuint vertexArray = 0;
};

/**
 * Snow flakes falling around the camera
 */
class SnowRenderer {
 public:
  /**
   * @param flakes Number of flakes in the box around the camera
   */
  explicit SnowRenderer(int flakes);
  ~SnowRenderer();
  SnowRenderer(const SnowRenderer&) = delete;
  SnowRenderer& operator=(const SnowRenderer&) = delete;

  void draw();

 private:
  std::unique_ptr<Shader> shader;
  GLuint vertexArray = 0;
  int flakes;
};
}  // namespace world
}  // namespace bibo

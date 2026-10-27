#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace bibo {
namespace render {
/**
 * One shadow map of the cascaded shadow maps: an orthographic view from the sun
 * onto a slice of the view frustum (the closer the slice to the camera, the
 * smaller it is and the sharper its shadows)
 */
struct ShadowCascade {
  glm::mat4 lightView = glm::mat4(1.0f);
  glm::mat4 lightProjection = glm::mat4(1.0f);
  glm::mat4 lightSpace = glm::mat4(1.0f);   // projection * view
  float splitNear = 0.0f, splitFar = 0.0f;  // view depth of the slice
  float radius = 0.0f;  // half the width of the map in world units
  float depth = 0.0f;   // from the light to the far end of the map
  float texelSize = 0.0f;

  /**
   * @return false if a sphere cannot cast a shadow into the map (it is outside
   * of the volume of the map)
   */
  bool contains(glm::vec3 center, float sphereRadius) const;
};

/**
 * Cascades for a perspective camera
 * @param view View matrix of the camera
 * @param fieldOfView Vertical, in degrees
 * @param splits View depth where every cascade ends (the first one starts at
 * near)
 * @param toSun Direction towards the sun
 * @param resolution Of a shadow map in texels
 * @param casterDistance How far between the sun and the slice objects still
 * cast shadows into it
 */
std::vector<ShadowCascade> computeCascades(const glm::mat4& view,
                                           float fieldOfView, float aspectRatio,
                                           float nearPlane,
                                           const std::vector<float>& splits,
                                           glm::vec3 toSun, int resolution,
                                           float casterDistance);
}  // namespace render
}  // namespace bibo

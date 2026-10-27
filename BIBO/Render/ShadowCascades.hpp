#ifndef BIBO_RENDER_SHADOWCASCADES_HPP_
#define BIBO_RENDER_SHADOWCASCADES_HPP_
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
  glm::mat4 light_view = glm::mat4(1.0f);
  glm::mat4 light_projection = glm::mat4(1.0f);
  glm::mat4 light_space = glm::mat4(1.0f);    // projection * view
  float split_near = 0.0f, split_far = 0.0f;  // view depth of the slice
  float radius = 0.0f;  // half the width of the map in world units
  float depth = 0.0f;   // from the light to the far end of the map
  float texel_size = 0.0f;

  /**
   * @return false if a sphere cannot cast a shadow into the map (it is outside
   * of the volume of the map)
   */
  bool Contains(glm::vec3 center, float sphere_radius) const;
};

/**
 * Cascades for a perspective camera
 * @param view View matrix of the camera
 * @param field_of_view Vertical, in degrees
 * @param splits View depth where every cascade ends (the first one starts at
 * near)
 * @param to_sun Direction towards the sun
 * @param resolution Of a shadow map in texels
 * @param caster_distance How far between the sun and the slice objects still
 * cast shadows into it
 */
std::vector<ShadowCascade> ComputeCascades(const glm::mat4& view,
                                           float field_of_view,
                                           float aspect_ratio, float near_plane,
                                           const std::vector<float>& splits,
                                           glm::vec3 to_sun, int resolution,
                                           float caster_distance);
}  // namespace render
}  // namespace bibo

#endif  // BIBO_RENDER_SHADOWCASCADES_HPP_

#include "ShadowCascades.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace bibo::render {

bool ShadowCascade::Contains(glm::vec3 center, float sphere_radius) const {
  // The light view is a rigid transformation: distances stay the same
  glm::vec3 local = glm::vec3(light_view * glm::vec4(center, 1.0f));
  return std::abs(local.x) <= radius + sphere_radius &&
         std::abs(local.y) <= radius + sphere_radius &&
         local.z <= sphere_radius && local.z >= -depth - sphere_radius;
}

std::vector<ShadowCascade> ComputeCascades(const glm::mat4& view,
                                           float field_of_view,
                                           float aspect_ratio, float near_plane,
                                           const std::vector<float>& splits,
                                           glm::vec3 to_sun, int resolution,
                                           float caster_distance) {
  std::vector<ShadowCascade> cascades;
  glm::mat4 camera_to_world = glm::inverse(view);
  float tangent = std::tan(glm::radians(field_of_view) * 0.5f);
  to_sun = glm::normalize(to_sun);
  glm::vec3 up = std::abs(to_sun.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                            : glm::vec3(0.0f, 1.0f, 0.0f);
  float start = near_plane;
  for (float end : splits) {
    ShadowCascade cascade;
    cascade.split_near = start;
    cascade.split_far = end;
    // Corners of the slice of the view frustum (the camera looks along -z)
    glm::vec3 corners[8];
    int index = 0;
    for (float depth : {start, end}) {
      float half_height = depth * tangent,
            half_width = half_height * aspect_ratio;
      for (int corner = 0; corner < 4; ++corner) {
        glm::vec3 camera_space((corner & 1 ? 1.0f : -1.0f) * half_width,
                               (corner & 2 ? 1.0f : -1.0f) * half_height,
                               -depth);
        corners[index++] =
            glm::vec3(camera_to_world * glm::vec4(camera_space, 1.0f));
      }
    }
    // Bounding sphere: its size does not change when the camera turns, so the
    // shadows do not shimmer
    glm::vec3 center(0.0f);
    for (const glm::vec3& corner : corners) {
      center += corner / 8.0f;
    }
    float radius = 0.0f;
    for (const glm::vec3& corner : corners) {
      radius = std::max(radius, glm::distance(center, corner));
    }
    radius = std::ceil(radius * 2.0f) * 0.5f;

    cascade.radius = radius;
    cascade.depth = 2.0f * radius + caster_distance;
    glm::vec3 eye = center + to_sun * (radius + caster_distance);
    cascade.light_view = glm::lookAt(eye, center, up);
    cascade.light_projection =
        glm::ortho(-radius, radius, -radius, radius, 0.0f, cascade.depth);
    // The map only moves by whole texels: the shadows do not shimmer when the
    // camera moves
    glm::mat4 light_space = cascade.light_projection * cascade.light_view;
    glm::vec4 origin = light_space * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec2 texels =
        glm::vec2(origin) * (static_cast<float>(resolution) * 0.5f);
    glm::vec2 offset =
        (glm::round(texels) - texels) * (2.0f / static_cast<float>(resolution));
    cascade.light_projection[3][0] += offset.x;
    cascade.light_projection[3][1] += offset.y;
    cascade.light_space = cascade.light_projection * cascade.light_view;
    cascade.texel_size = 2.0f * radius / static_cast<float>(resolution);
    cascades.push_back(cascade);
    start = end;
  }
  return cascades;
}

}  // namespace bibo::render

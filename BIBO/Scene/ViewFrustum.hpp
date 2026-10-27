#ifndef BIBO_SCENE_VIEWFRUSTUM_HPP_
#define BIBO_SCENE_VIEWFRUSTUM_HPP_
#include <glm/glm.hpp>

#include "../Resources/Plane.hpp"

namespace bibo {
namespace scene {
/**
 * The volume a perspective camera sees, bounded by 6 planes whose normals point
 * inwards. Code based on:
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-implementation/
 **/
class ViewFrustum {
 public:
  enum Result { kOutside, kIntersect, kInside };

  /**
   * @param field_of_view Vertical opening angle in degrees
   * @param aspect_ratio Width divided by height
   */
  void SetCamInternals(float field_of_view, float aspect_ratio,
                       float near_distance, float far_distance);
  /**
   * @param position Position of the camera
   * @param lookAt Any point in the viewing direction
   * @param up Up direction of the camera
   */
  void SetCamDef(const glm::vec3& position, const glm::vec3& look_at,
                 const glm::vec3& up);

  Result PointInFrustum(const glm::vec3& point) const;
  Result SphereInFrustum(const glm::vec3& center, float radius) const;

 private:
  enum { kTop = 0, kBottom, kLeft, kRight, kNearp, kFarp, kPlaneCount };
  resources::Plane planes_[kPlaneCount];

  float near_distance_ = 0.1f, far_distance_ = 100.0f;
  // Half width and height of the near and far plane
  float near_width_ = 0.0f, near_height_ = 0.0f, far_width_ = 0.0f,
        far_height_ = 0.0f;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_VIEWFRUSTUM_HPP_

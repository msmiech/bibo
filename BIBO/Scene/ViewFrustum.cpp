#include "ViewFrustum.hpp"

#include <cmath>

namespace bibo::scene {

using glm::radians;
using glm::vec3;

void ViewFrustum::SetCamInternals(float field_of_view, float aspect_ratio,
                                  float near_distance, float far_distance) {
  this->near_distance_ = near_distance;
  this->far_distance_ = far_distance;

  float tangent = std::tan(radians(field_of_view) * 0.5f);
  near_height_ = near_distance * tangent;
  near_width_ = near_height_ * aspect_ratio;
  far_height_ = far_distance * tangent;
  far_width_ = far_height_ * aspect_ratio;
}

void ViewFrustum::SetCamDef(const vec3& position, const vec3& look_at,
                            const vec3& up) {
  // Axes of the camera; the camera looks along -z
  vec3 z = normalize(position - look_at);
  vec3 x = normalize(cross(up, z));
  vec3 y = cross(z, x);

  vec3 near_center = position - z * near_distance_;
  vec3 far_center = position - z * far_distance_;

  vec3 near_top_left = near_center + y * near_height_ - x * near_width_;
  vec3 near_top_right = near_center + y * near_height_ + x * near_width_;
  vec3 near_bottom_left = near_center - y * near_height_ - x * near_width_;
  vec3 near_bottom_right = near_center - y * near_height_ + x * near_width_;

  vec3 far_top_left = far_center + y * far_height_ - x * far_width_;
  vec3 far_top_right = far_center + y * far_height_ + x * far_width_;
  vec3 far_bottom_left = far_center - y * far_height_ - x * far_width_;
  vec3 far_bottom_right = far_center - y * far_height_ + x * far_width_;

  // Counter-clockwise seen from inside of the frustum, so that the normals
  // point inwards
  planes_[kTop].Set3Points(near_top_right, near_top_left, far_top_left);
  planes_[kBottom].Set3Points(near_bottom_left, near_bottom_right,
                              far_bottom_right);
  planes_[kLeft].Set3Points(near_top_left, near_bottom_left, far_bottom_left);
  planes_[kRight].Set3Points(near_bottom_right, near_top_right,
                             far_bottom_right);
  planes_[kNearp].Set3Points(near_top_left, near_top_right, near_bottom_right);
  planes_[kFarp].Set3Points(far_top_right, far_top_left, far_bottom_left);
}

ViewFrustum::Result ViewFrustum::PointInFrustum(const vec3& point) const {
  for (const resources::Plane& plane : planes_) {
    if (plane.Distance(point) < 0.0f) {
      return kOutside;
    }
  }
  return kInside;
}

ViewFrustum::Result ViewFrustum::SphereInFrustum(const vec3& center,
                                                 float radius) const {
  Result result = kInside;
  for (const resources::Plane& plane : planes_) {
    float distance = plane.Distance(center);
    if (distance < -radius) {
      return kOutside;
    }
    if (distance < radius) {
      result = kIntersect;
    }
  }
  return result;
}

}  // namespace bibo::scene

#include "ViewFrustum.hpp"

#include <cmath>

using namespace bibo::scene;
using namespace glm;

void ViewFrustum::setCamInternals(float fieldOfView, float aspectRatio,
                                  float nearDistance, float farDistance) {
  this->nearDistance = nearDistance;
  this->farDistance = farDistance;

  float tangent = std::tan(radians(fieldOfView) * 0.5f);
  nearHeight = nearDistance * tangent;
  nearWidth = nearHeight * aspectRatio;
  farHeight = farDistance * tangent;
  farWidth = farHeight * aspectRatio;
}

void ViewFrustum::setCamDef(const vec3& position, const vec3& lookAt,
                            const vec3& up) {
  // Axes of the camera; the camera looks along -z
  vec3 z = normalize(position - lookAt);
  vec3 x = normalize(cross(up, z));
  vec3 y = cross(z, x);

  vec3 nearCenter = position - z * nearDistance;
  vec3 farCenter = position - z * farDistance;

  vec3 nearTopLeft = nearCenter + y * nearHeight - x * nearWidth;
  vec3 nearTopRight = nearCenter + y * nearHeight + x * nearWidth;
  vec3 nearBottomLeft = nearCenter - y * nearHeight - x * nearWidth;
  vec3 nearBottomRight = nearCenter - y * nearHeight + x * nearWidth;

  vec3 farTopLeft = farCenter + y * farHeight - x * farWidth;
  vec3 farTopRight = farCenter + y * farHeight + x * farWidth;
  vec3 farBottomLeft = farCenter - y * farHeight - x * farWidth;
  vec3 farBottomRight = farCenter - y * farHeight + x * farWidth;

  // Counter-clockwise seen from inside of the frustum, so that the normals
  // point inwards
  planes[TOP].set3Points(nearTopRight, nearTopLeft, farTopLeft);
  planes[BOTTOM].set3Points(nearBottomLeft, nearBottomRight, farBottomRight);
  planes[LEFT].set3Points(nearTopLeft, nearBottomLeft, farBottomLeft);
  planes[RIGHT].set3Points(nearBottomRight, nearTopRight, farBottomRight);
  planes[NEARP].set3Points(nearTopLeft, nearTopRight, nearBottomRight);
  planes[FARP].set3Points(farTopRight, farTopLeft, farBottomLeft);
}

ViewFrustum::Result ViewFrustum::pointInFrustum(const vec3& point) const {
  for (const resources::Plane& plane : planes) {
    if (plane.distance(point) < 0.0f) {
      return OUTSIDE;
    }
  }
  return INSIDE;
}

ViewFrustum::Result ViewFrustum::sphereInFrustum(const vec3& center,
                                                 float radius) const {
  Result result = INSIDE;
  for (const resources::Plane& plane : planes) {
    float distance = plane.distance(center);
    if (distance < -radius) {
      return OUTSIDE;
    }
    if (distance < radius) {
      result = INTERSECT;
    }
  }
  return result;
}

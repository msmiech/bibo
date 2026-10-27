#pragma once
#include <glm/glm.hpp>

namespace bibo {
namespace resources {
/**
 * Plane in Hessian normal form: dot(normal, p) + d = 0
 * Code based on:
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-implementation/
 **/
class Plane {
 public:
  glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);  // unit length
  float d = 0.0f;

  Plane() = default;
  Plane(const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3);

  /**
   * Plane through three points, which are counter-clockwise when looking at the
   * plane from the side the normal points to
   */
  void set3Points(const glm::vec3& v1, const glm::vec3& v2,
                  const glm::vec3& v3);
  void setNormalAndPoint(const glm::vec3& normal, const glm::vec3& point);
  /**
   * Plane a * x + b * y + c * z + d = 0
   */
  void setCoefficients(float a, float b, float c, float d);
  /**
   * @return Signed distance of the point: positive on the side the normal
   * points to
   */
  float distance(const glm::vec3& p) const;
};
}  // namespace resources
}  // namespace bibo

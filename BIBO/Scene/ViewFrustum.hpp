#pragma once
#include <glm/glm.hpp>
#include "../Resources/Plane.hpp"

namespace bibo
{
	namespace scene
	{
		/**
		* The volume a perspective camera sees, bounded by 6 planes whose normals point inwards.
		* Code based on: http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-implementation/
		**/
		class ViewFrustum
		{
		public:
			enum Result { OUTSIDE, INTERSECT, INSIDE };

			/**
			* @param fieldOfView Vertical opening angle in degrees
			* @param aspectRatio Width divided by height
			*/
			void setCamInternals(float fieldOfView, float aspectRatio, float nearDistance, float farDistance);
			/**
			* @param position Position of the camera
			* @param lookAt Any point in the viewing direction
			* @param up Up direction of the camera
			*/
			void setCamDef(const glm::vec3& position, const glm::vec3& lookAt, const glm::vec3& up);

			Result pointInFrustum(const glm::vec3& point) const;
			Result sphereInFrustum(const glm::vec3& center, float radius) const;

		private:
			enum { TOP = 0, BOTTOM, LEFT, RIGHT, NEARP, FARP, PLANE_COUNT };
			resources::Plane planes[PLANE_COUNT];

			float nearDistance = 0.1f, farDistance = 100.0f;
			//Half width and height of the near and far plane
			float nearWidth = 0.0f, nearHeight = 0.0f, farWidth = 0.0f, farHeight = 0.0f;
		};
	}
}

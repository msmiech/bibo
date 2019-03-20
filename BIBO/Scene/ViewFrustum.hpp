#pragma once
#include <glm/detail/type_vec3.hpp>
#include "../Resources/Plane.hpp"

namespace CPM_GLM_AABB_NS {
	class AABB;
}

using namespace glm;

namespace bibo
{
	namespace scene
	{
		/**
		* Code based on: http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-implementation/
		**/
		class FrustumG {

			enum {
				TOP = 0, BOTTOM, LEFT,
				RIGHT, NEARP, FARP
			};

		public:

			static enum { OUTSIDE, INTERSECT, INSIDE };

			Plane pl[6];

			vec3 ntl, ntr, nbl, nbr, ftl, ftr, fbl, fbr;
			float nearD, farD, ratio, angle, tang;
			float nw, nh, fw, fh;

			FrustumG::FrustumG();
			FrustumG::~FrustumG();

			void setCamInternals(float angle, float ratio, float nearD, float farD);
			void setCamDef(vec3 &p, vec3 &l, vec3 &u);
			int pointInFrustum(vec3 &p);
			int sphereInFrustum(vec3 &p, float raio);
			int boxInFrustum(CPM_GLM_AABB_NS::AABB &b);

		};

	}
}

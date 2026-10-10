#ifndef IAUNS_GLM_AABB_HPP
#define IAUNS_GLM_AABB_HPP

#include <glm/glm.hpp>

/**
* Copyright http://iauns.github.io/cpm-glm-aabb
**/

using namespace glm;

namespace CPM_GLM_AABB_NS {
	class AABB
	{
	public:
		AABB();

		AABB(const vec3& center, float radius);

		AABB(const vec3& p1, const vec3& p2);

		AABB(const AABB& aabb);
		~AABB();

		void setNull()      { mMin = vec3(1.0f); mMax = vec3(-1.0f); }

		bool isNull() const { return mMin.x > mMax.x || mMin.y > mMax.y || mMin.z > mMax.z; }

		void extend(float val);

		void extend(const vec3& p);

		void extend(const vec3& center, float radius);

		void extend(const AABB& aabb);

		void extendDisk(const vec3& center, const vec3& normal,
			float radius);

		void translate(const vec3& v);

		void scale(const vec3& scale, const vec3& origin);

		vec3 getCenter() const;

		vec3 getDiagonal() const;

		float getLongestEdge() const;

		float getShortestEdge() const;

		vec3 getMin() const { return mMin; }

		vec3 getMax() const { return mMax; }

		bool overlaps(const AABB& bb) const;

		enum INTERSECTION_TYPE { INSIDE, INTERSECT, OUTSIDE };
		INTERSECTION_TYPE intersect(const AABB& bb) const;

		bool isSimilarTo(const AABB& b, float diff = 0.5) const;

	private:

		vec3 mMin;
		vec3 mMax;
	};


}

#endif
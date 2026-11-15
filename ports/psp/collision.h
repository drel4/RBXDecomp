#pragma once
#include "math3d.h"

namespace openrbx {
struct Aabb
{
	Vec3 low, high;
	bool overlaps(const Aabb& other, float margin = 0) const;
	static Aabb combine(const Aabb& a, const Aabb& b);
};
struct CollisionShape
{
	CFrame frame;
	Vec3 size;
	// Legacy Part shape: block=1; ball=0 and cylinder=2 both use the engine's
	// Ball primitive (radius=size.x/2). Wedge=3 is the existing PSP extension.
	int type = 1;
	Aabb bounds() const;
};
struct Contact
{
	Vec3 normal, point; // normal points from the second shape toward the first
	float depth = 0;
};
bool intersect(const CollisionShape& a, const CollisionShape& b, Contact& contact, float margin = 0);
bool surfaceJoint(const CollisionShape& a, const CollisionShape& b, const int surfaceA[6], const int surfaceB[6]);
} // namespace openrbx

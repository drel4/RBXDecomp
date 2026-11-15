#include "collision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openrbx {
static float component(Vec3 v, int axis)
{
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
static Vec3 axis(const CFrame& f, int i)
{
	return Vec3(f.rotation[i], f.rotation[i + 3], f.rotation[i + 6]);
}
bool Aabb::overlaps(const Aabb& b, float e) const
{
	return low.x <= b.high.x + e && high.x + e >= b.low.x && low.y <= b.high.y + e && high.y + e >= b.low.y &&
		   low.z <= b.high.z + e && high.z + e >= b.low.z;
}
Aabb Aabb::combine(const Aabb& a, const Aabb& b)
{
	return {
		Vec3(std::min(a.low.x, b.low.x), std::min(a.low.y, b.low.y), std::min(a.low.z, b.low.z)),
		Vec3(std::max(a.high.x, b.high.x), std::max(a.high.y, b.high.y), std::max(a.high.z, b.high.z))
	};
}
Aabb CollisionShape::bounds() const
{
	Vec3 e;
	if (type == 0 || type == 2) {
		e = Vec3(size.x * .5f, size.x * .5f, size.x * .5f);
	}
	else {
		e = Vec3(
				std::fabs(frame.rotation[0]) * size.x + std::fabs(frame.rotation[1]) * size.y +
					std::fabs(frame.rotation[2]) * size.z,
				std::fabs(frame.rotation[3]) * size.x + std::fabs(frame.rotation[4]) * size.y +
					std::fabs(frame.rotation[5]) * size.z,
				std::fabs(frame.rotation[6]) * size.x + std::fabs(frame.rotation[7]) * size.y +
					std::fabs(frame.rotation[8]) * size.z
			) *
			.5f;
	}
	return {frame.position - e, frame.position + e};
}
static float radius(const CollisionShape& s, Vec3 v)
{
	return (std::fabs(dot(axis(s.frame, 0), v)) * s.size.x + std::fabs(dot(axis(s.frame, 1), v)) * s.size.y +
			std::fabs(dot(axis(s.frame, 2), v)) * s.size.z) *
		   .5f;
}
static int corners(const CollisionShape& s, Vec3* out)
{
	int count = 0;
	for (int z = -1; z <= 1; z += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int x = -1; x <= 1; x += 2) {
				if (s.type == 3 && z == -1 && y == 1) {
					continue;
				}
				out[count++] = s.frame.pointToWorld(Vec3(x * s.size.x, y * s.size.y, z * s.size.z) * .5f);
			}
		}
	}
	return count;
}
static Vec3 support(const CollisionShape& s, Vec3 direction)
{
	Vec3 points[8];
	int count = corners(s, points);
	Vec3 best = points[0];
	float value = dot(best, direction);
	for (int i = 1; i < count; ++i) {
		if (dot(points[i], direction) > value) {
			best = points[i];
			value = dot(best, direction);
		}
	}
	return best;
}
static bool sphereBox(const CollisionShape& sphere, const CollisionShape& box, Contact& contact, float margin)
{
	Vec3 p = box.frame.pointToObject(sphere.frame.position), h = box.size * .5f;
	Vec3 q(std::max(-h.x, std::min(h.x, p.x)), std::max(-h.y, std::min(h.y, p.y)), std::max(-h.z, std::min(h.z, p.z)));
	Vec3 delta = p - q;
	float d = length(delta), r = sphere.size.x * .5f;
	if (d > r + margin) {
		return false;
	}
	if (d > 1e-7f) {
		contact.normal = box.frame.vectorToWorld(delta * (1 / d));
		contact.depth = r - d;
	}
	else {
		float distances[] = {h.x - std::fabs(p.x), h.y - std::fabs(p.y), h.z - std::fabs(p.z)};
		int n = (distances[0] < distances[1] ? 0 : 1);
		if (distances[2] < distances[n]) {
			n = 2;
		}
		contact.normal = axis(box.frame, n) * (component(p, n) >= 0 ? 1.f : -1.f);
		contact.depth = r + distances[n];
	}
	contact.point = sphere.frame.position - contact.normal * r;
	return true;
}
// Closest-point triangle calculation, used for sphere/wedge edge and vertex axes.
static Vec3 closestTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
	Vec3 ab = b - a, ac = c - a, ap = p - a;
	float d1 = dot(ab, ap), d2 = dot(ac, ap);
	if (d1 <= 0 && d2 <= 0) {
		return a;
	}
	Vec3 bp = p - b;
	float d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) {
		return b;
	}
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) {
		return a + ab * (d1 / (d1 - d3));
	}
	Vec3 cp = p - c;
	float d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) {
		return c;
	}
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) {
		return a + ac * (d2 / (d2 - d6));
	}
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
		return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	}
	float inverse = 1 / (va + vb + vc);
	return a + ab * (vb * inverse) + ac * (vc * inverse);
}
static bool sphereWedge(const CollisionShape& sphere, const CollisionShape& wedge, Contact& c, float margin)
{
	Vec3 v[8];
	corners(wedge, v);
	// corners(): front bottom 0/1, back bottom 2/3, back top 4/5.
	static const int triangles[][3] = {
		{0, 2, 1}, {1, 2, 3}, {2, 4, 5}, {2, 5, 3}, {0, 4, 2}, {1, 3, 5}, {0, 1, 5}, {0, 5, 4}
	};
	Vec3 best;
	float distance = std::numeric_limits<float>::max();
	for (const auto& t : triangles) {
		Vec3 q = closestTriangle(sphere.frame.position, v[t[0]], v[t[1]], v[t[2]]);
		float d = dot(q - sphere.frame.position, q - sphere.frame.position);
		if (d < distance) {
			distance = d;
			best = q;
		}
	}
	Vec3 p = wedge.frame.pointToObject(sphere.frame.position), h = wedge.size * .5f;
	bool inside =
		std::fabs(p.x) <= h.x && p.y >= -h.y && p.z <= h.z && p.z >= -h.z && p.y <= p.z * (wedge.size.y / wedge.size.z);
	float d = std::sqrt(distance), r = sphere.size.x * .5f;
	if (!inside && d > r + margin) {
		return false;
	}
	c.normal = d > 1e-7f ? (sphere.frame.position - best) * ((inside ? -1.f : 1.f) / d) : Vec3(0, 1, 0);
	c.depth = inside ? r + d : r - d;
	c.point = best;
	return true;
}
bool intersect(const CollisionShape& a, const CollisionShape& b, Contact& c, float margin)
{
	if (!a.bounds().overlaps(b.bounds(), margin)) {
		return false;
	}
	bool sa = a.type == 0 || a.type == 2, sb = b.type == 0 || b.type == 2;
	if (sa && sb) {
		Vec3 delta = a.frame.position - b.frame.position;
		float d = length(delta), r = (a.size.x + b.size.x) * .5f;
		if (d > r + margin) {
			return false;
		}
		c.normal = d > 1e-7f ? delta * (1 / d) : Vec3(0, 1, 0);
		c.depth = r - d;
		c.point = a.frame.position - c.normal * (a.size.x * .5f);
		return true;
	}
	if (sa) {
		return b.type == 3 ? sphereWedge(a, b, c, margin) : sphereBox(a, b, c, margin);
	}
	if (sb) {
		bool hit = a.type == 3 ? sphereWedge(b, a, c, margin) : sphereBox(b, a, c, margin);
		c.normal = c.normal * -1;
		return hit;
	}
	Vec3 av[8], bv[8];
	int na = 0, nb = 0;
	bool wedge = a.type == 3 || b.type == 3;
	if (wedge) {
		na = corners(a, av);
		nb = corners(b, bv);
	}
	Vec3 edgesA[4] = {
		axis(a.frame, 0), axis(a.frame, 1), axis(a.frame, 2), a.frame.vectorToWorld(Vec3(0, a.size.y, a.size.z))
	};
	Vec3 edgesB[4] = {
		axis(b.frame, 0), axis(b.frame, 1), axis(b.frame, 2), b.frame.vectorToWorld(Vec3(0, b.size.y, b.size.z))
	};
	c.depth = std::numeric_limits<float>::max();
	auto test = [&](Vec3 normal) -> bool {
		float l = length(normal);
		if (l < 1e-6f) {
			return true;
		}
		normal = normal * (1 / l);
		float move;
		if (!wedge) {
			float signedDistance = dot(a.frame.position - b.frame.position, normal);
			move = radius(a, normal) + radius(b, normal) - std::fabs(signedDistance);
			if (signedDistance < 0) {
				normal = normal * -1;
			}
		}
		else {
			float amin = dot(av[0], normal), amax = amin, bmin = dot(bv[0], normal), bmax = bmin;
			for (int i = 1; i < na; ++i) {
				float p = dot(av[i], normal);
				amin = std::min(amin, p);
				amax = std::max(amax, p);
			}
			for (int i = 1; i < nb; ++i) {
				float p = dot(bv[i], normal);
				bmin = std::min(bmin, p);
				bmax = std::max(bmax, p);
			}
			float positive = bmax - amin, negative = amax - bmin;
			move = std::min(positive, negative);
			if (negative < positive) {
				normal = normal * -1;
			}
		}
		if (move < -margin) {
			return false;
		}
		if (move < c.depth) {
			c.depth = move;
			c.normal = normal;
		}
		return true;
	};
	for (int i = 0; i < 3; ++i) {
		if (!test(edgesA[i]) || !test(edgesB[i])) {
			return false;
		}
	}
	if (a.type == 3 && !test(a.frame.vectorToWorld(Vec3(0, a.size.z, -a.size.y)))) {
		return false;
	}
	if (b.type == 3 && !test(b.frame.vectorToWorld(Vec3(0, b.size.z, -b.size.y)))) {
		return false;
	}
	for (int i = 0; i < (a.type == 3 ? 4 : 3); ++i) {
		for (int j = 0; j < (b.type == 3 ? 4 : 3); ++j) {
			if (!test(cross(edgesA[i], edgesB[j]))) {
				return false;
			}
		}
	}
	c.point = (support(a, c.normal * -1) + support(b, c.normal)) * .5f;
	return true;
}
bool surfaceJoint(const CollisionShape& a, const CollisionShape& b, const int sa[6], const int sb[6])
{
	if (a.type != 1 || b.type != 1 || !a.bounds().overlaps(b.bounds(), .05f)) {
		return false;
	}
	for (int i = 0; i < 3; ++i) {
		float alignment = 0;
		for (int j = 0; j < 3; ++j) {
			alignment = std::max(alignment, std::fabs(dot(axis(a.frame, i), axis(b.frame, j))));
		}
		if (alignment < .99875f) {
			return false;
		}
	}
	// JointBuilder/Joint/SnapJoint/WeldJoint/GlueJoint: opposed, axis-aligned
	// face planes, >=0.35-stud overlap, 0.01 tight / 0.05 loose plane tolerance.
	for (int i = 0; i < 6; ++i) {
		Vec3 normal = axis(a.frame, i % 3) * (i < 3 ? 1.f : -1.f);
		for (int j = 0; j < 6; ++j) {
			bool glue = sa[i] == 1 || sb[j] == 1;
			if (!glue && sa[i] != 2 && sb[j] != 2 && !((sa[i] == 3 && sb[j] == 4) || (sa[i] == 4 && sb[j] == 3))) {
				continue;
			}
			Vec3 other = axis(b.frame, j % 3) * (j < 3 ? 1.f : -1.f);
			if (dot(normal, other) > -.99995f) {
				continue;
			}
			Vec3 centerA = a.frame.position + normal * (component(a.size, i % 3) * .5f),
				 centerB = b.frame.position + other * (component(b.size, j % 3) * .5f);
			if (std::fabs(dot(centerB - centerA, normal)) > (glue ? .05f : .01f)) {
				continue;
			}
			bool overlap = true;
			for (int k = 1; k <= 2; ++k) {
				int tangent = (i % 3 + k) % 3;
				Vec3 direction = axis(a.frame, tangent);
				float h = component(a.size, tangent) * .5f, offset = dot(centerB - centerA, direction),
					  r = radius(b, direction);
				if (std::min(h, offset + r) - std::max(-h, offset - r) < .35f) {
					overlap = false;
				}
			}
			if (overlap) {
				return true;
			}
		}
	}
	return false;
}
} // namespace openrbx

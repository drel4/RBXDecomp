#pragma once

namespace openrbx {
struct Vec3
{
	float x, y, z;
	Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
	Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
	bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
};
float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
float length(const Vec3& v);
Vec3 normalized(const Vec3& v);

// Row-major G3D-compatible rigid transform; local forward is negative Z.
struct CFrame
{
	Vec3 position;
	float rotation[9];
	CFrame(Vec3 position = Vec3());
	Vec3 vectorToWorld(Vec3 v) const;
	Vec3 vectorToObject(Vec3 v) const;
	Vec3 pointToWorld(Vec3 v) const { return position + vectorToWorld(v); }
	Vec3 pointToObject(Vec3 v) const { return vectorToObject(v - position); }
	CFrame inverse() const;
	CFrame operator*(const CFrame& other) const;
	bool operator==(const CFrame& other) const;
	static CFrame angles(float x, float y, float z);
	static CFrame axisAngle(Vec3 axis, float angle);
	static CFrame quaternion(Vec3 position, float x, float y, float z, float w);
	static CFrame lookAt(Vec3 position, Vec3 target);
};
} // namespace openrbx

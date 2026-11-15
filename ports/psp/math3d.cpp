#include "math3d.h"

#include <algorithm>
#include <cmath>

namespace openrbx {
float dot(const Vec3& a, const Vec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3& a, const Vec3& b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float length(const Vec3& v)
{
	return std::sqrt(dot(v, v));
}
Vec3 normalized(const Vec3& v)
{
	float l = length(v);
	return l > 0.00001f ? v * (1 / l) : Vec3(0, 1, 0);
}
CFrame::CFrame(Vec3 p) : position(p)
{
	for (int i = 0; i < 9; ++i) {
		rotation[i] = i % 4 == 0 ? 1.f : 0.f;
	}
}
Vec3 CFrame::vectorToWorld(Vec3 v) const
{
	return Vec3(
		rotation[0] * v.x + rotation[1] * v.y + rotation[2] * v.z,
		rotation[3] * v.x + rotation[4] * v.y + rotation[5] * v.z,
		rotation[6] * v.x + rotation[7] * v.y + rotation[8] * v.z
	);
}
Vec3 CFrame::vectorToObject(Vec3 v) const
{
	return Vec3(
		rotation[0] * v.x + rotation[3] * v.y + rotation[6] * v.z,
		rotation[1] * v.x + rotation[4] * v.y + rotation[7] * v.z,
		rotation[2] * v.x + rotation[5] * v.y + rotation[8] * v.z
	);
}
CFrame CFrame::inverse() const
{
	CFrame result(vectorToObject(position) * -1);
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			result.rotation[r * 3 + c] = rotation[c * 3 + r];
		}
	}
	return result;
}
CFrame CFrame::operator*(const CFrame& other) const
{
	CFrame result(pointToWorld(other.position));
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			result.rotation[r * 3 + c] = 0;
			for (int k = 0; k < 3; ++k) {
				result.rotation[r * 3 + c] += rotation[r * 3 + k] * other.rotation[k * 3 + c];
			}
		}
	}
	return result;
}
bool CFrame::operator==(const CFrame& other) const
{
	if (!(position == other.position)) {
		return false;
	}
	for (int i = 0; i < 9; ++i) {
		if (rotation[i] != other.rotation[i]) {
			return false;
		}
	}
	return true;
}
CFrame CFrame::axisAngle(Vec3 axis, float angle)
{
	if (length(axis) < 1e-8f) {
		return CFrame();
	}
	axis = normalized(axis);
	float s = std::sin(angle), c = std::cos(angle), t = 1 - c;
	CFrame f;
	f.rotation[0] = t * axis.x * axis.x + c;
	f.rotation[1] = t * axis.x * axis.y - s * axis.z;
	f.rotation[2] = t * axis.x * axis.z + s * axis.y;
	f.rotation[3] = t * axis.x * axis.y + s * axis.z;
	f.rotation[4] = t * axis.y * axis.y + c;
	f.rotation[5] = t * axis.y * axis.z - s * axis.x;
	f.rotation[6] = t * axis.x * axis.z - s * axis.y;
	f.rotation[7] = t * axis.y * axis.z + s * axis.x;
	f.rotation[8] = t * axis.z * axis.z + c;
	return f;
}
CFrame CFrame::angles(float x, float y, float z)
{
	// G3D Matrix3::fromEulerAnglesXYZ: X * (Y * Z), not reversed.
	return axisAngle(Vec3(1, 0, 0), x) * (axisAngle(Vec3(0, 1, 0), y) * axisAngle(Vec3(0, 0, 1), z));
}
CFrame CFrame::quaternion(Vec3 position, float x, float y, float z, float w)
{
	float n = std::sqrt(x * x + y * y + z * z + w * w);
	if (n < 1e-8f) {
		return CFrame(position);
	}
	x /= n;
	y /= n;
	z /= n;
	w /= n;
	CFrame f(position);
	f.rotation[0] = 1 - 2 * (y * y + z * z);
	f.rotation[1] = 2 * (x * y - z * w);
	f.rotation[2] = 2 * (x * z + y * w);
	f.rotation[3] = 2 * (x * y + z * w);
	f.rotation[4] = 1 - 2 * (x * x + z * z);
	f.rotation[5] = 2 * (y * z - x * w);
	f.rotation[6] = 2 * (x * z - y * w);
	f.rotation[7] = 2 * (y * z + x * w);
	f.rotation[8] = 1 - 2 * (x * x + y * y);
	return f;
}
CFrame CFrame::lookAt(Vec3 position, Vec3 target)
{
	if (length(target - position) < 1e-8f) {
		return CFrame(position);
	}
	Vec3 z = normalized(position - target), up(0, 1, 0);
	if (std::fabs(dot(z, up)) > .99f) {
		up = Vec3(1, 0, 0);
	}
	Vec3 x = normalized(cross(up, z)), y = cross(z, x);
	CFrame f(position);
	f.rotation[0] = x.x;
	f.rotation[1] = y.x;
	f.rotation[2] = z.x;
	f.rotation[3] = x.y;
	f.rotation[4] = y.y;
	f.rotation[5] = z.y;
	f.rotation[6] = x.z;
	f.rotation[7] = y.z;
	f.rotation[8] = z.z;
	return f;
}
} // namespace openrbx

#include "script_values.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <lauxlib.h>
#include <lua.h>
#include <new>

namespace openrbx {
static const char* VectorMeta = "OpenRBX.Vector3";
static const char* FrameMeta = "OpenRBX.CFrame";
static bool isValue(lua_State* L, int i, const char* meta)
{
	if (!lua_isuserdata(L, i) || !lua_getmetatable(L, i)) {
		return false;
	}
	luaL_getmetatable(L, meta);
	bool equal = lua_rawequal(L, -1, -2) != 0;
	lua_pop(L, 2);
	return equal;
}
bool isVector(lua_State* L, int i)
{
	return isValue(L, i, VectorMeta);
}
const Vec3& checkVector(lua_State* L, int i)
{
	return *static_cast<Vec3*>(luaL_checkudata(L, i, VectorMeta));
}
const CFrame& checkFrame(lua_State* L, int i)
{
	return *static_cast<CFrame*>(luaL_checkudata(L, i, FrameMeta));
}
void pushVector(lua_State* L, Vec3 value)
{
	new (lua_newuserdata(L, sizeof(Vec3))) Vec3(value);
	luaL_getmetatable(L, VectorMeta);
	lua_setmetatable(L, -2);
}
void pushFrame(lua_State* L, const CFrame& value)
{
	new (lua_newuserdata(L, sizeof(CFrame))) CFrame(value);
	luaL_getmetatable(L, FrameMeta);
	lua_setmetatable(L, -2);
}
static float finiteNumber(lua_State* L, int i, float fallback = 0)
{
	double v = luaL_optnumber(L, i, fallback);
	if (!std::isfinite(v) || std::fabs(v) > 1e7) {
		luaL_error(L, "Value must be a finite PSP-range number");
	}
	return (float) v;
}
static int readOnly(lua_State* L)
{
	return luaL_error(L, "Value types are immutable");
}
static int vectorNew(lua_State* L)
{
	pushVector(L, Vec3(finiteNumber(L, 1), finiteNumber(L, 2), finiteNumber(L, 3)));
	return 1;
}
static int vectorMethod(lua_State* L)
{
	const char* key = lua_tostring(L, lua_upvalueindex(1));
	Vec3 a = checkVector(L, 1), b = checkVector(L, 2);
	if (!std::strcmp(key, "lerp") || !std::strcmp(key, "Lerp")) {
		pushVector(L, a + (b - a) * finiteNumber(L, 3));
	}
	else if (!std::strcmp(key, "Dot") || !std::strcmp(key, "dot")) {
		lua_pushnumber(L, dot(a, b));
	}
	else {
		pushVector(L, cross(a, b));
	}
	return 1;
}
static int vectorIndex(lua_State* L)
{
	const Vec3& v = checkVector(L, 1);
	const char* key = luaL_checkstring(L, 2);
	if (!std::strcmp(key, "x") || !std::strcmp(key, "X")) {
		lua_pushnumber(L, v.x);
	}
	else if (!std::strcmp(key, "y") || !std::strcmp(key, "Y")) {
		lua_pushnumber(L, v.y);
	}
	else if (!std::strcmp(key, "z") || !std::strcmp(key, "Z")) {
		lua_pushnumber(L, v.z);
	}
	else if (!std::strcmp(key, "magnitude") || !std::strcmp(key, "Magnitude")) {
		lua_pushnumber(L, length(v));
	}
	else if (!std::strcmp(key, "unit") || !std::strcmp(key, "Unit")) {
		pushVector(L, length(v) > 1e-8f ? v * (1 / length(v)) : Vec3());
	}
	else if (
		!std::strcmp(key, "lerp") || !std::strcmp(key, "Lerp") || !std::strcmp(key, "Dot") ||
		!std::strcmp(key, "dot") || !std::strcmp(key, "Cross") || !std::strcmp(key, "cross")
	) {
		lua_pushvalue(L, 2);
		lua_pushcclosure(L, vectorMethod, 1);
	}
	else {
		return luaL_error(L, "%s is not a Vector3 member", key);
	}
	return 1;
}
static int vectorOp(lua_State* L)
{
	char op = (char) lua_tointeger(L, lua_upvalueindex(1));
	bool first = isVector(L, 1), second = isVector(L, 2);
	Vec3 a = first ? checkVector(L, 1) : Vec3(), b;
	if (op == 'n') {
		pushVector(L, a * -1);
		return 1;
	}
	if (op == '+' || op == '-') {
		b = checkVector(L, 2);
		pushVector(L, op == '+' ? a + b : a - b);
		return 1;
	}
	if (!first) {
		float scalar = finiteNumber(L, 1);
		a = Vec3(scalar, scalar, scalar);
	}
	if (second) {
		b = checkVector(L, 2);
	}
	else {
		float scalar = finiteNumber(L, 2);
		b = Vec3(scalar, scalar, scalar);
	}
	if (op == '*') {
		pushVector(L, Vec3(a.x * b.x, a.y * b.y, a.z * b.z));
	}
	else {
		pushVector(L, Vec3(a.x / b.x, a.y / b.y, a.z / b.z));
	}
	return 1;
}
static int vectorEqual(lua_State* L)
{
	lua_pushboolean(L, checkVector(L, 1) == checkVector(L, 2));
	return 1;
}
static int vectorString(lua_State* L)
{
	Vec3 v = checkVector(L, 1);
	char text[128];
	std::snprintf(text, sizeof(text), "%.7g, %.7g, %.7g", v.x, v.y, v.z);
	lua_pushstring(L, text);
	return 1;
}
static int frameNew(lua_State* L)
{
	int n = lua_gettop(L);
	CFrame f;
	if (n == 1) {
		f.position = checkVector(L, 1);
	}
	else if (n == 2) {
		f = CFrame::lookAt(checkVector(L, 1), checkVector(L, 2));
	}
	else if (n == 3 || n == 7 || n == 12) {
		f.position = Vec3(finiteNumber(L, 1), finiteNumber(L, 2), finiteNumber(L, 3));
		if (n == 7) {
			f = CFrame::quaternion(
				f.position, finiteNumber(L, 4), finiteNumber(L, 5), finiteNumber(L, 6), finiteNumber(L, 7)
			);
		}
		if (n == 12) {
			for (int i = 0; i < 9; ++i) {
				f.rotation[i] = finiteNumber(L, 4 + i);
			}
		}
	}
	else if (n != 0) {
		return luaL_error(L, "Invalid CFrame constructor argument count");
	}
	pushFrame(L, f);
	return 1;
}
static int frameAngles(lua_State* L)
{
	pushFrame(L, CFrame::angles(finiteNumber(L, 1), finiteNumber(L, 2), finiteNumber(L, 3)));
	return 1;
}
static int frameAxis(lua_State* L)
{
	pushFrame(L, CFrame::axisAngle(checkVector(L, 1), finiteNumber(L, 2)));
	return 1;
}
static int frameMethod(lua_State* L)
{
	const char* key = lua_tostring(L, lua_upvalueindex(1));
	CFrame f = checkFrame(L, 1);
	if (!std::strcmp(key, "inverse") || !std::strcmp(key, "Inverse")) {
		pushFrame(L, f.inverse());
		return 1;
	}
	if (!std::strcmp(key, "components") || !std::strcmp(key, "GetComponents")) {
		lua_pushnumber(L, f.position.x);
		lua_pushnumber(L, f.position.y);
		lua_pushnumber(L, f.position.z);
		for (float v : f.rotation) {
			lua_pushnumber(L, v);
		}
		return 12;
	}
	if (!std::strcmp(key, "toEulerAnglesXYZ")) {
		float y = std::asin(std::max(-1.f, std::min(1.f, f.rotation[2]))), x, z;
		if (std::fabs(std::cos(y)) > 1e-6f) {
			x = std::atan2(-f.rotation[5], f.rotation[8]);
			z = std::atan2(-f.rotation[1], f.rotation[0]);
		}
		else {
			x = std::atan2(f.rotation[7], f.rotation[4]);
			z = 0;
		}
		lua_pushnumber(L, x);
		lua_pushnumber(L, y);
		lua_pushnumber(L, z);
		return 3;
	}
	int count = lua_gettop(L) - 1;
	bool object = std::strstr(key, "Object") != nullptr;
	bool frame = !std::strcmp(key, "toWorldSpace") || !std::strcmp(key, "toObjectSpace") ||
				 !std::strcmp(key, "ToWorldSpace") || !std::strcmp(key, "ToObjectSpace");
	bool point = key[0] == 'p' || key[0] == 'P';
	if (!count) {
		if (frame) {
			pushFrame(L, object ? f.inverse() : f);
		}
		else {
			pushVector(L, point ? (object ? f.pointToObject(Vec3()) : f.position) : Vec3());
		}
		return 1;
	}
	for (int i = 0; i < count; ++i) {
		if (frame) {
			pushFrame(L, (object ? f.inverse() : f) * checkFrame(L, i + 2));
		}
		else {
			Vec3 v = checkVector(L, i + 2);
			pushVector(
				L,
				point ? (object ? f.pointToObject(v) : f.pointToWorld(v))
					  : (object ? f.vectorToObject(v) : f.vectorToWorld(v))
			);
		}
	}
	return count;
}
static int frameIndex(lua_State* L)
{
	CFrame f = checkFrame(L, 1);
	const char* key = luaL_checkstring(L, 2);
	if (!std::strcmp(key, "p") || !std::strcmp(key, "Position")) {
		pushVector(L, f.position);
		return 1;
	}
	if (!std::strcmp(key, "lookVector") || !std::strcmp(key, "LookVector")) {
		pushVector(L, f.vectorToWorld(Vec3(0, 0, -1)));
		return 1;
	}
	if (!std::strcmp(key, "RightVector")) {
		pushVector(L, f.vectorToWorld(Vec3(1, 0, 0)));
		return 1;
	}
	if (!std::strcmp(key, "UpVector")) {
		pushVector(L, f.vectorToWorld(Vec3(0, 1, 0)));
		return 1;
	}
	for (const char* method :
		 {"inverse",
		  "Inverse",
		  "components",
		  "GetComponents",
		  "toWorldSpace",
		  "toObjectSpace",
		  "ToWorldSpace",
		  "ToObjectSpace",
		  "pointToWorldSpace",
		  "pointToObjectSpace",
		  "PointToWorldSpace",
		  "PointToObjectSpace",
		  "vectorToWorldSpace",
		  "vectorToObjectSpace",
		  "VectorToWorldSpace",
		  "VectorToObjectSpace",
		  "toEulerAnglesXYZ"}) {
		if (!std::strcmp(key, method)) {
			lua_pushvalue(L, 2);
			lua_pushcclosure(L, frameMethod, 1);
			return 1;
		}
	}
	return luaL_error(L, "%s is not a CFrame member", key);
}
static int frameOp(lua_State* L)
{
	char op = (char) lua_tointeger(L, lua_upvalueindex(1));
	CFrame f = checkFrame(L, 1);
	if (op == '*') {
		if (isVector(L, 2)) {
			pushVector(L, f.pointToWorld(checkVector(L, 2)));
		}
		else {
			pushFrame(L, f * checkFrame(L, 2));
		}
	}
	else {
		f.position = f.position + checkVector(L, 2) * (op == '+' ? 1.f : -1.f);
		pushFrame(L, f);
	}
	return 1;
}
static int frameEqual(lua_State* L)
{
	lua_pushboolean(L, checkFrame(L, 1) == checkFrame(L, 2));
	return 1;
}
static void function(lua_State* L, const char* name, lua_CFunction f)
{
	lua_pushcfunction(L, f);
	lua_setfield(L, -2, name);
}
static void operation(lua_State* L, const char* name, lua_CFunction f, char op)
{
	lua_pushinteger(L, op);
	lua_pushcclosure(L, f, 1);
	lua_setfield(L, -2, name);
}
void registerValueTypes(lua_State* L)
{
	luaL_newmetatable(L, VectorMeta);
	function(L, "__index", vectorIndex);
	function(L, "__newindex", readOnly);
	function(L, "__eq", vectorEqual);
	function(L, "__tostring", vectorString);
	operation(L, "__add", vectorOp, '+');
	operation(L, "__sub", vectorOp, '-');
	operation(L, "__mul", vectorOp, '*');
	operation(L, "__div", vectorOp, '/');
	operation(L, "__unm", vectorOp, 'n');
	lua_pushliteral(L, "Vector3");
	lua_setfield(L, -2, "__metatable");
	lua_pop(L, 1);
	lua_newtable(L);
	function(L, "new", vectorNew);
	lua_setglobal(L, "Vector3");
	luaL_newmetatable(L, FrameMeta);
	function(L, "__index", frameIndex);
	function(L, "__newindex", readOnly);
	function(L, "__eq", frameEqual);
	operation(L, "__add", frameOp, '+');
	operation(L, "__sub", frameOp, '-');
	operation(L, "__mul", frameOp, '*');
	lua_pushliteral(L, "CFrame");
	lua_setfield(L, -2, "__metatable");
	lua_pop(L, 1);
	lua_newtable(L);
	function(L, "new", frameNew);
	function(L, "Angles", frameAngles);
	function(L, "fromEulerAnglesXYZ", frameAngles);
	function(L, "fromAxisAngle", frameAxis);
	lua_setglobal(L, "CFrame");
}
} // namespace openrbx

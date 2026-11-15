#pragma once
#include "math3d.h"
struct lua_State;
namespace openrbx {
void registerValueTypes(lua_State* thread);
bool isVector(lua_State* thread, int index);
const Vec3& checkVector(lua_State* thread, int index);
const CFrame& checkFrame(lua_State* thread, int index);
void pushVector(lua_State* thread, Vec3 value);
void pushFrame(lua_State* thread, const CFrame& value);
} // namespace openrbx

#pragma once
#include <cstdint>

namespace openrbx {
// Versioned, pointer-free diagnostic snapshot for PSP/emulator profiling.
// Read-only consumers locate openrbxProfile with the matching build's nm map.
struct RuntimeProfile
{
	uint32_t version = 1;
	float fps = 0;
	uint32_t heapBytes = 0, liveInstances = 0, arenaSlots = 0, collectedInstances = 0;
	float collectionMs = 0, physicsMs = 0;
	uint32_t player = 0, character = 0;
	float health = 0;
	uint32_t grounded = 0, runtimeError = 0, luaErrors = 0;
	float x = 0, y = 0, z = 0;
};
static_assert(sizeof(RuntimeProfile) == 68, "Profile wire layout");
} // namespace openrbx

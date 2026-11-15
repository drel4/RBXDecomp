#pragma once

#include "math3d.h"
#include "runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openrbx {
struct Part
{
	InstanceId source = NoInstance;
	Vec3 position, size;
	float rotation[9];
	uint32_t color;
	float transparency, radius;
	uint32_t first, count;
	int shape;
	Part();
};
struct alignas(16) Vertex
{
	uint32_t color;
	float x, y, z;
};
static_assert(sizeof(Vertex) == 16, "PSP GU vertex layout");

struct Scene
{
	DataModel model;
	std::vector<Part> parts;
	std::vector<Vertex> vertices;
	Vec3 minimum, maximum, focus;
	size_t skipped;
	bool hasSpawn;
	uint32_t collectionRevision = 0;
	Scene();
	void clear();
	size_t bytes() const;
};
// Streaming legacy XML loader, preserving runtime instances and script source.
// Loading does not execute scripts; the runtime controls when simulation starts.
bool loadPlace(const char* path, Scene& scene, std::string& error, bool assetMode = false);
// Apply graph mutations to GU meshes. Existing fixed-topology parts are remeshed
// in place; membership/topology changes trigger one bounded geometry rebuild.
bool refreshScene(Scene& scene, bool& changed, std::string& error);
uint32_t brickColor(int number);
} // namespace openrbx

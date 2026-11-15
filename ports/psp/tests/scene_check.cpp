#include "../scene.h"

#include <cmath>
#include <cstdio>
int main(int argc, char** argv)
{
	if (argc < 2) {
		return 2;
	}
	openrbx::Scene scene;
	std::string error;
	for (int i = 1; i < argc; ++i) {
		if (!openrbx::loadPlace(argv[i], scene, error)) {
			std::fprintf(stderr, "%s: %s\n", argv[i], error.c_str());
			return 1;
		}
		for (size_t n = 0; n < scene.parts.size(); ++n) {
			const openrbx::Part& p = scene.parts[n];
			if (!p.count || p.first + p.count > scene.vertices.size()) {
				return 3;
			}
		}
		for (size_t n = 0; n < scene.vertices.size(); ++n) {
			if (!std::isfinite(scene.vertices[n].x) || !std::isfinite(scene.vertices[n].y) ||
				!std::isfinite(scene.vertices[n].z)) {
				return 4;
			}
		}
		std::printf(
			"%s: %zu parts, %zu triangles, %.3f MiB scene / %.3f MiB PSP model; bounds %.1f %.1f %.1f to %.1f %.1f "
			"%.1f\n",
			argv[i],
			scene.parts.size(),
			scene.vertices.size() / 3,
			scene.bytes() / 1048576.0f,
			scene.model.pspBytes() / 1048576.0f,
			scene.minimum.x,
			scene.minimum.y,
			scene.minimum.z,
			scene.maximum.x,
			scene.maximum.y,
			scene.maximum.z
		);
	}
	scene.clear();
	if (scene.bytes() != 0) {
		return 5;
	}
	return 0;
}

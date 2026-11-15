#include "../scene.h"
#include "../script.h"

#include <cstdio>
int main(int argc, char** argv)
{
	if (argc != 2) {
		return 2;
	}
	openrbx::Scene scene;
	std::string error;
	if (!openrbx::loadPlace(argv[1], scene, error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	openrbx::ScriptRuntime scripts;
	scripts.open(scene.model);
	scripts.startPlaceScripts();
	for (int i = 0; i < 300; ++i) {
		scripts.step(1.0 / 30);
	}
	for (const auto& message : scripts.messages()) {
		std::puts(message.c_str());
	}
	std::printf(
		"Place script startup audit: %zu suspended threads, %.3f MiB Lua, %.3f MiB model\n",
		scripts.threadCount(),
		scripts.bytes() / 1048576.,
		scene.model.bytes() / 1048576.
	);
	// This audit is diagnostic, not a claim of full place/gameplay compatibility.
	return 0;
}

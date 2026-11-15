#include "../scene.h"
#include "../schema.h"
#include "../script.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

int main(int argc, char** argv)
{
	try {
		if (argc != 2) {
			return 2;
		}
		openrbx::Scene scene;
		std::string error;
		if (!openrbx::loadPlace(argv[1], scene, error)) {
			throw std::runtime_error(error);
		}
		openrbx::ScriptRuntime scripts;
		scripts.open(scene.model);
		std::ifstream file("tests/bindings.lua");
		if (!file) {
			throw std::runtime_error("Missing bindings test source");
		}
		std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (!scripts.execute(source)) {
			throw std::runtime_error("Bindings source failed compilation");
		}
		for (int i = 0; i < 100; ++i) {
			scripts.step(.05);
		}
		for (const auto& message : scripts.messages()) {
			std::puts(message.c_str());
		}
		auto workspace = scene.model.service("Workspace");
		auto passed = scene.model.findChild(workspace, "BindingsPassed");
		if (!passed || scene.model.get(passed)->property("Value")->text != "true") {
			throw std::runtime_error("Bindings did not reach acceptance marker");
		}
		if (scripts.threadCount() != 0) {
			throw std::runtime_error("Signal waiter/callback leak");
		}
		// Physics will produce this event using actual contact pairs. Exercise the
		// native-to-Lua bridge now, including a callback that yields before mutation.
		auto part = scene.model.findChild(workspace, "BindingsPart");
		scripts.execute("workspace.BindingsPart.Touched:connect(function(hit) wait(.05); hit.Name='TouchedByLua' end)");
		scripts.step(0);
		scripts.fireSignal(part, "Touched", {passed});
		scripts.step(0);
		scripts.step(.06);
		if (scene.model.get(passed)->name != "TouchedByLua") {
			throw std::runtime_error("Native Touched callback failed");
		}
		bool dirty = false;
		if (!openrbx::refreshScene(scene, dirty, error) || !dirty || scene.parts.size() != 4) {
			throw std::runtime_error("Created parts not synchronized to scene: " + error);
		}
		const auto* originalBuffer = scene.vertices.data();
		size_t originalCount = scene.vertices.size();
		for (int i = 0; i < 50; ++i) {
			scene.model.setProperty(
				part, openrbx::frameProperty("CFrame", openrbx::CFrame(openrbx::Vec3((float) i, 12, 13)))
			);
			if (!openrbx::refreshScene(scene, dirty, error) || !dirty) {
				throw std::runtime_error("Moving instance did not update geometry");
			}
			if (scene.vertices.data() != originalBuffer || scene.vertices.size() != originalCount) {
				throw std::runtime_error("Fixed-topology motion rebuilt/grows the world buffer");
			}
			const auto& mesh = scene.parts[scene.model.get(part)->renderPart];
			openrbx::Vec3 centroid;
			for (size_t j = mesh.first; j < mesh.first + mesh.count; ++j) {
				centroid = centroid + openrbx::Vec3(scene.vertices[j].x, scene.vertices[j].y, scene.vertices[j].z);
			}
			if (openrbx::length(centroid * (1.f / mesh.count) - mesh.position) > .0001f) {
				throw std::runtime_error("GU vertices don't follow CFrame");
			}
		}
		auto group = scene.model.findChild(workspace, "Group");
		for (int i = 0; i < 20; ++i) {
			scene.model.setParent(group, openrbx::NoInstance);
			if (!openrbx::refreshScene(scene, dirty, error) || scene.parts.size() != 3) {
				throw std::runtime_error("Ancestor detachment did not remove mesh");
			}
			scene.model.setParent(group, workspace);
			if (!openrbx::refreshScene(scene, dirty, error) || scene.parts.size() != 4) {
				throw std::runtime_error("Ancestor reattachment did not restore mesh");
			}
		}
		scripts.close();
		if (scripts.bytes() != 0) {
			throw std::runtime_error("Signal teardown leaked VM allocations");
		}
		std::puts(
			"PASS: vector/frame algebra, imported properties, inherited defaults, aliases, typed Changed, yielding "
			"Connect/Wait/disconnect, native Touched, ancestry, bounded dynamic GU buffers and teardown"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

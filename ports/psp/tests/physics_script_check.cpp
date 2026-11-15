#include "../physics.h"
#include "../scene.h"
#include "../schema.h"
#include "../script.h"

#include <cstdio>
#include <stdexcept>

using namespace openrbx;
static void require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
int main()
{
	try {
		Scene scene;
		std::string error;
		require(loadPlace("places/PhysicsRenderTest.rbxl", scene, error), error.c_str());
		ScriptRuntime scripts;
		scripts.open(scene.model);
		scripts.startPlaceScripts();
		PhysicsWorld world;
		world.open(scene.model);
		const Vertex* buffer = scene.vertices.data();
		for (int frame = 0; frame < 120; ++frame) {
			scripts.step(1.0 / 30);
			world.synchronize();
			world.advance(1.0 / 30);
			world.publish();
			bool dirty;
			require(refreshScene(scene, dirty, error), error.c_str());
			require(scene.vertices.data() == buffer, "Integrated physics grew/reallocated fixed-topology world mesh");
		}
		auto workspace = scene.model.service("Workspace");
		auto block = scene.model.findChild(workspace, "FallingBlock"),
			 ball = scene.model.findChild(workspace, "FallingBall"), floor = scene.model.findChild(workspace, "Floor");
		require(
			scene.model.get(block)->property("BrickColor")->text == "37" &&
				scene.model.get(ball)->property("BrickColor")->text == "37",
			"Real physics contact failed to execute place Lua"
		);
		require(
			world.body(block)->shape.frame.position.y > 1.98f && world.body(block)->shape.frame.position.y < 2.02f,
			"Scripted block did not land"
		);
		for (const auto& message : scripts.messages()) {
			require(message.find("Physics Touched:") == 0, "Unexpected script error in integrated fixture");
		}
		scripts.execute("workspace.FallingBlock:MakeJoints()");
		scripts.step(0);
		world.synchronize();
		require(
			world.body(block)->assembly == world.body(floor)->assembly,
			"Lua MakeJoints did not reach native surface builder"
		);
		scripts.execute("workspace.FallingBlock:BreakJoints();workspace.FallingBlock.Velocity=Vector3.new(0,50,0)");
		scripts.step(0);
		world.synchronize();
		world.advance(.1);
		world.publish();
		require(
			world.body(block)->shape.frame.position.y > 5, "Lua BreakJoints/Velocity did not release native assembly"
		);
		world.close();
		scripts.close();
		scene.clear();
		require(
			scene.bytes() == 0 && world.bytes() == 0 && scripts.bytes() == 0,
			"Integrated teardown leaked retained state"
		);
		std::puts(
			"PASS: actual place Lua responds to physical Touched, bounded GU motion, Lua MakeJoints/BreakJoints and "
			"complete teardown"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

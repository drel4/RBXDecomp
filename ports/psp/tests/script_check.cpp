#include "../scene.h"
#include "../script.h"

#include <cstdio>
#include <stdexcept>

static void require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
int main(int argc, char** argv)
{
	try {
		{
			openrbx::DataModel data;
			data.create("DataModel");
			auto world = data.service("Workspace"), players = data.service("Players");
			auto counter = data.create("IntValue", world), ticks = data.create("IntValue", world);
			data.setName(counter, "StartupCount");
			data.setName(ticks, "StartupTicks");
			auto seen = data.create("StringValue", world);
			data.setName(seen, "PlayerSeen");
			auto add = [&](const char* source) {
				auto id = data.create("Script", world);
				data.setProperty(id, {"Source", "ProtectedString", source});
			};
			add("game.RunService.Stepped:connect(function() "
				"workspace.StartupTicks.Value=workspace.StartupTicks.Value+1 end)");
			for (int i = 0; i < 1000; ++i) {
				add("workspace.StartupCount.Value=workspace.StartupCount.Value+1");
			}
			add("game.Players.ChildAdded:connect(function(p) workspace.PlayerSeen.Value=p.Name end)");
			openrbx::ScriptRuntime startup;
			startup.open(data);
			startup.primePlaceScripts();
			require(
				data.get(counter)->property("Value")->text.find("1000") == 0 &&
					data.get(ticks)->property("Value")->text == "0" && startup.errorCount() == 0,
				"Batched startup skipped scripts or advanced simulation time"
			);
			data.create("Player", players);
			startup.step(0);
			require(data.get(seen)->property("Value")->text == "Player", "Late startup registration missed Player");
			for (int i = 0; i < 300; ++i) {
				add("wait(.05); workspace.StartupCount.Value=workspace.StartupCount.Value+1");
			}
			startup.primePlaceScripts();
			for (int i = 0; i < 5; ++i) {
				startup.step(.1);
			}
			require(
				data.get(counter)->property("Value")->text.find("1300") == 0 && startup.errorCount() == 0,
				"Deferred yielding startup scripts were lost or repeated"
			);
		}
		require(argc == 2, "fixture required");
		openrbx::Scene scene;
		std::string error;
		require(openrbx::loadPlace(argv[1], scene, error), error.c_str());
		openrbx::ScriptRuntime scripts;
		scripts.open(scene.model);
		auto group = scene.model.findChild(scene.model.service("Workspace"), "Group");
		auto platform = scene.model.findChild(group, "A & B");
		auto owner = scene.model.findChild(platform, "Script");
		require(
			scripts.execute(
				R"(
assert(script.Parent.Name == 'A & B')
assert(game:GetService('Workspace') == workspace)
local part = script.Parent
local group = part.Parent
assert(#group:GetChildren() == 3)
assert(group:FindFirstChild('Script', true) == script)
local copy = group:Clone()
assert(copy.Parent == nil)
copy.Parent = workspace
copy.Name = 'Copied'
assert(copy.Weld.Part0 == copy:FindFirstChild('A & B'))
local marker = Instance.new('Model', workspace)
marker.Name = 'Start'
delay(0.15, function() marker.Name = 'Delayed' end)
local elapsed = wait(0.1)
assert(elapsed >= 0.1)
part.Name = 'AfterWait'
wait(0.1)
assert(marker.Name == 'Delayed')
copy:Remove()
assert(copy.Parent == nil and #copy:GetChildren() == 0)
print('script completed', tick())
)",
				owner
			),
			"source load failed"
		);
		scripts.step(0);
		require(scripts.threadCount() == 2, "wait/delay coroutines not retained");
		scripts.step(0.05);
		require(scene.model.get(platform)->name == "A & B", "wait resumed too early");
		scripts.step(0.051);
		require(scene.model.get(platform)->name == "AfterWait", "wait didn't mutate loaded instance");
		scripts.step(0.06);
		scripts.step(0.06);
		require(scripts.threadCount() == 0, "finished coroutine leak");
		require(
			scripts.messages().size() == 1 && scripts.messages()[0].find("script completed") == 0, "Lua script failed"
		);
		require(scripts.execute("while true do end"), "loop source failed");
		scripts.step(0);
		require(
			scripts.threadCount() == 0 && scripts.messages().back().find("instruction budget") != std::string::npos,
			"non-yielding script wasn't stopped"
		);
		require(!scripts.execute("if then"), "invalid Lua accepted");
		scripts.close();
		require(scripts.bytes() == 0 && scripts.threadCount() == 0, "Lua teardown leaked allocations");
		for (int i = 0; i < 25; ++i) {
			scripts.open(scene.model);
			require(scripts.execute("wait(1)"), "reload script failed");
			scripts.step(0);
			scripts.close();
			require(scripts.bytes() == 0, "pending coroutine leaked at reload");
		}
		std::puts(
			"PASS: real Lua 5.1.1, loaded instance bindings, Ref identity, clone, wait/delay, instruction limit, "
			"repeated teardown"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

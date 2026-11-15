#include "../player.h"
#include "../scene.h"
#include "../schema.h"
#include "../script.h"

#include <cstdio>
#include <stdexcept>

using namespace openrbx;
static void require(bool value, const std::string& message)
{
	if (!value) {
		throw std::runtime_error(message);
	}
}
int main(int argc, char** argv)
{
	try {
		{
			DataModel model;
			model.create("DataModel");
			auto workspace = model.service("Workspace");
			auto backup = model.create("Model"), child = model.create("Part", backup);
			model.retain(child);
			require(model.collect() == 0, "Retained child lost its detached parent/subtree");
			model.release(child);
			auto holder = model.create("ObjectValue", workspace);
			Property ref("Value", "Ref");
			ref.reference = backup;
			model.setProperty(holder, ref);
			require(model.collect() == 0, "Reachable Ref did not retain detached graph");
			model.remove(holder);
			require(model.collect() == 3, "Unreachable graph was not reclaimed");
			auto replacement = model.create("Part", workspace);
			require(
				model.size() == 5 && replacement != holder && !model.get(holder) && !model.get(backup) &&
					!model.get(child),
				"Collected handles aliased a new generation"
			);
			auto a = model.create("ObjectValue"), b = model.create("ObjectValue");
			ref.reference = b;
			model.setProperty(a, ref);
			ref.reference = a;
			model.setProperty(b, ref);
			require(model.collect() == 2, "Unreachable Ref cycle leaked");
			auto anchor = model.create("Part", workspace), moving = model.create("Part", workspace);
			model.setProperty(anchor, {"Anchored", "bool", "true"});
			model.setProperty(anchor, frameProperty("CFrame", CFrame(Vec3(0, 1000, 0))));
			model.setProperty(moving, frameProperty("CFrame", CFrame(Vec3(20, 1000, 0))));
			auto weld = [&](float offset) {
				auto id = model.create("Weld", workspace);
				Property part0("Part0", "Ref"), part1("Part1", "Ref");
				part0.reference = anchor;
				part1.reference = moving;
				model.setProperty(id, part0);
				model.setProperty(id, part1);
				model.setProperty(id, frameProperty("C0", CFrame(Vec3(offset, 0, 0))));
				return id;
			};
			auto previous = weld(5);
			PhysicsWorld physics;
			physics.open(model);
			model.remove(previous);
			model.collect();
			auto next = weld(9);
			require(DataModel::slot(previous) == DataModel::slot(next), "Joint generation test did not reuse a slot");
			physics.synchronize();
			require(
				physics.body(moving)->shape.frame.position.x == 9,
				"New-generation joint with same revision was mistaken for retired joint"
			);
			model.remove(replacement);
			model.collect();
			auto partToJoint = weld(11);
			require(
				DataModel::slot(partToJoint) == DataModel::slot(replacement),
				"Part-to-joint regression did not reuse a physical slot"
			);
			physics.synchronize();
			size_t rebuilds = physics.statistics().rebuilds;
			physics.synchronize();
			require(
				physics.statistics().rebuilds == rebuilds,
				"Part-to-joint tombstone repeatedly invalidates an unchanged joint"
			);
			physics.close();
		}
		Scene scene;
		std::string error;
		require(loadPlace(argc > 1 ? argv[1] : "tests/PlayerFixture.rbxl", scene, error), error);
		ScriptRuntime scripts;
		scripts.open(scene.model);
		PhysicsWorld world;
		world.open(scene.model);
		LocalPlayer game;
		game.open(scene.model, world, "content", false);
		scripts.startPlaceScripts();
		scripts.step(0);
		game.create();
		game.loadCharacter();
		auto frame = [&] {
			scripts.step(1.0 / 30);
			world.synchronize();
			world.advance(1.0 / 30);
			world.publish();
			game.advance(1.0 / 30);
			world.synchronize();
			bool changed;
			require(refreshScene(scene, changed, error), error);
		};
		auto collect = [&] {
			scripts.collect();
			world.synchronize();
			bool changed;
			require(refreshScene(scene, changed, error), error);
			for (const Part& part : scene.parts) {
				require(scene.model.get(part.source) != nullptr, "Render cache retained a collected handle");
			}
		};
		scripts.execute(
			"shared.backup=workspace:findFirstChild('Spawn',true) or game.Players.LocalPlayer.Character.Head;"
			"shared.backup=shared.backup:Clone();shared.backup.Name='RetainedBackup';"
			"local orphan=Instance.new('Part');orphan.Name='SignalHeld';shared.savedSignal=orphan.Touched;"
			"local function hook() local h=game.Players.LocalPlayer.Character.Humanoid;"
			"h.Died:connect(function() shared.lifetimeDeaths=(shared.lifetimeDeaths or 0)+1;assert(h.Health==0) end) "
			"end;"
			"hook();game.Players.LocalPlayer.Changed:connect(function(p) if p=='Character' then hook() end end)"
		);
		for (int i = 0; i < 100; ++i) {
			frame();
		}
		collect();
		scripts.execute(
			"assert(shared.backup.Name=='RetainedBackup');assert(shared.backup.Parent==nil);"
			"shared.savedSignal:connect(function() end);shared.savedSignal=nil"
		);
		frame();
		collect();
		size_t slots = scene.model.size(), live = scene.model.liveCount(), bytes = scene.bytes(),
			   physical = world.bytes();
		auto player = game.player();
		for (int cycle = 0; cycle < 128; ++cycle) {
			auto previous = game.character();
			auto previousTorso = scene.model.findChild(previous, "Torso");
			game.loadCharacter();
			for (int i = 0; i < 3; ++i) {
				frame();
			}
			collect();
			size_t rebuilds = world.statistics().rebuilds;
			frame();
			require(
				world.statistics().rebuilds == rebuilds,
				"Unchanged world rebuilt after slot reuse (part-to-joint tombstone alias)"
			);
			require(!scene.model.get(previous), "Retired character remained pinned by old callbacks");
			require(!world.body(previousTorso), "Physics resolved stale generation");
			require(game.player() == player && game.alive(), "Respawn changed Player or lost character");
			require(scene.model.size() <= slots + 64, "Respawn arena grows instead of reusing slots");
			require(scene.model.liveCount() <= live + 8, "Respawn live instances accumulate");
		}
		for (int cycle = 0; cycle < 12; ++cycle) {
			auto previous = game.character();
			scene.model.setProperty(game.humanoid(), {"Health", "float", "0"});
			for (int i = 0; i < 100; ++i) {
				frame();
				if (i % 30 == 0) {
					collect();
				}
			}
			collect();
			require(
				game.alive() && game.character() != previous && !scene.model.get(previous),
				"Natural death/respawn did not release previous rig"
			);
		}
		scripts.execute(
			"assert(shared.backup.Name=='RetainedBackup');assert(shared.lifetimeDeaths==12);shared.backup=nil"
		);
		frame();
		collect();
		require(scripts.errorCount() <= (argc > 1 ? 1u : 0u), "Unexpected Lua runtime error during lifetime stress");
		for (const auto& message : scripts.messages()) {
			std::printf("Lua: %s\n", message.c_str());
			require(message.find("assertion failed") == std::string::npos, "Lifetime Lua assertion failed");
		}
		require(scene.model.liveCount() <= live + 8, "Natural respawn retains live instances");
		require(
			scene.bytes() <= bytes + 65536 && world.bytes() <= physical + 32768,
			"Retained scene/physics storage grows across respawns"
		);
		std::printf(
			"PASS: graph/Ref roots and cycles, Lua-held backups/signals, generation safety, "
			"128 replacements + 12 natural deaths; slots %zu -> %zu, live %zu -> %zu, "
			"scene %zu -> %zu, physics %zu -> %zu bytes\n",
			slots,
			scene.model.size(),
			live,
			scene.model.liveCount(),
			bytes,
			scene.bytes(),
			physical,
			world.bytes()
		);
		game.close();
		world.close();
		scripts.close();
		scene.clear();
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

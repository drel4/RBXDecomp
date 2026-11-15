#include "../player.h"
#include "../scene.h"
#include "../schema.h"
#include "../script.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace openrbx;
static void require(bool condition, const std::string& message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
int main(int argc, char** argv)
{
	try {
		Scene scene;
		std::string error;
		require(loadPlace(argc > 1 ? argv[1] : "tests/PlayerFixture.rbxl", scene, error), error);
		ScriptRuntime scripts;
		scripts.open(scene.model);
		PhysicsWorld world;
		world.open(scene.model);
		LocalPlayer game;
		game.open(scene.model, world, "content", false);
		scripts.primePlaceScripts();
		if (!game.player()) {
			game.create();
		}
		if (!game.character()) {
			game.loadCharacter();
		}
		auto frame = [&](Vec3 move = Vec3(), bool jump = false) {
			game.input(move, jump);
			scripts.step(1.0 / 30);
			world.synchronize();
			world.advance(1.0 / 30);
			world.publish();
			game.advance(1.0 / 30);
			world.synchronize();
			bool dirty;
			require(refreshScene(scene, dirty, error), error);
		};
		if (argc > 1) {
			for (int i = 0; i < 180; ++i) {
				frame(i >= 60 && i < 90 ? Vec3(0, 0, -1) : Vec3(), i == 70);
			}
			for (const auto& message : scripts.messages()) {
				std::printf("Lua: %s\n", message.c_str());
			}
			if (!game.alive()) {
				auto deadCharacter = game.character();
				Vec3 deathPosition = game.position();
				std::printf(
					"Real-place death after movement/jump at (%.2f,%.2f,%.2f); waiting for native respawn\n",
					deathPosition.x,
					deathPosition.y,
					deathPosition.z
				);
				for (int i = 0; i < 150 && !game.alive(); ++i) {
					frame();
				}
				require(
					game.alive() && game.character() != deadCharacter,
					"Real-place death did not produce a fresh respawn"
				);
			}
			require(game.alive(), "Real-place local character is not alive");
			Vec3 position = game.position();
			std::printf(
				"Real place: parts=%zu player=%u character=%u grounded=%d position=(%.2f,%.2f,%.2f) "
				"leaderstats=%u heap-accounted=%.2f MiB\n",
				scene.parts.size(),
				game.player(),
				game.character(),
				game.onGround(),
				position.x,
				position.y,
				position.z,
				scene.model.findChild(game.player(), "leaderstats"),
				scene.bytes() / 1048576.0
			);
			game.close();
			world.close();
			scripts.close();
			scene.clear();
			return 0;
		}
		// Jump as soon as the rig reaches the spawn, not after the team-color
		// script's one-second bootstrap. Events must already be connected.
		for (int i = 0; i < 3; ++i) {
			frame();
		}
		require(game.onGround(), "Fresh character did not reach spawn");
		frame(Vec3(), true);
		for (int i = 0; i < 6; ++i) {
			frame();
		}
		auto earlyTorso = scene.model.findChild(game.character(), "Torso");
		auto earlyShoulder = scene.model.findChild(earlyTorso, "Right Shoulder");
		require(
			std::strtof(scene.model.get(earlyShoulder)->property("DesiredAngle")->text.c_str(), nullptr) > 3,
			"Immediate post-spawn jump missed animation event"
		);
		for (int i = 0; i < 60; ++i) {
			frame();
		}
		for (const auto& message : scripts.messages()) {
			std::printf("Lua: %s\n", message.c_str());
		}
		require(scripts.messages().empty(), "Character/animation scripts reported errors");
		require(game.onGround() && game.alive(), "Imported character did not settle on spawn");
		require(scene.parts.size() == 9, "Real six-part rig not rendered");
		auto player = game.player(), firstCharacter = game.character();
		require(
			scene.model.get(scene.model.service("Players"))->property("LocalPlayer")->reference == player,
			"LocalPlayer service reference missing"
		);
		require(game.create(0) == player, "CreateLocalPlayer duplicated the player");
		Vec3 start = game.position();
		for (int i = 0; i < 30; ++i) {
			frame(Vec3(0, 0, -1));
		}
		require(std::fabs(game.position().z - start.z + 16) < .1f, "Walk speed is not 16 studs/second");
		for (int i = 0; i < 15; ++i) {
			frame();
		}
		float ground = game.position().y, highest = ground;
		frame(Vec3(), true);
		for (int i = 0; i < 8; ++i) {
			frame();
			highest = std::max(highest, game.position().y);
		}
		auto torso = scene.model.findChild(game.character(), "Torso"),
			 shoulder = scene.model.findChild(torso, "Right Shoulder");
		require(
			std::strtof(scene.model.get(shoulder)->property("DesiredAngle")->text.c_str(), nullptr) > 3,
			"Actual animation Lua did not select jump pose"
		);
		require(
			std::strtof(scene.model.get(shoulder)->property("CurrentAngle")->text.c_str(), nullptr) > 2,
			"Motor did not advance toward jump pose"
		);
		for (int i = 0; i < 5; ++i) {
			frame();
			highest = std::max(highest, game.position().y);
		}
		require(
			std::strtof(scene.model.get(shoulder)->property("DesiredAngle")->text.c_str(), nullptr) > 3,
			"Jump pose changed to fall pose before landing"
		);
		for (int i = 0; i < 30; ++i) {
			frame();
		}
		require(
			highest - ground > 5.8f && highest - ground < 6.6f && game.onGround(), "Jump trajectory or landing failed"
		);
		scripts.execute(
			"local "
			"p=game.Players.LocalPlayer;assert(game.Players:GetPlayerFromCharacter(p.Character)==p);p:LoadCharacter()"
		);
		frame();
		require(game.character() != firstCharacter, "Lua LoadCharacter did not replace character");
		for (int i = 0; i < 20; ++i) {
			frame();
		}
		auto beforeDeath = game.character();
		for (int i = 0; i < 60 && game.alive(); ++i) {
			frame(Vec3(1, 0, 0));
		}
		frame();
		require(!game.alive(), "Real contact did not execute kill-brick Lua/death");
		for (int i = 0; i < 100; ++i) {
			frame();
		}
		require(game.alive() && game.character() != beforeDeath && game.player() == player, "Automatic respawn failed");
		require(scene.model.get(beforeDeath)->parent == NoInstance, "Respawn kept old character in Workspace");
		require(
			scene.model.get(game.humanoid())->property("Health")->text != "0.000000", "Respawn reused dead Humanoid"
		);
		game.close();
		require(
			scene.model.get(scene.model.service("Players"))->property("LocalPlayer")->reference == NoInstance,
			"Local reset retained LocalPlayer"
		);
		world.close();
		scripts.close();
		scene.clear();
		require(
			scene.bytes() == 0 && world.bytes() == 0 && scripts.bytes() == 0, "Player teardown retained world/VM state"
		);
		std::puts(
			"PASS: real character assets, actual animation Lua/Motors, walk/jump/landing, Lua player APIs, physical "
			"kill script, death, respawn, local reset and teardown"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

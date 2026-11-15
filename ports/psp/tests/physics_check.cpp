#include "../physics.h"
#include "../scene.h"
#include "../schema.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace openrbx;
static void require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
static void near(float a, float b, float epsilon, const char* message)
{
	require(std::fabs(a - b) < epsilon, message);
}
static InstanceId part(DataModel& model, Vec3 position, Vec3 size, bool anchored = false)
{
	auto id = model.create("Part", model.service("Workspace"));
	model.setProperty(id, frameProperty("CFrame", CFrame(position)));
	model.setProperty(id, vectorProperty("size", size));
	model.setProperty(id, {"Anchored", "bool", anchored ? "true" : "false"});
	return id;
}
static float falling(int rate)
{
	DataModel model;
	auto id = part(model, Vec3(0, 100, 0), Vec3(2, 2, 2));
	PhysicsWorld world;
	world.open(model);
	for (int i = 0; i < rate; ++i) {
		world.synchronize();
		world.advance(1.0 / rate);
		world.publish();
	}
	near((float) world.time(), 1, .00001f, "Simulation lost real time");
	return world.body(id)->shape.frame.position.y;
}
int main(int argc, char** argv)
{
	try {
		near(falling(30), falling(60), .0001f, "Render rate changes gravity");
		near(falling(30), falling(10), .0001f, "Slow render changes gravity");
		near(
			falling(30), 100 - 196.2f * .5f * (1.f + 1.f / 240), .002f, "Gravity/integration differs from engine units"
		);
		DataModel model;
		part(model, Vec3(0, -1, 0), Vec3(100, 2, 100), true);
		auto box = part(model, Vec3(0, 8, 0), Vec3(2, 2, 2));
		auto trigger = part(model, Vec3(0, 5, 0), Vec3(6, .2f, 6), true);
		model.setProperty(trigger, {"CanCollide", "bool", "false"});
		int touched = 0;
		model.signal = [&](InstanceId sender, std::string_view event, const std::vector<SignalValue>& args) {
			if (sender == trigger && event == "Touched" && std::get<InstanceId>(args[0]) == box) {
				++touched;
			}
		};
		PhysicsWorld world;
		world.open(model);
		for (int i = 0; i < 120; ++i) {
			world.synchronize();
			world.advance(1.0 / 30);
			world.publish();
		}
		near(world.body(box)->shape.frame.position.y, 1, .02f, "Dynamic box did not land on floor");
		require(touched >= 1, "CanCollide=false trigger did not deliver Touched");
		require(world.statistics().awake == 0, "Resting dynamic assembly did not sleep");
		part(model, Vec3(50, -100, 0), Vec3(2, 2, 2), true);
		world.synchronize(); // unrelated addition must not weld the landed box
		world.advance(0);
		require(world.statistics().awake == 0, "Unrelated part creation woke a sleeping assembly");
		model.setProperty(box, vectorProperty("Velocity", Vec3(0, 50, 0)));
		world.synchronize();
		world.advance(.1);
		world.publish();
		require(world.body(box)->shape.frame.position.y > 4, "Script velocity did not wake/integrate assembly");
		world.close();
		DataModel joined;
		part(joined, Vec3(0, -1, 0), Vec3(10, 2, 10), true);
		auto attached = part(joined, Vec3(0, 1, 0), Vec3(2, 2, 2));
		auto loose = part(joined, Vec3(20, 10, 0), Vec3(2, 2, 2));
		world.open(joined);
		require(world.statistics().assemblies == 2, "Stud/inlet surface joint did not form assembly");
		world.advance(.5);
		world.publish();
		near(world.body(attached)->shape.frame.position.y, 1, .0001f, "Anchored assembly drifted");
		require(world.body(loose)->shape.frame.position.y < -10, "Unjoined body was incorrectly frozen");
		world.changeJoints(attached, false);
		world.synchronize();
		require(world.statistics().assemblies == 3, "BreakJoints did not split anchored assembly");
		joined.setProperty(attached, vectorProperty("Velocity", Vec3(0, 50, 0)));
		world.synchronize();
		world.advance(.1);
		world.publish();
		require(world.body(attached)->shape.frame.position.y > 4, "Broken joint still pins part");
		joined.setProperty(attached, frameProperty("CFrame", CFrame(Vec3(0, 1, 0))));
		joined.setProperty(attached, vectorProperty("Velocity", Vec3()));
		world.changeJoints(attached, true);
		world.synchronize();
		require(world.statistics().assemblies == 2, "MakeJoints did not recreate compatible surface joint");
		world.close();
		DataModel welded;
		auto a = part(welded, Vec3(0, 20, 0), Vec3(2, 2, 2));
		auto b = part(welded, Vec3(10, 20, 0), Vec3(2, 2, 2));
		auto weld = welded.create("Weld", a);
		welded.setProperty(weld, frameProperty("C0", CFrame(Vec3(10, 0, 0))));
		Property p("Part0", "Ref");
		p.reference = a;
		welded.setProperty(weld, p);
		p.name = "Part1";
		p.reference = b;
		welded.setProperty(weld, p);
		world.open(welded);
		require(world.statistics().assemblies == 1, "Explicit Weld did not form assembly");
		world.advance(.2);
		near(
			length(world.body(a)->shape.frame.position - world.body(b)->shape.frame.position),
			10,
			.0001f,
			"Rigid assembly changed separation"
		);
		welded.setProperty(a, vectorProperty("RotVelocity", Vec3(0, 2, 0)));
		world.synchronize();
		world.advance(.2);
		world.publish();
		near(
			length(world.body(a)->shape.frame.position - world.body(b)->shape.frame.position),
			10,
			.0002f,
			"Rotating assembly lost rigid rest frames"
		);
		world.advance(2.5);
		require(world.debt() > 1.49, "Physics call discarded backlog");
		world.advance(0);
		world.advance(0);
		require(world.debt() < .00001, "Physics backlog never caught up");
		CollisionShape wedge{CFrame(), Vec3(4, 4, 4), 3}, sphere{CFrame(Vec3(0, 1.5f, 0)), Vec3(1, 1, 1), 0};
		Contact c;
		require(!intersect(sphere, wedge, c), "Wedge collider filled empty upper half");
		sphere.frame.position = Vec3(0, .1f, 0);
		require(intersect(sphere, wedge, c) && c.normal.y > .5f, "Wedge ramp normal incorrect");
		CollisionShape obb{CFrame::angles(0, .5f, 0), Vec3(4, 4, 4), 1}, other{CFrame(Vec3(1, 0, 0)), Vec3(2, 2, 2), 1};
		require(intersect(obb, other, c), "Rotated OBB collision missed");
		other.frame.position = Vec3(10, 0, 0);
		require(!intersect(obb, other, c), "Separated OBB collision false positive");
		if (argc == 2) {
			Scene scene;
			std::string error;
			require(loadPlace(argv[1], scene, error), error.c_str());
			world.open(scene.model);
			std::vector<Vec3> original;
			for (const auto& object : scene.model.instances()) {
				original.push_back(object.spatial ? propertyFrame(*object.property("CFrame")).position : Vec3());
			}
			for (int i = 0; i < 90; ++i) {
				world.synchronize();
				world.advance(1.0 / 30);
				world.publish();
			}
			size_t moved = 0;
			for (size_t slotIndex = 1; slotIndex <= scene.model.size(); ++slotIndex) {
				InstanceId id = scene.model.idAt(slotIndex);
				if (const auto* state = world.body(id)) {
					if (length(state->shape.frame.position - original[id - 1]) > 2) {
						++moved;
					}
				}
			}
			const auto& s = world.statistics();
			std::printf(
				"Crossroads physics audit: %zu parts, %zu assemblies, %zu joints, %zu awake, %zu moved >2 studs, %.3f "
				"MiB physics\n",
				s.parts,
				s.assemblies,
				s.joints,
				s.awake,
				moved,
				world.bytes() / 1048576.
			);
		}
		std::puts(
			"PASS: real-time fixed stepping, gravity, script velocity, sleeping, triggers, surface assemblies, "
			"explicit Welds, backlog, OBB/wedge collisions"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}

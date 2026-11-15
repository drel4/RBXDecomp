#pragma once
#include "collision.h"
#include "runtime.h"

#include <set>

namespace openrbx {
class PhysicsWorld
{
public:
	static constexpr double Step = 1.0 / 240;
	struct Body
	{
		InstanceId id = NoInstance;
		CollisionShape shape;
		Vec3 velocity, angular;
		Aabb bounds;
		int surfaces[6] = {0, 3, 0, 0, 4, 0};
		float friction = .3f, elasticity = .5f;
		bool anchored = false, canCollide = true, active = true, kinematic = false;
		bool dirty = false;
		uint32_t revision = UINT32_MAX;
		int assembly = -1;
		int leaf = -1;
		CFrame relative;
	};
	struct Statistics
	{
		size_t parts = 0, assemblies = 0, awake = 0, joints = 0, narrowTests = 0, contacts = 0, steps = 0;
		size_t rebuilds = 0;
	};
	void open(DataModel& model);
	void close();
	void synchronize();
	void advance(double seconds);
	void publish();
	void changeJoints(InstanceId root, bool make);
	const Body* body(InstanceId id) const;
	void setKinematic(InstanceId id, bool value);
	void query(const Aabb& bounds, std::vector<int>& indices) const;
	const Body& bodyAt(size_t index) const { return bodies[index]; }
	const Statistics& statistics() const { return stats; }
	double time() const { return simulatedTime; }
	double debt() const { return accumulator; }
	size_t bytes() const;

private:
	struct Assembly
	{
		CFrame frame;
		CFrame publishedFrame;
		Vec3 velocity, angular;
		float inverseMass = 0, quiet = 0;
		bool fixed = false, sleeping = false, supported = false;
		std::vector<int> parts;
	};
	struct Node
	{
		Aabb bounds;
		int left = -1, right = -1, body = -1, parent = -1;
	};
	DataModel* model = nullptr;
	InstanceId workspace = NoInstance;
	std::vector<Body> bodies;
	std::vector<int> byId, order, candidates;
	std::vector<uint64_t> jointRevisions;
	std::vector<Assembly> assemblies;
	std::vector<Node> tree;
	Statistics stats;
	std::set<uint64_t> touching, previousTouching;
	std::set<uint64_t> autoLinks;
	std::set<InstanceId> joiningCandidates;
	double accumulator = 0, simulatedTime = 0;
	bool rebuild = true;
	Body readBody(InstanceId id) const;
	void rebuildAssemblies();
	int buildTree(size_t begin, size_t end);
	void updateAssembly(size_t index);
	void updateBounds(int body);
	void tick(float dt);
};
} // namespace openrbx

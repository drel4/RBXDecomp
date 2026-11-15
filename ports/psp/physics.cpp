#include "physics.h"

#include "schema.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <stdexcept>

namespace openrbx {
static float number(const Instance& object, const char* key, float fallback = 0)
{
	const Property* p = object.property(key);
	if (!p) {
		return fallback;
	}
	char* end;
	float value = std::strtof(p->text.c_str(), &end);
	if (end == p->text.c_str() || !std::isfinite(value)) {
		throw std::runtime_error("Invalid physics property " + std::string(key));
	}
	return value;
}
static bool boolean(const Instance& object, const char* key, bool fallback = false)
{
	const Property* p = object.property(key);
	return p ? p->text == "true" : fallback;
}
static uint64_t pairKey(InstanceId a, InstanceId b)
{
	return (uint64_t) std::min(a, b) << 32 | std::max(a, b);
}
void PhysicsWorld::open(DataModel& data)
{
	close();
	model = &data;
	workspace = model->service("Workspace");
	model->jointOperation = [this](InstanceId root, bool make) { changeJoints(root, make); };
	synchronize();
}
void PhysicsWorld::close()
{
	if (model) {
		model->jointOperation = nullptr;
	}
	model = nullptr;
	workspace = NoInstance;
	std::vector<Body>().swap(bodies);
	std::vector<int>().swap(byId);
	std::vector<int>().swap(order);
	std::vector<int>().swap(candidates);
	std::vector<Assembly>().swap(assemblies);
	std::vector<Node>().swap(tree);
	std::vector<uint64_t>().swap(jointRevisions);
	touching.clear();
	autoLinks.clear();
	joiningCandidates.clear();
	previousTouching.clear();
	stats = {};
	accumulator = simulatedTime = 0;
	rebuild = true;
}
PhysicsWorld::Body PhysicsWorld::readBody(InstanceId id) const
{
	const Instance& object = *model->get(id);
	Body b;
	b.id = id;
	b.shape.frame = propertyFrame(*object.property("CFrame"));
	b.shape.size = propertyVector(*object.property("size"));
	if (b.shape.size.x <= 0 || b.shape.size.y <= 0 || b.shape.size.z <= 0) {
		throw std::runtime_error("Physics part size must be positive");
	}
	b.shape.type = object.className == "WedgePart" ? 3 : (int) number(object, "shape", 1);
	b.velocity = propertyVector(*object.property("Velocity"));
	b.angular = propertyVector(*object.property("RotVelocity"));
	b.anchored = boolean(object, "Anchored");
	b.canCollide = boolean(object, "CanCollide", true);
	b.friction = std::max(0.f, std::min(2.f, number(object, "Friction", .3f)));
	b.elasticity = std::max(0.f, std::min(1.f, number(object, "Elasticity", .5f)));
	static const char* surfaces[] = {
		"RightSurface", "TopSurface", "BackSurface", "LeftSurface", "BottomSurface", "FrontSurface"
	};
	for (int i = 0; i < 6; ++i) {
		b.surfaces[i] = (int) number(object, surfaces[i], i == 1 ? 3 : i == 4 ? 4 : 0);
	}
	b.bounds = b.shape.bounds();
	b.revision = object.revision;
	return b;
}
const PhysicsWorld::Body* PhysicsWorld::body(InstanceId id) const
{
	if (!id || DataModel::slot(id) >= byId.size() || byId[DataModel::slot(id)] < 0 ||
		!bodies[byId[DataModel::slot(id)]].active || bodies[byId[DataModel::slot(id)]].id != id) {
		return nullptr;
	}
	return &bodies[byId[DataModel::slot(id)]];
}
void PhysicsWorld::setKinematic(InstanceId id, bool value)
{
	if (!body(id)) {
		return;
	}
	Body& b = bodies[byId[DataModel::slot(id)]];
	if (b.kinematic == value) {
		return;
	}
	b.kinematic = value;
	rebuild = true;
}
void PhysicsWorld::changeJoints(InstanceId target, bool make)
{
	if (!model || !model->get(target)) {
		return;
	}
	auto selected = [&](InstanceId id) { return id == target || model->isDescendant(id, target); };
	if (make) {
		for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
			InstanceId id = model->idAt(slotIndex);
			if (const Instance* object = model->get(id)) {
				if (object->spatial && selected(id)) {
					joiningCandidates.insert(id);
				}
			}
		}
	}
	else {
		for (auto it = autoLinks.begin(); it != autoLinks.end();) {
			if (selected((InstanceId) (*it >> 32)) || selected((InstanceId) *it)) {
				it = autoLinks.erase(it);
			}
			else {
				++it;
			}
		}
		for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
			InstanceId id = model->idAt(slotIndex);
			const Instance* object = model->get(id);
			if (!object) {
				continue;
			}
			if (object->spatial && selected(id)) {
				joiningCandidates.erase(id);
			}
			if (!classIsA(object->className, "JointInstance")) {
				continue;
			}
			if (selected(object->property("Part0")->reference) || selected(object->property("Part1")->reference)) {
				model->setParent(id, NoInstance);
			}
		}
	}
	rebuild = true;
}
void PhysicsWorld::synchronize()
{
	if (!model) {
		return;
	}
	byId.resize(model->size() + 1, -1);
	jointRevisions.resize(model->size() + 1, UINT64_MAX);
	for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
		InstanceId id = model->idAt(slotIndex);
		const Instance* object = model->get(id);
		int index = byId[slotIndex];
		if (index >= 0 && bodies[index].id != id) {
			// Collected slot or a new generation: old contacts and assemblies must
			// not inherit this handle. Reuse the bounded physical slot, not identity.
			if (bodies[index].active) {
				rebuild = true;
			}
			Body tombstone;
			tombstone.id = id;
			tombstone.active = false;
			bodies[index] = tombstone;
			jointRevisions[slotIndex] = UINT64_MAX;
			if (object && object->spatial && model->isDescendant(id, workspace)) {
				bodies[index] = readBody(id);
				joiningCandidates.insert(id);
				rebuild = true;
			}
		}
		if (index >= 0 && object && object->spatial && bodies[index].active &&
			bodies[index].revision == object->revision) {
			continue;
		}
		if (object && (object->className == "Weld" || object->className == "Snap" || object->className == "Glue")) {
			uint64_t revision =
				model->isDescendant(id, workspace) ? (uint64_t(id) << 32) | object->revision : UINT64_MAX;
			if (jointRevisions[slotIndex] != revision) {
				jointRevisions[slotIndex] = revision;
				rebuild = true;
			}
		}
		bool active = object && object->spatial && model->isDescendant(id, workspace);
		if (!active) {
			if (index >= 0 && bodies[index].active) {
				bodies[index].active = false;
				rebuild = true;
			}
			continue;
		}
		if (index < 0) {
			byId[slotIndex] = (int) bodies.size();
			bodies.push_back(readBody(id));
			joiningCandidates.insert(id);
			rebuild = true;
			continue;
		}
		Body& old = bodies[index];
		if (old.revision == object->revision && old.active) {
			continue;
		}
		Body next = readBody(id);
		next.kinematic = old.kinematic;
		bool sameGeometry = old.active && old.anchored == next.anchored && old.shape.size == next.shape.size &&
							old.shape.type == next.shape.type;
		for (int i = 0; i < 6 && sameGeometry; ++i) {
			if (old.surfaces[i] != next.surfaces[i]) {
				sameGeometry = false;
			}
		}
		if (old.kinematic && sameGeometry && !rebuild && old.assembly >= 0 &&
			assemblies[old.assembly].parts.size() == 1) {
			next.assembly = old.assembly;
			next.relative = old.relative;
			next.leaf = old.leaf;
			Assembly& a = assemblies[old.assembly];
			a.frame = a.publishedFrame = next.shape.frame;
			a.velocity = next.velocity;
			a.angular = next.angular;
			old = next;
			updateBounds(index);
			continue;
		}
		bool config = !old.active || old.anchored != next.anchored || !(old.shape.frame == next.shape.frame) ||
					  !(old.shape.size == next.shape.size) || old.shape.type != next.shape.type;
		for (int i = 0; i < 6; ++i) {
			if (old.surfaces[i] != next.surfaces[i]) {
				config = true;
			}
		}
		if (config) {
			bool joining = !old.active || !(old.shape.size == next.shape.size) || old.shape.type != next.shape.type;
			for (int i = 0; i < 6; ++i) {
				if (old.surfaces[i] != next.surfaces[i]) {
					joining = true;
				}
			}
			if (joining) {
				joiningCandidates.insert(id);
			}
			old = next;
			rebuild = true;
		}
		else {
			if (!(old.velocity == next.velocity) || !(old.angular == next.angular)) {
				if (old.assembly >= 0 && !rebuild) {
					Assembly& a = assemblies[old.assembly];
					a.velocity = next.velocity;
					a.angular = next.angular;
					a.sleeping = false;
					a.quiet = 0;
				}
				old.velocity = next.velocity;
				old.angular = next.angular;
			}
			old.canCollide = next.canCollide;
			old.friction = next.friction;
			old.elasticity = next.elasticity;
			old.revision = next.revision;
		}
	}
	if (rebuild) {
		rebuildAssemblies();
	}
}
int PhysicsWorld::buildTree(size_t begin, size_t end)
{
	int node = (int) tree.size();
	tree.push_back({});
	Aabb bounds = bodies[order[begin]].bounds;
	for (size_t i = begin + 1; i < end; ++i) {
		bounds = Aabb::combine(bounds, bodies[order[i]].bounds);
	}
	tree[node].bounds = bounds;
	if (end - begin == 1) {
		tree[node].body = order[begin];
		bodies[order[begin]].leaf = node;
		return node;
	}
	Vec3 e = bounds.high - bounds.low;
	int axis = e.x > e.y ? 0 : 1;
	if (e.z > (axis == 0 ? e.x : e.y)) {
		axis = 2;
	}
	auto center = [&](int body) -> float {
		const Aabb& a = bodies[body].bounds;
		return axis == 0 ? a.low.x + a.high.x : axis == 1 ? a.low.y + a.high.y : a.low.z + a.high.z;
	};
	size_t middle = (begin + end) / 2;
	std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end, [&](int a, int b) {
		return center(a) < center(b);
	});
	int left = buildTree(begin, middle), right = buildTree(middle, end);
	tree[node].left = left;
	tree[node].right = right;
	tree[left].parent = node;
	tree[right].parent = node;
	return node;
}
void PhysicsWorld::query(const Aabb& bounds, std::vector<int>& result) const
{
	result.clear();
	if (tree.empty()) {
		return;
	}
	int stack[64], count = 1;
	stack[0] = 0;
	while (count) {
		const Node& n = tree[stack[--count]];
		if (!n.bounds.overlaps(bounds, .01f)) {
			continue;
		}
		if (n.body >= 0) {
			result.push_back(n.body);
		}
		else {
			stack[count++] = n.left;
			stack[count++] = n.right;
		}
	}
}
void PhysicsWorld::rebuildAssemblies()
{
	++stats.rebuilds;
	// JointInstance coordinates constrain p0*C0 == p1*C1. Establish the rest
	// frames before surface joining and BVH construction, not just ID grouping.
	for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
		InstanceId id = model->idAt(slotIndex);
		const Instance* joint = model->get(id);
		if (!joint || !model->isDescendant(id, workspace)) {
			continue;
		}
		if (joint->className != "Weld" && joint->className != "Snap" && joint->className != "Glue") {
			continue;
		}
		InstanceId aid = joint->property("Part0")->reference, bid = joint->property("Part1")->reference;
		if (!body(aid) || !body(bid)) {
			continue;
		}
		Body& a = bodies[byId[DataModel::slot(aid)]];
		Body& b = bodies[byId[DataModel::slot(bid)]];
		CFrame c0 = propertyFrame(*joint->property("C0")), c1 = propertyFrame(*joint->property("C1"));
		if (!b.anchored && !b.kinematic) {
			CFrame frame = a.shape.frame * c0 * c1.inverse();
			if (!(b.shape.frame == frame)) {
				b.shape.frame = frame;
				b.dirty = true;
			}
		}
		else if (!a.anchored && !a.kinematic) {
			CFrame frame = b.shape.frame * c1 * c0.inverse();
			if (!(a.shape.frame == frame)) {
				a.shape.frame = frame;
				a.dirty = true;
			}
		}
	}
	order.clear();
	tree.clear();
	for (size_t i = 0; i < bodies.size(); ++i) {
		if (bodies[i].active) {
			bodies[i].bounds = bodies[i].shape.bounds();
			order.push_back((int) i);
		}
	}
	if (!order.empty()) {
		tree.reserve(order.size() * 2);
		buildTree(0, order.size());
	}
	std::vector<int> parent(bodies.size());
	std::iota(parent.begin(), parent.end(), 0);
	auto root = [&](int i) {
		while (parent[i] != i) {
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	};
	auto join = [&](int a, int b) {
		int x = root(a), y = root(b);
		if (x != y) {
			parent[y] = x;
			++stats.joints;
		}
	};
	stats.joints = 0;
	for (auto it = autoLinks.begin(); it != autoLinks.end();) {
		InstanceId a = (InstanceId) (*it >> 32), b = (InstanceId) *it;
		if (!body(a) || !body(b) || body(a)->kinematic || body(b)->kinematic ||
			!surfaceJoint(body(a)->shape, body(b)->shape, body(a)->surfaces, body(b)->surfaces)) {
			it = autoLinks.erase(it);
			continue;
		}
		join(byId[DataModel::slot(a)], byId[DataModel::slot(b)]);
		++it;
	}
	for (size_t i = 0; i < bodies.size(); ++i) {
		const Body& a = bodies[i];
		if (!a.active || a.kinematic) {
			continue;
		}
		Aabb expanded = a.bounds;
		expanded.low = expanded.low - Vec3(.05f, .05f, .05f);
		expanded.high = expanded.high + Vec3(.05f, .05f, .05f);
		query(expanded, candidates);
		for (int j : candidates) {
			if (j <= (int) i || bodies[j].kinematic) {
				continue;
			}
			// Rebuilding after unrelated removals must not weld previously loose
			// bodies just because they have since landed on a studded floor.
			if (!joiningCandidates.count(a.id) && !joiningCandidates.count(bodies[j].id)) {
				continue;
			}
			if (surfaceJoint(a.shape, bodies[j].shape, a.surfaces, bodies[j].surfaces)) {
				autoLinks.insert(pairKey(a.id, bodies[j].id));
				join((int) i, j);
			}
		}
	}
	for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
		InstanceId id = model->idAt(slotIndex);
		const Instance* joint = model->get(id);
		if (!joint || !model->isDescendant(id, workspace)) {
			continue;
		}
		if (joint->className != "Weld" && joint->className != "Snap" && joint->className != "Glue") {
			continue;
		}
		InstanceId a = joint->property("Part0")->reference, b = joint->property("Part1")->reference;
		if (body(a) && body(b) && !body(a)->kinematic && !body(b)->kinematic) {
			join(byId[DataModel::slot(a)], byId[DataModel::slot(b)]);
		}
	}
	std::vector<Assembly> previousAssemblies;
	previousAssemblies.swap(assemblies);
	std::vector<int> previousByFirst(bodies.size(), -1);
	for (size_t i = 0; i < previousAssemblies.size(); ++i) {
		if (!previousAssemblies[i].parts.empty()) {
			previousByFirst[previousAssemblies[i].parts.front()] = (int) i;
		}
	}
	std::vector<int> assemblyForRoot(bodies.size(), -1);
	for (size_t i = 0; i < bodies.size(); ++i) {
		Body& b = bodies[i];
		if (!b.active) {
			b.assembly = -1;
			continue;
		}
		int r = root((int) i), group = assemblyForRoot[r];
		if (group < 0) {
			group = (int) assemblies.size();
			assemblyForRoot[r] = group;
			assemblies.push_back({});
			assemblies.back().frame = b.shape.frame;
		}
		b.assembly = group;
		assemblies[group].parts.push_back((int) i);
	}
	for (Assembly& a : assemblies) {
		float mass = 0;
		Vec3 center, velocity, angular;
		for (int index : a.parts) {
			const Body& b = bodies[index];
			float m = b.shape.size.x * b.shape.size.y * b.shape.size.z;
			mass += m;
			center = center + b.shape.frame.position * m;
			velocity = velocity + b.velocity * m;
			angular = angular + b.angular * m;
			a.fixed = a.fixed || b.anchored || b.kinematic;
		}
		a.frame.position = center * (1 / mass);
		a.velocity = velocity * (1 / mass);
		a.angular = angular * (1 / mass);
		a.inverseMass = a.fixed ? 0 : 1 / mass;
		a.publishedFrame = a.frame;
		int previous = previousByFirst[a.parts.front()];
		if (previous >= 0) {
			const Assembly& old = previousAssemblies[previous];
			bool same = old.parts == a.parts && old.fixed == a.fixed &&
						length(old.frame.position - a.frame.position) < .001f &&
						length(old.velocity - a.velocity) < .0001f && length(old.angular - a.angular) < .0001f;
			for (int i = 0; i < 9 && same; ++i) {
				if (std::fabs(old.frame.rotation[i] - a.frame.rotation[i]) > 1e-5f) {
					same = false;
				}
			}
			if (same) {
				a.sleeping = old.sleeping;
				a.quiet = old.quiet;
			}
		}
		for (int index : a.parts) {
			bodies[index].relative = a.frame.inverse() * bodies[index].shape.frame;
		}
	}
	stats.parts = order.size();
	joiningCandidates.clear();
	stats.assemblies = assemblies.size();
	rebuild = false;
}
void PhysicsWorld::updateBounds(int index)
{
	int node = bodies[index].leaf;
	if (node < 0) {
		return;
	}
	tree[node].bounds = bodies[index].bounds;
	for (int parent = tree[node].parent; parent >= 0; parent = tree[parent].parent) {
		tree[parent].bounds = Aabb::combine(tree[tree[parent].left].bounds, tree[tree[parent].right].bounds);
	}
}
void PhysicsWorld::updateAssembly(size_t group)
{
	Assembly& a = assemblies[group];
	bool rotated = false;
	for (int i = 0; i < 9; ++i) {
		if (a.frame.rotation[i] != a.publishedFrame.rotation[i]) {
			rotated = true;
			break;
		}
	}
	Vec3 translation = a.frame.position - a.publishedFrame.position;
	bool moved = rotated || !(translation == Vec3());
	for (int index : a.parts) {
		Body& b = bodies[index];
		CFrame frame = b.shape.frame;
		if (rotated) {
			frame = a.frame * b.relative;
		}
		else {
			frame.position = frame.position + translation;
		}
		Vec3 velocity =
			a.angular == Vec3() ? a.velocity : a.velocity + cross(a.angular, frame.position - a.frame.position);
		if (!(frame == b.shape.frame) || !(velocity == b.velocity) || !(a.angular == b.angular)) {
			b.dirty = true;
		}
		b.shape.frame = frame;
		if (moved) {
			if (rotated) {
				b.bounds = b.shape.bounds();
			}
			else {
				b.bounds.low = b.bounds.low + translation;
				b.bounds.high = b.bounds.high + translation;
			}
			updateBounds(index);
		}
		b.velocity = velocity;
		b.angular = a.angular;
	}
	a.publishedFrame = a.frame;
}
void PhysicsWorld::tick(float dt)
{
	for (size_t i = 0; i < assemblies.size(); ++i) {
		Assembly& a = assemblies[i];
		a.supported = false;
		if (a.fixed || a.sleeping) {
			continue;
		}
		// SimBody::step is semi-implicit Euler. Units converts -9.81 m/s^2 by
		// 20 studs/m. Angular damping is time-scaled from its 4560 Hz kernel.
		a.velocity.y -= 196.2f * dt;
		a.frame.position = a.frame.position + a.velocity * dt;
		float speed = length(a.angular);
		if (speed > 1e-7f) {
			CFrame rotation = CFrame::axisAngle(a.angular * (1 / speed), speed * dt);
			CFrame orientation = a.frame;
			orientation.position = Vec3();
			orientation = rotation * orientation;
			std::copy(orientation.rotation, orientation.rotation + 9, a.frame.rotation);
			a.angular = a.angular * std::exp(-.91209f * dt);
		}
		updateAssembly(i);
	}
	for (int iteration = 0; iteration < 4; ++iteration) {
		for (size_t i = 0; i < bodies.size(); ++i) {
			Body& a = bodies[i];
			if (!a.active) {
				continue;
			}
			Assembly& first = assemblies[a.assembly];
			if (first.fixed || first.sleeping) {
				continue;
			}
			query(a.bounds, candidates);
			for (int j : candidates) {
				Body& b = bodies[j];
				if (a.assembly == b.assembly) {
					continue;
				}
				Assembly& second = assemblies[b.assembly];
				if (!second.fixed && !second.sleeping && j < (int) i) {
					continue;
				}
				Contact c;
				++stats.narrowTests;
				if (!intersect(a.shape, b.shape, c, .002f)) {
					continue;
				}
				touching.insert(pairKey(a.id, b.id));
				if (!a.canCollide || !b.canCollide) {
					continue;
				}
				Vec3 relative = first.velocity - second.velocity;
				float speed = dot(relative, c.normal);
				if (!second.fixed && speed < -2) {
					second.sleeping = false;
					second.quiet = 0;
				}
				float inverseSecond = second.sleeping ? 0 : second.inverseMass;
				float total = first.inverseMass + inverseSecond;
				if (total <= 0) {
					continue;
				}
				if (c.normal.y > .5f) {
					first.supported = true;
				}
				if (c.normal.y < -.5f) {
					second.supported = true;
				}
				float move = std::max(0.f, c.depth - .001f) * .8f / total;
				first.frame.position = first.frame.position + c.normal * (move * first.inverseMass);
				second.frame.position = second.frame.position - c.normal * (move * inverseSecond);
				if (speed < 0) {
					float restitution = speed < -2 ? std::min(a.elasticity, b.elasticity) : 0;
					float impulse = -(1 + restitution) * speed / total;
					Vec3 change = c.normal * impulse;
					Vec3 tangent = relative - c.normal * speed;
					float slip = length(tangent);
					if (slip > 1e-6f) {
						change =
							change -
							tangent * (std::min(slip / total, std::sqrt(a.friction * b.friction) * impulse) / slip);
					}
					first.velocity = first.velocity + change * first.inverseMass;
					second.velocity = second.velocity - change * inverseSecond;
				}
				updateAssembly((size_t) a.assembly);
				if (!second.fixed && !second.sleeping) {
					updateAssembly((size_t) b.assembly);
				}
			}
		}
	}
	for (Assembly& a : assemblies) {
		if (a.fixed || a.sleeping) {
			continue;
		}
		if (a.supported && length(a.velocity) < .06f && length(a.angular) < .06f) {
			a.quiet += dt;
			if (a.quiet > .5f) {
				a.sleeping = true;
				a.velocity = a.angular = Vec3();
				for (int index : a.parts) {
					bodies[index].velocity = bodies[index].angular = Vec3();
				}
			}
		}
		else {
			a.quiet = 0;
		}
	}
	++stats.steps;
	simulatedTime += Step;
}
void PhysicsWorld::advance(double seconds)
{
	if (!model || !std::isfinite(seconds) || seconds < 0) {
		return;
	}
	if (rebuild) {
		rebuildAssemblies();
	}
	stats.steps = stats.narrowTests = stats.contacts = 0;
	touching.clear();
	accumulator += seconds;
	bool moving = false;
	for (const Assembly& a : assemblies) {
		if (!a.fixed && !a.sleeping) {
			moving = true;
			break;
		}
	}
	if (!moving) {
		size_t steps = (size_t) std::min(240.0, std::floor((accumulator + 1e-10) / Step));
		stats.steps = steps;
		stats.awake = 0;
		stats.contacts = previousTouching.size();
		accumulator -= steps * Step;
		simulatedTime += steps * Step;
		return;
	}
	// Retain rather than discard time if a debugger stall or slow frame creates
	// a large backlog. This bound limits one UI call, not the simulation clock.
	while (accumulator + 1e-10 >= Step && stats.steps < 240) {
		tick((float) Step);
		accumulator -= Step;
	}
	stats.awake = 0;
	for (const Assembly& a : assemblies) {
		if (!a.fixed && !a.sleeping) {
			++stats.awake;
		}
	}
	stats.contacts = touching.size();
	if (stats.steps) {
		if (model->signal) {
			for (uint64_t pair : touching) {
				if (!previousTouching.count(pair)) {
					InstanceId a = (InstanceId) (pair >> 32), b = (InstanceId) pair;
					model->signal(a, "Touched", {b});
					model->signal(b, "Touched", {a});
				}
			}
		}
		previousTouching = touching;
	}
}
void PhysicsWorld::publish()
{
	if (!model) {
		return;
	}
	for (Body& b : bodies) {
		if (!b.active || !b.dirty) {
			continue;
		}
		Instance* object = model->get(b.id);
		if (!object) {
			continue;
		}
		if (!(propertyFrame(*object->property("CFrame")) == b.shape.frame)) {
			model->setProperty(b.id, frameProperty("CFrame", b.shape.frame));
		}
		if (!(propertyVector(*object->property("Velocity")) == b.velocity)) {
			model->setProperty(b.id, vectorProperty("Velocity", b.velocity));
		}
		if (!(propertyVector(*object->property("RotVelocity")) == b.angular)) {
			model->setProperty(b.id, vectorProperty("RotVelocity", b.angular));
		}
		b.revision = object->revision;
		b.dirty = false;
	}
}
size_t PhysicsWorld::bytes() const
{
	size_t size = bodies.capacity() * sizeof(Body) +
				  (byId.capacity() + order.capacity() + candidates.capacity()) * sizeof(int) +
				  tree.capacity() * sizeof(Node) + assemblies.capacity() * sizeof(Assembly);
	for (const Assembly& a : assemblies) {
		size += a.parts.capacity() * sizeof(int);
	}
	return size + jointRevisions.capacity() * sizeof(uint64_t) +
		   (touching.size() + previousTouching.size() + autoLinks.size()) * sizeof(uint64_t) * 5;
}
} // namespace openrbx

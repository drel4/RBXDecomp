#include "player.h"

#include "scene.h"
#include "schema.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace openrbx {
static float number(const Instance* object, const char* name)
{
	if (!object) {
		return 0;
	}
	const Property* p = object->property(name);
	return p ? std::strtof(p->text.c_str(), nullptr) : 0;
}
static void reference(DataModel& model, InstanceId id, const char* name, InstanceId value)
{
	Property p(name, "Ref");
	p.reference = value;
	model.setProperty(id, std::move(p));
}
static CFrame normalFrame(int normal, Vec3 position)
{
	CFrame f(position);
	// NormalId::uvwToObject: X=(z,y,-x), -X=(-z,y,x), Y=(-x,z,y).
	const float x[] = {0, 0, 1, 0, 1, 0, -1, 0, 0}, xn[] = {0, 0, -1, 0, 1, 0, 1, 0, 0},
				y[] = {-1, 0, 0, 0, 0, 1, 0, 1, 0};
	const float* values = normal == 0 ? x : normal == 3 ? xn : y;
	std::copy(values, values + 9, f.rotation);
	return f;
}
InstanceId LocalPlayer::asset(const std::string& path)
{
	Scene source;
	std::string error;
	if (!loadPlace(path.c_str(), source, error, true)) {
		throw std::runtime_error(path + ": " + error);
	}
	const auto& children = source.model.get(source.model.root())->children;
	if (children.size() != 1) {
		throw std::runtime_error("Character asset must have one root");
	}
	return model->import(source.model, children.front());
}
void LocalPlayer::open(DataModel& data, PhysicsWorld& physics, const std::string& content, bool spawn)
{
	close();
	model = &data;
	world = &physics;
	players = model->service("Players");
	workspace = model->service("Workspace");
	templateId = asset(content + "/character.rbxm");
	model->retain(templateId);
	InstanceId animate = asset(content + "/humanoidAnimate.rbxm"), health = asset(content + "/humanoidHealth.rbxm");
	model->setParent(animate, templateId);
	model->setParent(health, templateId);
	model->action = [this](InstanceId id, std::string_view method, const std::vector<SignalValue>& args) {
		return invoke(id, method, args);
	};
	if (spawn) {
		create();
		loadCharacter();
	}
}
void LocalPlayer::close()
{
	if (model) {
		retireRig();
		retainRig(false);
		model->release(playerId);
		model->release(templateId);
		model->action = nullptr;
		if (characterId && model->get(characterId)) {
			model->remove(characterId);
		}
		if (playerId && model->get(playerId)) {
			model->remove(playerId);
		}
		if (players && model->get(players)) {
			reference(*model, players, "LocalPlayer", NoInstance);
		}
	}
	model = nullptr;
	world = nullptr;
	players = workspace = templateId = playerId = characterId = humanoidId = NoInstance;
	std::fill(limbs, limbs + 6, NoInstance);
	std::fill(joints, joints + 5, NoInstance);
	std::fill(angles, angles + 5, 0);
	root = lastPublished = CFrame();
	motion = desired = Vec3();
	grounded = jump = dying = false;
	accumulator = respawnLeft = 0;
	state = Freefall;
	touches.clear();
	previousTouches.clear();
	std::vector<int>().swap(candidates);
}
void LocalPlayer::retainRig(bool retain)
{
	if (rigRetained == retain) {
		return;
	}
	auto update = [&](InstanceId id) {
		if (retain) {
			model->retain(id);
		}
		else {
			model->release(id);
		}
	};
	update(characterId);
	update(humanoidId);
	for (InstanceId id : limbs) {
		update(id);
	}
	for (InstanceId id : joints) {
		update(id);
	}
	rigRetained = retain;
}
void LocalPlayer::retireRig()
{
	if (!rigRetained) {
		return;
	}
	model->retire(characterId);
	model->retire(humanoidId);
	for (InstanceId id : limbs) {
		model->retire(id);
	}
	for (InstanceId id : joints) {
		model->retire(id);
	}
}
InstanceId LocalPlayer::create(int userId)
{
	if (playerId && model->get(playerId) && model->get(playerId)->parent == players) {
		return playerId;
	}
	model->release(playerId);
	playerId = model->create("Player");
	model->retain(playerId);
	model->setName(playerId, "Player");
	model->setProperty(playerId, {"userId", "int", std::to_string(userId)});
	model->setParent(playerId, players);
	reference(*model, players, "LocalPlayer", playerId);
	if (model->signal) {
		model->signal(players, "PlayerAdded", {playerId});
	}
	return playerId;
}
void LocalPlayer::buildJoints()
{
	static const char* names[] = {"Right Shoulder", "Left Shoulder", "Right Hip", "Left Hip", "Neck"};
	const CFrame c0[] = {
		normalFrame(0, Vec3(1, .5f, 0)),
		normalFrame(3, Vec3(-1, .5f, 0)),
		normalFrame(0, Vec3(1, -1, 0)),
		normalFrame(3, Vec3(-1, -1, 0)),
		normalFrame(1, Vec3(0, 1, 0))
	};
	const CFrame c1[] = {
		normalFrame(0, Vec3(-.5f, .5f, 0)),
		normalFrame(3, Vec3(.5f, .5f, 0)),
		normalFrame(0, Vec3(.5f, 1, 0)),
		normalFrame(3, Vec3(-.5f, 1, 0)),
		normalFrame(1, Vec3(0, -.5f, 0))
	};
	for (int i = 0; i < 5; ++i) {
		joints[i] = model->create(i == 4 ? "Snap" : "Motor", limbs[0]);
		model->setName(joints[i], names[i]);
		reference(*model, joints[i], "Part0", limbs[0]);
		reference(*model, joints[i], "Part1", limbs[i == 4 ? 1 : i + 2]);
		model->setProperty(joints[i], frameProperty("C0", c0[i]));
		model->setProperty(joints[i], frameProperty("C1", c1[i]));
	}
}
void LocalPlayer::loadCharacter()
{
	if (!playerId || !model->get(playerId) || model->get(playerId)->parent != players) {
		throw std::runtime_error("LoadCharacter requires the local Player in Players");
	}
	retireRig();
	if (characterId && model->get(characterId)) {
		model->remove(characterId);
	}
	retainRig(false);
	characterId = model->clone(templateId);
	model->setName(characterId, model->get(playerId)->name);
	static const char* names[] = {"Torso", "Head", "Right Arm", "Left Arm", "Right Leg", "Left Leg"};
	for (int i = 0; i < 6; ++i) {
		limbs[i] = model->findChild(characterId, names[i]);
		if (!limbs[i]) {
			throw std::runtime_error("Character rig is missing body part");
		}
	}
	humanoidId = model->findChild(characterId, "Humanoid");
	if (!humanoidId) {
		throw std::runtime_error("Character rig is missing Humanoid");
	}
	buildJoints();
	retainRig(true);
	std::fill(angles, angles + 5, 0);
	root = CFrame(Vec3(0, 20, 25));
	for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
		InstanceId id = model->idAt(slotIndex);
		const Instance* object = model->get(id);
		if (object && object->className == "SpawnLocation" && model->isDescendant(id, workspace)) {
			CFrame spawn = propertyFrame(*object->property("CFrame"));
			root = spawn;
			root.position.y += propertyVector(*object->property("size")).y * .5f + 3.05f;
			break;
		}
	}
	motion = Vec3();
	grounded = false;
	dying = false;
	state = Freefall;
	respawnLeft = 0;
	accumulator = 0;
	touches.clear();
	previousTouches.clear();
	publish(0);
	model->setParent(characterId, workspace);
	reference(*model, playerId, "Character", characterId);
	world->synchronize();
	for (InstanceId id : limbs) {
		world->setKinematic(id, true);
	}
	world->synchronize();
	if (model->signal) {
		model->signal(playerId, "CharacterAdded", {characterId});
	}
}
SignalValue LocalPlayer::invoke(InstanceId object, std::string_view method, const std::vector<SignalValue>& args)
{
	const Instance* instance = model->get(object);
	if (!instance) {
		throw std::runtime_error("Instance no longer exists");
	}
	if (method == "CreateLocalPlayer" || method == "createLocalPlayer") {
		if (object != players) {
			throw std::runtime_error("CreateLocalPlayer requires Players");
		}
		return create((int) std::get<double>(args.at(0)));
	}
	if (method == "GetPlayerFromCharacter" || method == "getPlayerFromCharacter") {
		if (object != players) {
			throw std::runtime_error("GetPlayerFromCharacter requires Players");
		}
		InstanceId character = std::get<InstanceId>(args.at(0));
		for (InstanceId id : model->get(players)->children) {
			const Instance* p = model->get(id);
			if (p->className == "Player" && p->property("Character")->reference == character) {
				return id;
			}
		}
		return NoInstance;
	}
	if (method == "LoadCharacter" || method == "loadCharacter") {
		if (object != playerId) {
			throw std::runtime_error("LoadCharacter supports the local Player");
		}
		loadCharacter();
		return {};
	}
	if (method == "TakeDamage" || method == "takeDamage") {
		if (instance->className != "Humanoid") {
			throw std::runtime_error("TakeDamage requires Humanoid");
		}
		float value = number(instance, "Health") - (float) std::get<double>(args.at(0));
		model->setProperty(object, {"Health", "float", std::to_string(value)});
		return {};
	}
	throw std::runtime_error("Unsupported local-game action");
}
void LocalPlayer::input(Vec3 movement, bool heldJump)
{
	movement.y = 0;
	float magnitude = length(movement);
	desired = magnitude > 1 ? movement * (1 / magnitude) : movement;
	jump = heldJump;
}
bool LocalPlayer::ownPart(InstanceId id) const
{
	for (InstanceId part : limbs) {
		if (part == id) {
			return true;
		}
	}
	return false;
}
bool LocalPlayer::blocked(const CollisionShape& shape) const
{
	std::vector<int> matches;
	world->query(shape.bounds(), matches);
	for (int index : matches) {
		const auto& body = world->bodyAt(index);
		if (ownPart(body.id) || !body.canCollide) {
			continue;
		}
		Contact c;
		if (intersect(shape, body.shape, c) && c.depth > .002f) {
			return true;
		}
	}
	return false;
}
void LocalPlayer::emit(std::string_view event, const std::vector<SignalValue>& args)
{
	if (model->signal && humanoidId) {
		model->signal(humanoidId, event, args);
	}
}
void LocalPlayer::beginDeath()
{
	if (dying) {
		return;
	}
	dying = true;
	state = Dead;
	respawnLeft = 3;
	if (number(model->get(humanoidId), "Health") > 0) {
		model->setProperty(humanoidId, {"Health", "float", "0"});
	}
	for (InstanceId id : limbs) {
		world->setKinematic(id, false);
	}
}
void LocalPlayer::tick(float dt)
{
	if (dying) {
		return;
	}
	float speed = std::max(0.f, number(model->get(humanoidId), "WalkSpeed"));
	motion.x = desired.x * speed;
	motion.z = desired.z * speed;
	if (length(desired) > .001f) {
		CFrame turn = CFrame::lookAt(root.position, root.position + desired);
		std::copy(turn.rotation, turn.rotation + 9, root.rotation);
	}
	const Property* requested = model->get(humanoidId)->property("Jump");
	bool wantJump = jump || (requested && requested->text == "true");
	if (wantJump && grounded) {
		motion.y = 50;
		grounded = false;
		state = Jumping;
		emit("Jumping", {true});
		model->setProperty(humanoidId, {"Jump", "bool", "false"});
	}
	bool wasGrounded = grounded;
	motion.y -= 196.2f * dt;
	root.position = root.position + motion * dt;
	grounded = false;
	CollisionShape shape{root, Vec3(2, 5, 1.2f), 1};
	shape.frame.position.y -= .5f;
	for (int iteration = 0; iteration < 4; ++iteration) {
		world->query(shape.bounds(), candidates);
		bool corrected = false;
		for (int index : candidates) {
			const auto& other = world->bodyAt(index);
			if (ownPart(other.id)) {
				continue;
			}
			Contact c;
			if (!intersect(shape, other.shape, c, .002f)) {
				continue;
			}
			InstanceId hit = c.normal.y > .5f ? limbs[4] : limbs[0];
			touches.insert((uint64_t) other.id << 32 | hit);
			if (!other.canCollide) {
				continue;
			}
			if (c.normal.y > .55f) {
				grounded = true;
			}
			if (wasGrounded && c.normal.y < .5f && std::fabs(c.normal.y) < .3f && motion.y <= 0 &&
				length(desired) > .001f) {
				CollisionShape raised = shape;
				raised.frame.position.y += 1.05f;
				if (!blocked(raised)) {
					shape = raised;
					root.position.y += 1.05f;
					corrected = true;
					continue;
				}
			}
			if (c.depth > 0) {
				shape.frame.position = shape.frame.position + c.normal * (c.depth + .0001f);
				root.position = shape.frame.position + Vec3(0, .5f, 0);
				corrected = true;
			}
			float into = dot(motion - other.velocity, c.normal);
			if (into < 0) {
				motion = motion - c.normal * into;
			}
		}
		if (!corrected) {
			break;
		}
	}
	if (root.position.y < -500) {
		beginDeath();
		return;
	}
	if (grounded) {
		if (state == Freefall) {
			emit("FreeFalling", {false});
		}
		if (state == Jumping || state == Freefall) {
			emit("Jumping", {false});
		}
		state = Running;
	}
	else if (state == Running || (state == Jumping && motion.y <= 0)) {
		state = Freefall;
		emit("FreeFalling", {true});
	}
}
void LocalPlayer::publish(float dt)
{
	if (!characterId || dying) {
		return;
	}
	model->setProperty(limbs[0], frameProperty("CFrame", root));
	for (int i = 0; i < 5; ++i) {
		const Instance* joint = model->get(joints[i]);
		if (!joint || !joint->parent) {
			continue;
		}
		if (i < 4) {
			float target = number(joint, "DesiredAngle"), maxStep = std::fabs(number(joint, "MaxVelocity")) * dt * 30;
			angles[i] += std::max(-maxStep, std::min(maxStep, target - angles[i]));
			model->setProperty(joints[i], {"CurrentAngle", "float", std::to_string(angles[i])});
		}
		CFrame part = root * propertyFrame(*joint->property("C0")) * CFrame::axisAngle(Vec3(0, 0, 1), angles[i]) *
					  propertyFrame(*joint->property("C1")).inverse();
		model->setProperty(limbs[i == 4 ? 1 : i + 2], frameProperty("CFrame", part));
	}
	for (InstanceId id : limbs) {
		model->setProperty(id, vectorProperty("Velocity", motion));
	}
	lastPublished = root;
}
void LocalPlayer::advance(double seconds)
{
	if (!model || !std::isfinite(seconds) || seconds < 0) {
		return;
	}
	if (!model->get(playerId) || model->get(playerId)->parent != players) {
		retireRig();
		if (characterId && model->get(characterId)) {
			model->remove(characterId);
		}
		reference(*model, players, "LocalPlayer", NoInstance);
		retainRig(false);
		model->release(playerId);
		playerId = characterId = humanoidId = NoInstance;
		return;
	}
	if (!model->get(characterId) || model->get(characterId)->parent != workspace) {
		if (!dying) {
			beginDeath();
		}
	}
	if (number(model->get(humanoidId), "Health") <= 0 && !dying) {
		beginDeath();
	}
	if (dying) {
		respawnLeft -= seconds;
		if (respawnLeft <= 0) {
			loadCharacter();
		}
		return;
	}
	CFrame external = propertyFrame(*model->get(limbs[0])->property("CFrame"));
	if (!(external == lastPublished)) {
		root = external;
		motion = propertyVector(*model->get(limbs[0])->property("Velocity"));
		grounded = false;
	}
	touches.clear();
	accumulator += seconds;
	size_t steps = 0;
	while (accumulator + 1e-10 >= PhysicsWorld::Step && steps < 240) {
		tick((float) PhysicsWorld::Step);
		accumulator -= PhysicsWorld::Step;
		++steps;
		if (dying) {
			break;
		}
	}
	if (steps && model->signal) {
		for (uint64_t pair : touches) {
			if (!previousTouches.count(pair)) {
				InstanceId other = (InstanceId) (pair >> 32), hit = (InstanceId) pair;
				model->signal(other, "Touched", {hit});
				model->signal(hit, "Touched", {other});
			}
		}
	}
	if (steps) {
		previousTouches = touches;
	}
	if (grounded) {
		emit("Running", {(double) length(Vec3(motion.x, 0, motion.z))});
	}
	publish((float) (steps * PhysicsWorld::Step));
}
} // namespace openrbx

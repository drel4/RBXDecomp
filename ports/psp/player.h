#pragma once
#include "physics.h"

namespace openrbx {
// Local game-session backend. Uses imported engine character/health/animation
// assets and real instance services; no place-specific or hardcoded map logic.
class LocalPlayer
{
public:
	void open(DataModel& model, PhysicsWorld& physics, const std::string& contentDirectory, bool spawn = true);
	void close();
	InstanceId create(int userId = 0);
	void loadCharacter();
	void input(Vec3 worldMovement, bool jump);
	void advance(double seconds);
	InstanceId player() const { return playerId; }
	InstanceId character() const { return characterId; }
	InstanceId humanoid() const { return humanoidId; }
	Vec3 position() const { return root.position; }
	Vec3 velocity() const { return motion; }
	bool onGround() const { return grounded; }
	bool alive() const { return characterId && !dying; }

private:
	DataModel* model = nullptr;
	PhysicsWorld* world = nullptr;
	InstanceId players = NoInstance, workspace = NoInstance, templateId = NoInstance;
	InstanceId playerId = NoInstance, characterId = NoInstance, humanoidId = NoInstance;
	InstanceId limbs[6] = {}, joints[5] = {}; // torso, head, right/left arms, right/left legs
	float angles[5] = {};
	CFrame root, lastPublished;
	Vec3 motion, desired;
	bool grounded = false, jump = false, dying = false;
	bool rigRetained = false;
	void retainRig(bool retain);
	void retireRig();
	enum State
	{
		Running,
		Jumping,
		Freefall,
		Dead
	};
	State state = Freefall;
	double accumulator = 0, respawnLeft = 0;
	std::vector<int> candidates;
	std::set<uint64_t> touches, previousTouches;
	SignalValue invoke(InstanceId object, std::string_view method, const std::vector<SignalValue>& args);
	void emit(std::string_view event, const std::vector<SignalValue>& args = {});
	void tick(float dt);
	void publish(float dt);
	void beginDeath();
	bool ownPart(InstanceId id) const;
	bool blocked(const CollisionShape& shape) const;
	void buildJoints();
	InstanceId asset(const std::string& path);
};
} // namespace openrbx

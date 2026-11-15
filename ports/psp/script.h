#pragma once

#include "runtime.h"

#include <list>

struct lua_State;
struct lua_Debug;
namespace openrbx {
// Portable ScriptContext adaptation: original Lua 5.1.1, per-script globals,
// heartbeat-driven coroutine wait/delay. This owns the VM and all coroutine refs.
class ScriptRuntime
{
public:
	ScriptRuntime();
	~ScriptRuntime();
	ScriptRuntime(const ScriptRuntime&) = delete;
	ScriptRuntime& operator=(const ScriptRuntime&) = delete;
	void open(DataModel& model);
	void close();
	void startPlaceScripts();
	void primePlaceScripts();
	bool execute(const std::string& source, InstanceId script = NoInstance);
	void step(double dt);
	size_t collect();
	void fireSignal(InstanceId sender, std::string_view name, const std::vector<SignalValue>& arguments = {});
	size_t threadCount() const { return tasks.size(); }
	size_t bytes() const { return memoryUsed; }
	size_t errorCount() const { return errors; }
	const std::vector<std::string>& messages() const { return output; }

private:
	struct Task
	{
		lua_State* thread;
		int reference;
		double wake, yieldedAt;
		bool waiting = false;
		InstanceId script;
		uint32_t serial;
		int pendingArguments = 0;
	};
	lua_State* state = nullptr;
	DataModel* model = nullptr;
	std::list<Task> tasks;
	std::vector<std::string> output;
	size_t memoryUsed = 0;
	size_t errors = 0;
	double time = 0;
	uint32_t nextSerial = 0;
	Task* active = nullptr;
	InstanceId runService = NoInstance;
	bool autoScripts = false;
	std::set<InstanceId> startedScripts;
	bool activeScript(InstanceId id) const;
	struct Connection
	{
		uint32_t serial;
		InstanceId sender, script;
		std::string event;
		int function;
		uint32_t waiter = 0;
	};
	std::list<Connection> connections;
	uint32_t nextConnection = 0;
	static void pushSignal(lua_State* thread, InstanceId sender, const char* name);
	static void pushArgument(lua_State* thread, const SignalValue& value);
	static int signalIndex(lua_State* thread);
	static int signalConnect(lua_State* thread);
	static int signalWait(lua_State* thread);
	static int connectionIndex(lua_State* thread);
	static int disconnect(lua_State* thread);
	static void* allocate(void* context, void* pointer, size_t oldSize, size_t newSize);
	static ScriptRuntime& context(lua_State* thread);
	static InstanceId instance(lua_State* thread, int index);
	static void pushInstance(lua_State* thread, InstanceId id);
	static int index(lua_State* thread);
	static int newIndex(lua_State* thread);
	static int method(lua_State* thread);
	static int construct(lua_State* thread);
	static int wait(lua_State* thread);
	static int delay(lua_State* thread);
	static int print(lua_State* thread);
	static int tick(lua_State* thread);
	static int equal(lua_State* thread);
	static int releaseInstance(lua_State* thread);
	static int releaseSignal(lua_State* thread);
	static void instructionLimit(lua_State* thread, lua_Debug* debug);
	void log(const std::string& text);
	Task& newTask(lua_State* parent, InstanceId script, double wake);
	void resume(Task& task, int arguments);
	void resumeReadyTasks();
};
} // namespace openrbx

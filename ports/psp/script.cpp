#include "script.h"

#include "schema.h"
#include "script_values.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <lauxlib.h>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <stdexcept>

namespace openrbx {
static const char* InstanceMeta = "OpenRBX.Instance";
static char ContextKey;
static const char* SignalMeta = "OpenRBX.Signal";
static const char* ConnectionMeta = "OpenRBX.Connection";
struct Signal
{
	InstanceId sender;
	const char* name;
};
ScriptRuntime::ScriptRuntime()
{
}
ScriptRuntime::~ScriptRuntime()
{
	close();
}
void* ScriptRuntime::allocate(void* context, void* pointer, size_t oldSize, size_t newSize)
{
	auto& self = *static_cast<ScriptRuntime*>(context);
	if (!pointer) {
		oldSize = 0;
	}
	if (!newSize) {
		std::free(pointer);
		self.memoryUsed -= oldSize;
		return nullptr;
	}
	if (newSize > 4 * 1024 * 1024 || self.memoryUsed - oldSize > 4 * 1024 * 1024 - newSize) {
		return nullptr;
	}
	void* result = std::realloc(pointer, newSize);
	if (result) {
		self.memoryUsed = self.memoryUsed - oldSize + newSize;
	}
	return result;
}
ScriptRuntime& ScriptRuntime::context(lua_State* thread)
{
	lua_pushlightuserdata(thread, &ContextKey);
	lua_rawget(thread, LUA_REGISTRYINDEX);
	auto* self = static_cast<ScriptRuntime*>(lua_touserdata(thread, -1));
	lua_pop(thread, 1);
	return *self;
}
void ScriptRuntime::open(DataModel& data)
{
	close();
	model = &data;
	runService = model->service("RunService");
	state = lua_newstate(allocate, this);
	if (!state) {
		throw std::runtime_error("Unable to allocate PSP Lua state");
	}
	lua_pushlightuserdata(state, &ContextKey);
	lua_pushlightuserdata(state, this);
	lua_rawset(state, LUA_REGISTRYINDEX);
	// Same base/math/string/table/coroutine VM facilities as the game engine;
	// no host process, dynamic-library loading or unrestricted file IO.
	luaopen_base(state);
	lua_settop(state, 0);
	luaopen_math(state);
	lua_settop(state, 0);
	luaopen_string(state);
	lua_settop(state, 0);
	luaopen_table(state);
	lua_settop(state, 0);
	lua_newtable(state);
	lua_setglobal(state, "shared");
	registerValueTypes(state);
	luaL_newmetatable(state, SignalMeta);
	lua_pushcfunction(state, releaseSignal);
	lua_setfield(state, -2, "__gc");
	lua_pushcfunction(state, signalIndex);
	lua_setfield(state, -2, "__index");
	lua_pushliteral(state, "RBXScriptSignal");
	lua_setfield(state, -2, "__metatable");
	lua_pop(state, 1);
	luaL_newmetatable(state, ConnectionMeta);
	lua_pushcfunction(state, connectionIndex);
	lua_setfield(state, -2, "__index");
	lua_pushliteral(state, "RBXScriptConnection");
	lua_setfield(state, -2, "__metatable");
	lua_pop(state, 1);
	model->signal = [this](InstanceId sender, std::string_view name, const std::vector<SignalValue>& args) {
		fireSignal(sender, name, args);
	};
	for (const char* name : {"dofile", "loadfile"}) {
		lua_pushnil(state);
		lua_setglobal(state, name);
	}
	luaL_newmetatable(state, InstanceMeta);
	lua_pushcfunction(state, releaseInstance);
	lua_setfield(state, -2, "__gc");
	lua_pushcfunction(state, index);
	lua_setfield(state, -2, "__index");
	lua_pushcfunction(state, newIndex);
	lua_setfield(state, -2, "__newindex");
	lua_pushcfunction(state, equal);
	lua_setfield(state, -2, "__eq");
	lua_pushliteral(state, "Instance");
	lua_setfield(state, -2, "__metatable");
	lua_pop(state, 1);
	lua_newtable(state);
	lua_pushcfunction(state, construct);
	lua_setfield(state, -2, "new");
	lua_setglobal(state, "Instance");
	pushInstance(state, model->root());
	lua_setglobal(state, "game");
	pushInstance(state, model->service("Workspace"));
	lua_setglobal(state, "workspace");
	lua_pushcfunction(state, wait);
	lua_setglobal(state, "wait");
	lua_pushcfunction(state, delay);
	lua_setglobal(state, "delay");
	lua_pushcfunction(state, print);
	lua_setglobal(state, "print");
	lua_pushcfunction(state, tick);
	lua_setglobal(state, "tick");
}
void ScriptRuntime::close()
{
	if (model) {
		model->signal = nullptr;
	}
	connections.clear();
	startedScripts.clear();
	autoScripts = false;
	nextConnection = 0;
	active = nullptr;
	tasks.clear();
	if (state) {
		lua_close(state);
	}
	state = nullptr;
	model = nullptr;
	output.clear();
	time = 0;
	nextSerial = 0;
	errors = 0;
}
InstanceId ScriptRuntime::instance(lua_State* thread, int index)
{
	return *static_cast<InstanceId*>(luaL_checkudata(thread, index, InstanceMeta));
}
void ScriptRuntime::pushInstance(lua_State* thread, InstanceId id)
{
	if (!id) {
		lua_pushnil(thread);
		return;
	}
	*static_cast<InstanceId*>(lua_newuserdata(thread, sizeof(InstanceId))) = id;
	luaL_getmetatable(thread, InstanceMeta);
	lua_setmetatable(thread, -2);
	context(thread).model->retain(id);
}
int ScriptRuntime::releaseInstance(lua_State* thread)
{
	context(thread).model->release(instance(thread, 1));
	return 0;
}
int ScriptRuntime::releaseSignal(lua_State* thread)
{
	auto* value = static_cast<Signal*>(luaL_checkudata(thread, 1, SignalMeta));
	context(thread).model->release(value->sender);
	return 0;
}
size_t ScriptRuntime::collect()
{
	if (!state) {
		return 0;
	}
	// Run after step has canceled detached script owners, so stale registry
	// coroutine/callback references cannot pin previous characters indefinitely.
	lua_gc(state, LUA_GCCOLLECT, 0);
	return model->collect();
}
int ScriptRuntime::equal(lua_State* thread)
{
	lua_pushboolean(thread, instance(thread, 1) == instance(thread, 2));
	return 1;
}
void ScriptRuntime::pushSignal(lua_State* thread, InstanceId sender, const char* name)
{
	*static_cast<Signal*>(lua_newuserdata(thread, sizeof(Signal))) = {sender, name};
	luaL_getmetatable(thread, SignalMeta);
	lua_setmetatable(thread, -2);
	context(thread).model->retain(sender);
}
int ScriptRuntime::signalIndex(lua_State* thread)
{
	const char* key = luaL_checkstring(thread, 2);
	if (!std::strcmp(key, "connect") || !std::strcmp(key, "Connect")) {
		lua_pushcfunction(thread, signalConnect);
	}
	else if (!std::strcmp(key, "wait") || !std::strcmp(key, "Wait")) {
		lua_pushcfunction(thread, signalWait);
	}
	else {
		return luaL_error(thread, "%s is not a signal member", key);
	}
	return 1;
}
int ScriptRuntime::signalConnect(lua_State* thread)
{
	auto& self = context(thread);
	auto value = *static_cast<Signal*>(luaL_checkudata(thread, 1, SignalMeta));
	luaL_checktype(thread, 2, LUA_TFUNCTION);
	if (self.connections.size() >= 1024) {
		return luaL_error(thread, "PSP signal connection budget exceeded");
	}
	lua_pushvalue(thread, 2);
	int function = luaL_ref(thread, LUA_REGISTRYINDEX);
	uint32_t serial = ++self.nextConnection;
	self.connections.push_back(
		{serial, value.sender, self.active ? self.active->script : NoInstance, value.name, function}
	);
	*static_cast<uint32_t*>(lua_newuserdata(thread, sizeof(uint32_t))) = serial;
	luaL_getmetatable(thread, ConnectionMeta);
	lua_setmetatable(thread, -2);
	return 1;
}
int ScriptRuntime::signalWait(lua_State* thread)
{
	auto& self = context(thread);
	Signal value = *static_cast<Signal*>(luaL_checkudata(thread, 1, SignalMeta));
	if (!self.active || self.active->thread != thread) {
		return luaL_error(thread, "Signal wait requires scheduled thread");
	}
	if (self.connections.size() >= 1024) {
		return luaL_error(thread, "PSP signal connection budget exceeded");
	}
	self.connections.push_back(
		{++self.nextConnection, value.sender, self.active->script, value.name, LUA_NOREF, self.active->serial}
	);
	self.active->wake = std::numeric_limits<double>::infinity();
	self.active->waiting = false;
	return lua_yield(thread, 0);
}
int ScriptRuntime::connectionIndex(lua_State* thread)
{
	auto& self = context(thread);
	uint32_t serial = *static_cast<uint32_t*>(luaL_checkudata(thread, 1, ConnectionMeta));
	const char* key = luaL_checkstring(thread, 2);
	if (!std::strcmp(key, "disconnect") || !std::strcmp(key, "Disconnect")) {
		lua_pushcfunction(thread, disconnect);
		return 1;
	}
	if (!std::strcmp(key, "connected") || !std::strcmp(key, "Connected")) {
		bool found = false;
		for (const auto& c : self.connections) {
			if (c.serial == serial) {
				found = true;
			}
		}
		lua_pushboolean(thread, found);
		return 1;
	}
	return luaL_error(thread, "%s is not a connection member", key);
}
int ScriptRuntime::disconnect(lua_State* thread)
{
	auto& self = context(thread);
	uint32_t serial = *static_cast<uint32_t*>(luaL_checkudata(thread, 1, ConnectionMeta));
	for (auto it = self.connections.begin(); it != self.connections.end(); ++it) {
		if (it->serial == serial) {
			if (it->function != LUA_NOREF) {
				luaL_unref(thread, LUA_REGISTRYINDEX, it->function);
			}
			self.connections.erase(it);
			break;
		}
	}
	return 0;
}
void ScriptRuntime::pushArgument(lua_State* thread, const SignalValue& value)
{
	if (const auto* id = std::get_if<InstanceId>(&value)) {
		pushInstance(thread, *id);
	}
	else if (const auto* text = std::get_if<std::string>(&value)) {
		lua_pushlstring(thread, text->data(), text->size());
	}
	else if (const auto* number = std::get_if<double>(&value)) {
		lua_pushnumber(thread, *number);
	}
	else if (const auto* boolean = std::get_if<bool>(&value)) {
		lua_pushboolean(thread, *boolean);
	}
	else if (const auto* vector = std::get_if<Vec3>(&value)) {
		pushVector(thread, *vector);
	}
	else if (const auto* frame = std::get_if<CFrame>(&value)) {
		pushFrame(thread, *frame);
	}
	else {
		lua_pushnil(thread);
	}
}
void ScriptRuntime::fireSignal(InstanceId sender, std::string_view name, const std::vector<SignalValue>& args)
{
	if (!state) {
		return;
	}
	for (auto it = connections.begin(); it != connections.end();) {
		Connection& c = *it;
		if (c.sender != sender || c.event != name) {
			++it;
			continue;
		}
		if (c.waiter) {
			for (Task& task : tasks) {
				if (task.serial == c.waiter) {
					for (const auto& value : args) {
						pushArgument(task.thread, value);
					}
					task.pendingArguments = (int) args.size();
					task.wake = time;
					task.waiting = false;
					break;
				}
			}
			it = connections.erase(it);
			continue;
		}
		if (tasks.size() >= 256) {
			log("Signal callback dropped: PSP thread budget exceeded");
			++it;
			continue;
		}
		Task& task = newTask(state, c.script, time);
		lua_rawgeti(state, LUA_REGISTRYINDEX, c.function);
		lua_xmove(state, task.thread, 1);
		for (const auto& value : args) {
			pushArgument(task.thread, value);
		}
		task.pendingArguments = (int) args.size();
		++it;
	}
}
static bool isMethod(const char* name)
{
	for (const char* method :
		 {"FindFirstChild",
		  "findFirstChild",
		  "GetChildren",
		  "children",
		  "Clone",
		  "clone",
		  "Remove",
		  "remove",
		  "GetService",
		  "getService",
		  "service",
		  "FindService",
		  "IsDescendantOf",
		  "isDescendentOf",
		  "MakeJoints",
		  "makeJoints",
		  "BreakJoints",
		  "breakJoints",
		  "CreateLocalPlayer",
		  "createLocalPlayer",
		  "LoadCharacter",
		  "loadCharacter",
		  "GetPlayerFromCharacter",
		  "getPlayerFromCharacter",
		  "TakeDamage",
		  "takeDamage",
		  "IsA"}) {
		if (!std::strcmp(name, method)) {
			return true;
		}
	}
	return false;
}
int ScriptRuntime::index(lua_State* thread)
{
	auto& self = context(thread);
	Instance* object = self.model->get(instance(thread, 1));
	const char* key = luaL_checkstring(thread, 2);
	if (!object) {
		return luaL_error(thread, "Instance no longer exists");
	}
	if (isMethod(key)) {
		lua_pushvalue(thread, 2);
		lua_pushcclosure(thread, method, 1);
		return 1;
	}
	if (const char* event = signalName(object->className, key)) {
		pushSignal(thread, instance(thread, 1), event);
		return 1;
	}
	if (!std::strcmp(key, "Name")) {
		lua_pushlstring(thread, object->name.data(), object->name.size());
	}
	else if (!std::strcmp(key, "ClassName") || !std::strcmp(key, "className")) {
		lua_pushlstring(thread, object->className.data(), object->className.size());
	}
	else if (!std::strcmp(key, "Parent")) {
		pushInstance(thread, object->parent);
	}
	else if (!std::strcmp(key, "Position") && classIsA(object->className, "Part")) {
		pushVector(thread, propertyFrame(*object->property("CFrame")).position);
	}
	else if (const Property* p = object->property(key)) {
		if (p->type == "Ref") {
			pushInstance(thread, p->reference);
		}
		else if (p->type == "bool") {
			lua_pushboolean(thread, p->text == "true");
		}
		else if (p->type == "int" || p->type == "float" || p->type == "double" || p->type == "token") {
			lua_pushnumber(thread, std::strtod(p->text.c_str(), nullptr));
		}
		else if (p->type == "Vector3") {
			pushVector(thread, propertyVector(*p));
		}
		else if (p->type == "CoordinateFrame") {
			pushFrame(thread, propertyFrame(*p));
		}
		else if (!p->components.empty()) {
			return luaL_error(thread, "Structured property binding pending: %s", key);
		}
		else {
			lua_pushlstring(thread, p->text.data(), p->text.size());
		}
	}
	else {
		InstanceId child = self.model->findChild(instance(thread, 1), key);
		if (!child) {
			return luaL_error(thread, "%s is not a valid member", key);
		}
		pushInstance(thread, child);
	}
	return 1;
}
int ScriptRuntime::newIndex(lua_State* thread)
{
	auto& self = context(thread);
	InstanceId id = instance(thread, 1);
	Instance* object = self.model->get(id);
	const char* key = luaL_checkstring(thread, 2);
	if (!object) {
		return luaL_error(thread, "Instance no longer exists");
	}
	if (!std::strcmp(key, "Name")) {
		self.model->setName(id, luaL_checkstring(thread, 3));
		return 0;
	}
	if (!std::strcmp(key, "Parent")) {
		InstanceId parent = lua_isnil(thread, 3) ? NoInstance : instance(thread, 3);
		self.model->setParent(id, parent);
		return 0;
	}
	if (!std::strcmp(key, "Position") && classIsA(object->className, "Part")) {
		CFrame f = propertyFrame(*object->property("CFrame"));
		f.position = checkVector(thread, 3);
		self.model->setProperty(id, frameProperty("CFrame", f));
		return 0;
	}
	if (const Property* existing = object->property(key)) {
		Property p = *existing;
		if (p.type == "Ref") {
			p.reference = lua_isnil(thread, 3) ? NoInstance : instance(thread, 3);
		}
		else if (p.type == "bool") {
			luaL_checktype(thread, 3, LUA_TBOOLEAN);
			p.text = lua_toboolean(thread, 3) ? "true" : "false";
		}
		else if (p.type == "int" || p.type == "float" || p.type == "double" || p.type == "token") {
			double value = luaL_checknumber(thread, 3);
			if (!std::isfinite(value)) {
				return luaL_error(thread, "Property must be finite");
			}
			p.text = lua_tostring(thread, 3);
			if (p.type == "int" || p.type == "token") {
				if (value < -2147483648.0 || value > 2147483647.0) {
					return luaL_error(thread, "Integer property out of range");
				}
				p.text = std::to_string((int32_t) value);
			}
		}
		else if (p.type == "Vector3") {
			p = vectorProperty(p.name, checkVector(thread, 3));
		}
		else if (p.type == "CoordinateFrame") {
			p = frameProperty(p.name, checkFrame(thread, 3));
		}
		else if (!p.components.empty()) {
			return luaL_error(thread, "Structured property binding pending: %s", key);
		}
		else {
			size_t size;
			const char* text = luaL_checklstring(thread, 3, &size);
			if (size > 256 * 1024) {
				return luaL_error(thread, "Property exceeds PSP text budget");
			}
			p.text.assign(text, size);
		}
		self.model->setProperty(id, std::move(p));
		return 0;
	}
	return luaL_error(thread, "%s is not a writable member", key);
}
int ScriptRuntime::method(lua_State* thread)
{
	auto& self = context(thread);
	InstanceId id = instance(thread, 1);
	const char* name = lua_tostring(thread, lua_upvalueindex(1));
	if (!self.model->get(id)) {
		return luaL_error(thread, "Instance no longer exists");
	}
	if (!std::strcmp(name, "CreateLocalPlayer") || !std::strcmp(name, "createLocalPlayer") ||
		!std::strcmp(name, "LoadCharacter") || !std::strcmp(name, "loadCharacter") ||
		!std::strcmp(name, "GetPlayerFromCharacter") || !std::strcmp(name, "getPlayerFromCharacter") ||
		!std::strcmp(name, "TakeDamage") || !std::strcmp(name, "takeDamage")) {
		if (!self.model->action) {
			return luaL_error(thread, "Player/character runtime is not active");
		}
		std::vector<SignalValue> arguments;
		if (!std::strcmp(name, "CreateLocalPlayer") || !std::strcmp(name, "createLocalPlayer") ||
			!std::strcmp(name, "TakeDamage") || !std::strcmp(name, "takeDamage")) {
			arguments.push_back((double) luaL_optnumber(thread, 2, 0));
		}
		if (!std::strcmp(name, "GetPlayerFromCharacter") || !std::strcmp(name, "getPlayerFromCharacter")) {
			arguments.push_back(lua_isnoneornil(thread, 2) ? NoInstance : instance(thread, 2));
		}
		pushArgument(thread, self.model->action(id, name, arguments));
		return 1;
	}
	if (!std::strcmp(name, "MakeJoints") || !std::strcmp(name, "makeJoints") || !std::strcmp(name, "BreakJoints") ||
		!std::strcmp(name, "breakJoints")) {
		if (!classIsA(self.model->get(id)->className, "PVInstance") || !self.model->jointOperation) {
			return luaL_error(thread, "Joint operations require an active physics world and a part/model");
		}
		self.model->jointOperation(id, name[0] == 'M' || name[0] == 'm');
		return 0;
	}
	if (!std::strcmp(name, "FindFirstChild") || !std::strcmp(name, "findFirstChild")) {
		pushInstance(thread, self.model->findChild(id, luaL_checkstring(thread, 2), lua_toboolean(thread, 3)));
		return 1;
	}
	if (!std::strcmp(name, "GetChildren") || !std::strcmp(name, "children")) {
		lua_newtable(thread);
		int i = 0;
		for (InstanceId child : self.model->get(id)->children) {
			pushInstance(thread, child);
			lua_rawseti(thread, -2, ++i);
		}
		return 1;
	}
	if (!std::strcmp(name, "IsA")) {
		const char* cls = luaL_checkstring(thread, 2);
		lua_pushboolean(thread, classIsA(self.model->get(id)->className, cls));
		return 1;
	}
	if (!std::strcmp(name, "IsDescendantOf") || !std::strcmp(name, "isDescendentOf")) {
		lua_pushboolean(thread, self.model->isDescendant(id, instance(thread, 2)));
		return 1;
	}
	// Engine Lua catches std::exception in luaD_rawrunprotected. Do not catch
	// its private lua_exception here: that would consume Lua's error ownership.
	{
		if (!std::strcmp(name, "Clone") || !std::strcmp(name, "clone")) {
			pushInstance(thread, self.model->clone(id));
			return 1;
		}
		if (!std::strcmp(name, "Remove") || !std::strcmp(name, "remove")) {
			self.model->remove(id);
			return 0;
		}
		if (id != self.model->root()) {
			lua_pushliteral(thread, "GetService requires DataModel");
		}
		else {
			const char* cls = luaL_checkstring(thread, 2);
			if (!std::strcmp(name, "FindService")) {
				for (InstanceId child : self.model->get(id)->children) {
					if (self.model->get(child)->className == cls) {
						pushInstance(thread, child);
						return 1;
					}
				}
				lua_pushnil(thread);
				return 1;
			}
			pushInstance(thread, self.model->service(cls));
			return 1;
		}
	}
	return lua_error(thread);
}
int ScriptRuntime::construct(lua_State* thread)
{
	auto& self = context(thread);
	const char* cls = luaL_checkstring(thread, 1);
	if (!knownClass(cls)) {
		return luaL_error(thread, "Cannot create unknown class %s", cls);
	}
	InstanceId parent = lua_isnoneornil(thread, 2) ? NoInstance : instance(thread, 2);
	pushInstance(thread, self.model->create(cls, parent));
	return 1;
}
void ScriptRuntime::log(const std::string& text)
{
	if (output.size() >= 64) {
		output.erase(output.begin());
	}
	output.push_back(text.substr(0, 1024));
}
int ScriptRuntime::print(lua_State* thread)
{
	std::string text;
	for (int i = 1; i <= lua_gettop(thread); ++i) {
		if (i > 1) {
			text += '\t';
		}
		const char* value = lua_tostring(thread, i);
		text += value ? value : lua_typename(thread, lua_type(thread, i));
		if (text.size() >= 1024) {
			break;
		}
	}
	context(thread).log(text);
	return 0;
}
int ScriptRuntime::tick(lua_State* thread)
{
	lua_pushnumber(thread, context(thread).time);
	return 1;
}
int ScriptRuntime::wait(lua_State* thread)
{
	auto& self = context(thread);
	if (!self.active || self.active->thread != thread) {
		return luaL_error(thread, "wait requires a scheduled thread");
	}
	double duration = luaL_optnumber(thread, 1, 0);
	if (!std::isfinite(duration)) {
		return luaL_error(thread, "wait requires finite seconds");
	}
	self.active->wake = self.time + std::max(1.0 / 30, duration);
	self.active->yieldedAt = self.time;
	self.active->waiting = true;
	return lua_yield(thread, 0);
}
ScriptRuntime::Task& ScriptRuntime::newTask(lua_State* parent, InstanceId script, double wake)
{
	if (tasks.size() >= 256) {
		throw std::runtime_error("PSP script thread budget exceeded");
	}
	lua_State* thread = lua_newthread(parent);
	int reference = luaL_ref(parent, LUA_REGISTRYINDEX);
	tasks.push_back({thread, reference, wake, time, false, script, ++nextSerial});
	return tasks.back();
}
int ScriptRuntime::delay(lua_State* thread)
{
	auto& self = context(thread);
	double duration = luaL_checknumber(thread, 1);
	luaL_checktype(thread, 2, LUA_TFUNCTION);
	if (!std::isfinite(duration)) {
		return luaL_error(thread, "delay requires finite seconds");
	}
	{
		Task& task =
			self.newTask(thread, self.active ? self.active->script : NoInstance, self.time + std::max(0.0, duration));
		lua_pushvalue(thread, 2);
		lua_xmove(thread, task.thread, 1);
		return 0;
	}
}
void ScriptRuntime::instructionLimit(lua_State* thread, lua_Debug*)
{
	luaL_error(thread, "Script exceeded instruction budget without yielding");
}
void ScriptRuntime::resume(Task& task, int arguments)
{
	active = &task;
	task.waiting = false;
	lua_sethook(task.thread, instructionLimit, LUA_MASKCOUNT, 100000);
	int status = lua_resume(task.thread, arguments);
	active = nullptr;
	if (status == LUA_YIELD) {
		if (!task.waiting && std::isfinite(task.wake)) {
			task.wake = time + 1.0 / 30;
			task.yieldedAt = time;
			task.waiting = true;
		}
		lua_settop(task.thread, 0);
		return;
	}
	if (status) {
		log(lua_tostring(task.thread, -1) ? lua_tostring(task.thread, -1) : "Lua error");
		++errors;
	}
	luaL_unref(state, LUA_REGISTRYINDEX, task.reference);
	task.reference = LUA_NOREF;
}
bool ScriptRuntime::execute(const std::string& source, InstanceId script)
{
	if (!state) {
		throw std::runtime_error("Script runtime is closed");
	}
	Task& task = newTask(state, script, time);
	lua_newtable(task.thread);
	lua_newtable(task.thread);
	lua_pushvalue(task.thread, LUA_GLOBALSINDEX);
	lua_setfield(task.thread, -2, "__index");
	lua_setmetatable(task.thread, -2);
	lua_replace(task.thread, LUA_GLOBALSINDEX);
	pushInstance(task.thread, script);
	lua_setglobal(task.thread, "script");
	std::string chunk = "=PlaceScript";
	if (const Instance* owner = model->get(script)) {
		chunk = "=" + owner->name;
	}
	if (luaL_loadbuffer(task.thread, source.data(), source.size(), chunk.c_str())) {
		log(lua_tostring(task.thread, -1));
		++errors;
		luaL_unref(state, LUA_REGISTRYINDEX, task.reference);
		tasks.pop_back();
		return false;
	}
	// Execution begins on the next simulation heartbeat, never from XML parsing.
	return true;
}
void ScriptRuntime::startPlaceScripts()
{
	autoScripts = true;
	// Snapshot handles: scripts may create more instances once execution starts.
	for (size_t slotIndex = 1; slotIndex <= model->size(); ++slotIndex) {
		InstanceId id = model->idAt(slotIndex);
		const Instance* object = model->get(id);
		if (!object || (object->className != "Script" && object->className != "LocalScript")) {
			continue;
		}
		InstanceId workspace = model->service("Workspace");
		if (!model->isDescendant(id, workspace)) {
			continue;
		}
		if (startedScripts.count(id)) {
			continue;
		}
		const Property* disabled = object->property("Disabled");
		const Property* source = object->property("Source");
		if (source && (!disabled || disabled->text != "true")) {
			// Leave room for event callbacks, and defer remaining discovery until
			// completed startup tasks have released their coroutine references.
			if (tasks.size() >= 192) {
				return;
			}
			startedScripts.insert(id);
			execute(source->text, id);
		}
	}
}
void ScriptRuntime::primePlaceScripts()
{
	// Initial registration precedes native default Player creation. No artificial
	// Stepped/Heartbeat events or elapsed time while draining one-shot scripts.
	for (size_t pass = 0; pass < 128; ++pass) {
		size_t before = startedScripts.size();
		startPlaceScripts();
		resumeReadyTasks();
		if (startedScripts.size() == before) {
			break;
		}
	}
	lua_gc(state, LUA_GCSTEP, 16);
}
bool ScriptRuntime::activeScript(InstanceId id) const
{
	if (!id) {
		return true;
	}
	const Instance* object = model->get(id);
	if (!object || object->retired || !model->isDescendant(id, model->root())) {
		return false;
	}
	const Property* disabled = object->property("Disabled");
	return !disabled || disabled->text != "true";
}
void ScriptRuntime::step(double dt)
{
	if (!state || !std::isfinite(dt) || dt < 0) {
		return;
	}
	for (auto it = connections.begin(); it != connections.end();) {
		const Instance* sender = model->get(it->sender);
		if (activeScript(it->script) && sender && !sender->retired) {
			++it;
			continue;
		}
		if (it->function != LUA_NOREF) {
			luaL_unref(state, LUA_REGISTRYINDEX, it->function);
		}
		it = connections.erase(it);
	}
	for (auto it = startedScripts.begin(); it != startedScripts.end();) {
		if (activeScript(*it)) {
			++it;
		}
		else {
			it = startedScripts.erase(it);
		}
	}
	if (autoScripts) {
		startPlaceScripts();
	}
	time += dt;
	fireSignal(runService, "Stepped", {time, dt});
	fireSignal(runService, "Heartbeat", {dt});
	resumeReadyTasks();
	lua_gc(state, LUA_GCSTEP, 16);
}
void ScriptRuntime::resumeReadyTasks()
{
	uint32_t last = nextSerial;
	for (auto it = tasks.begin(); it != tasks.end();) {
		Task& task = *it;
		if (!activeScript(task.script)) {
			luaL_unref(state, LUA_REGISTRYINDEX, task.reference);
			it = tasks.erase(it);
			continue;
		}
		if (task.serial <= last && task.wake <= time) {
			int arguments = task.pendingArguments;
			task.pendingArguments = 0;
			if (task.waiting) {
				lua_pushnumber(task.thread, time - task.yieldedAt);
				arguments = 1;
			}
			resume(task, arguments);
		}
		if (task.reference == LUA_NOREF) {
			it = tasks.erase(it);
		}
		else {
			++it;
		}
	}
}
} // namespace openrbx

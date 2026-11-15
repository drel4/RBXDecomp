#pragma once

#include "math3d.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openrbx {
using InstanceId = uint32_t;
constexpr InstanceId NoInstance = 0;
using SignalValue = std::variant<std::monostate, InstanceId, double, bool, std::string, Vec3, CFrame>;

// Preserve XML types and structured components until their engine binding reads
// them. Ref targets use stable handles, never pointers into a growable vector.
struct Property
{
	std::string_view name, type;
	std::string text;
	std::vector<std::pair<std::string_view, std::string>> components;
	InstanceId reference = NoInstance;
	Property(
		std::string_view name = {},
		std::string_view type = {},
		std::string text = {},
		std::vector<std::pair<std::string_view, std::string>> components = {}
	)
		: name(name), type(type), text(std::move(text)), components(std::move(components))
	{
	}
};
// Clone snapshots share immutable property arrays. Mutation must explicitly call
// write/edit; plain iteration is always const and cannot accidentally detach.
class PropertyList
{
public:
	using Storage = std::vector<Property>;
	const Storage& read() const;
	Storage& write();
	Storage::const_iterator begin() const { return read().begin(); }
	Storage::const_iterator end() const { return read().end(); }
	const Property& operator[](size_t i) const { return read()[i]; }
	Property& edit(size_t i) { return write()[i]; }
	const Property& back() const { return read().back(); }
	size_t size() const { return read().size(); }
	size_t capacity() const { return read().capacity(); }
	bool empty() const { return read().empty(); }
	void push_back(Property value) { write().push_back(std::move(value)); }
	void shrink_to_fit()
	{
		if (data) {
			write().shrink_to_fit();
		}
	}
	const void* identity() const { return data.get(); }
	long owners() const { return data.use_count(); }

private:
	std::shared_ptr<Storage> data;
};
struct Instance
{
	std::string_view className;
	std::string name, referent;
	InstanceId parent = NoInstance;
	std::vector<InstanceId> children;
	PropertyList properties;
	int renderPart = -1;
	bool alive = true;
	uint32_t revision = 0;
	uint32_t renderRevision = UINT32_MAX;
	bool spatial = false;
	bool dead = false;
	bool retired = false;
	uint16_t generation = 0;
	uint32_t retainers = 0;
	const Property* property(const std::string& name) const;
};

// PSP storage adaptation of v8tree/Instance: ordered children, named lookup,
// parent cycle prevention, subtree cloning and Remove detachment. Unknown
// serialized classes/properties survive loading; this is not reflection support
// for those classes. Handle 1 is the DataModel, handle 0 is nil.
class DataModel
{
public:
	DataModel();
	DataModel(const DataModel&) = delete;
	DataModel& operator=(const DataModel&) = delete;
	std::string_view intern(const std::string& text);
	void clear();
	InstanceId root() const { return objects.empty() ? NoInstance : 1; }
	InstanceId create(const std::string& className, InstanceId parent = NoInstance);
	Instance* get(InstanceId id);
	const Instance* get(InstanceId id) const;
	// Handles encode generation:slot. Never index an array with a full handle.
	static size_t slot(InstanceId id) { return id & 0xffffu; }
	InstanceId idAt(size_t slot) const;
	void retain(InstanceId id);
	void release(InstanceId id);
	size_t collect();
	size_t liveCount() const;
	uint32_t collections() const { return collectionRevision; }
	void setParent(InstanceId child, InstanceId parent);
	bool isDescendant(InstanceId child, InstanceId ancestor) const;
	InstanceId findChild(InstanceId parent, const std::string& name, bool recursive = false) const;
	InstanceId service(const std::string& className);
	InstanceId clone(InstanceId source);
	InstanceId import(const DataModel& source, InstanceId object, InstanceId parent = NoInstance);
	void setName(InstanceId id, const std::string& name);
	void setProperty(InstanceId id, Property property);
	void changed(InstanceId id, std::string_view name);
	using SignalSink = std::function<void(InstanceId, std::string_view, const std::vector<SignalValue>&)>;
	SignalSink signal;
	std::function<void(InstanceId, bool)> jointOperation;
	std::function<SignalValue(InstanceId, std::string_view, const std::vector<SignalValue>&)> action;
	void remove(InstanceId source);
	// Native session teardown ends event ownership without invalidating Lua handles.
	void retire(InstanceId source);
	void resolveReferences();
	size_t size() const { return objects.size(); }
	size_t bytes() const;
	size_t pspBytes() const;
	const std::vector<Instance>& instances() const { return objects; }

private:
	std::vector<Instance> objects;
	std::vector<uint16_t> freeSlots;
	uint32_t collectionRevision = 0;
	std::set<std::string> symbols;
	size_t storageBytes(bool pspLayout) const;
	InstanceId copyTree(InstanceId source, InstanceId parent, std::vector<std::pair<InstanceId, InstanceId>>& copied);
};
} // namespace openrbx

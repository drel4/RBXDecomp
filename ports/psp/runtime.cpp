#include "runtime.h"

#include "schema.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>

namespace openrbx {
const PropertyList::Storage& PropertyList::read() const
{
	static const Storage empty;
	return data ? *data : empty;
}
PropertyList::Storage& PropertyList::write()
{
	if (!data) {
		data = std::make_shared<Storage>();
	}
	else if (!data.unique()) {
		data = std::make_shared<Storage>(*data);
	}
	return *data;
}
const Property* Instance::property(const std::string& key) const
{
	std::string_view canonical = canonicalProperty(className, key);
	for (const Property& p : properties) {
		if (canonicalProperty(className, p.name) == canonical) {
			return &p;
		}
	}
	return defaultProperty(className, canonical);
}
DataModel::DataModel()
{
}
void DataModel::clear()
{
	signal = nullptr;
	jointOperation = nullptr;
	action = nullptr;
	std::vector<Instance>().swap(objects);
	std::vector<uint16_t>().swap(freeSlots);
	collectionRevision = 0;
	symbols.clear();
}
std::string_view DataModel::intern(const std::string& text)
{
	return *symbols.insert(text).first;
}
Instance* DataModel::get(InstanceId id)
{
	size_t index = slot(id);
	return index && index <= objects.size() && objects[index - 1].alive && objects[index - 1].generation == (id >> 16)
			   ? &objects[index - 1]
			   : nullptr;
}
const Instance* DataModel::get(InstanceId id) const
{
	size_t index = slot(id);
	return index && index <= objects.size() && objects[index - 1].alive && objects[index - 1].generation == (id >> 16)
			   ? &objects[index - 1]
			   : nullptr;
}
InstanceId DataModel::idAt(size_t index) const
{
	if (!index || index > objects.size() || !objects[index - 1].alive) {
		return NoInstance;
	}
	return (InstanceId(objects[index - 1].generation) << 16) | InstanceId(index);
}
void DataModel::retain(InstanceId id)
{
	if (Instance* object = get(id)) {
		++object->retainers;
	}
}
void DataModel::release(InstanceId id)
{
	if (Instance* object = get(id)) {
		if (!object->retainers) {
			throw std::runtime_error("Unbalanced instance release");
		}
		--object->retainers;
	}
}
size_t DataModel::liveCount() const
{
	return std::count_if(objects.begin(), objects.end(), [](const Instance& object) { return object.alive; });
}
size_t DataModel::collect()
{
	// Native ownership and Lua userdata are roots. Traverse both hierarchy
	// directions and Ref properties so detached Lua backups remain complete.
	std::vector<uint8_t> marked(objects.size(), 0);
	std::vector<InstanceId> pending;
	auto mark = [&](InstanceId id) {
		if (get(id) && !marked[slot(id) - 1]) {
			marked[slot(id) - 1] = 1;
			pending.push_back(id);
		}
	};
	mark(root());
	for (size_t index = 1; index <= objects.size(); ++index) {
		if (objects[index - 1].retainers) {
			mark(idAt(index));
		}
	}
	while (!pending.empty()) {
		const Instance& object = *get(pending.back());
		pending.pop_back();
		mark(object.parent);
		for (InstanceId child : object.children) {
			mark(child);
		}
		for (const Property& p : object.properties) {
			if (p.type == "Ref") {
				mark(p.reference);
			}
		}
	}
	size_t count = 0;
	// Reserve before mutation: collection is all-or-nothing on allocation failure.
	freeSlots.reserve(objects.size());
	for (size_t index = 1; index <= objects.size(); ++index) {
		Instance& object = objects[index - 1];
		if (!object.alive || marked[index - 1]) {
			continue;
		}
		uint16_t generation = object.generation;
		object = Instance();
		object.alive = false;
		object.generation = generation;
		// Retire exhausted generations instead of ever aliasing an old handle.
		if (generation != UINT16_MAX) {
			freeSlots.push_back((uint16_t) index);
		}
		++count;
	}
	if (count) {
		++collectionRevision;
	}
	return count;
}
InstanceId DataModel::create(const std::string& cls, InstanceId parent)
{
	if (freeSlots.empty() && objects.size() >= 16000) {
		throw std::runtime_error("Place exceeds 16000-instance PSP budget");
	}
	if (cls.empty() || (parent && !get(parent))) {
		throw std::runtime_error("Invalid instance class or parent");
	}
	Instance object;
	object.className = intern(cls);
	object.name = cls;
	object.spatial = classIsA(cls, "Part");
	size_t index;
	if (freeSlots.empty()) {
		objects.push_back(std::move(object));
		index = objects.size();
	}
	else {
		index = freeSlots.back();
		object.generation = objects[index - 1].generation + 1;
		objects[index - 1] = std::move(object);
		freeSlots.pop_back();
	}
	InstanceId id = idAt(index);
	setParent(id, parent);
	return id;
}
bool DataModel::isDescendant(InstanceId child, InstanceId ancestor) const
{
	const Instance* object = get(child);
	while (object && object->parent) {
		if (object->parent == ancestor) {
			return true;
		}
		object = get(object->parent);
	}
	return false;
}
void DataModel::setParent(InstanceId child, InstanceId parent)
{
	Instance* object = get(child);
	if (!object || (parent && !get(parent)) || child == parent || isDescendant(parent, child)) {
		throw std::runtime_error("Invalid or cyclic instance parenting");
	}
	if (child == root() && parent) {
		throw std::runtime_error("DataModel cannot be parented");
	}
	if (object->parent == parent) {
		return;
	}
	InstanceId oldParent = object->parent;
	std::vector<InstanceId> oldAncestors, newAncestors, descendants;
	for (InstanceId p = oldParent; p; p = get(p)->parent) {
		oldAncestors.push_back(p);
	}
	for (InstanceId p = parent; p; p = get(p)->parent) {
		newAncestors.push_back(p);
	}
	std::vector<std::pair<InstanceId, size_t>> pending = {{child, 1}};
	while (!pending.empty()) {
		auto current = pending.back();
		pending.pop_back();
		if (newAncestors.size() + current.second > 192) {
			throw std::runtime_error("Instance hierarchy exceeds PSP depth budget");
		}
		descendants.push_back(current.first);
		for (InstanceId id : get(current.first)->children) {
			pending.push_back({id, current.second + 1});
		}
	}
	// Allocate before detaching, keeping the old relationship on allocation failure.
	if (parent) {
		get(parent)->children.push_back(child);
	}
	if (Instance* old = get(object->parent)) {
		auto& children = old->children;
		children.erase(std::remove(children.begin(), children.end(), child), children.end());
	}
	object->parent = parent;
	for (InstanceId descendant : descendants) {
		++get(descendant)->revision;
	}
	if (signal) {
		for (InstanceId ancestor : oldAncestors) {
			if (std::find(newAncestors.begin(), newAncestors.end(), ancestor) != newAncestors.end()) {
				continue;
			}
			for (InstanceId descendant : descendants) {
				signal(ancestor, "DescendentRemoving", {descendant});
			}
		}
		if (oldParent) {
			signal(oldParent, "ChildRemoved", {child});
		}
		if (parent) {
			signal(parent, "ChildAdded", {child});
		}
		for (InstanceId ancestor : newAncestors) {
			if (std::find(oldAncestors.begin(), oldAncestors.end(), ancestor) != oldAncestors.end()) {
				continue;
			}
			for (InstanceId descendant : descendants) {
				signal(ancestor, "DescendentAdded", {descendant});
			}
		}
		for (InstanceId descendant : descendants) {
			signal(descendant, "AncestryChanged", {child, parent});
		}
		signal(child, "Changed", {std::string("Parent")});
	}
}
void DataModel::setName(InstanceId id, const std::string& name)
{
	Instance* object = get(id);
	if (!object) {
		throw std::runtime_error("Invalid instance");
	}
	if (object->name == name) {
		return;
	}
	object->name = name;
	for (Property& p : object->properties.write()) {
		if (p.name == "Name") {
			p.text = name;
		}
	}
	changed(id, "Name");
}
void DataModel::setProperty(InstanceId id, Property value)
{
	Instance* object = get(id);
	if (!object) {
		throw std::runtime_error("Invalid instance");
	}
	value.name = intern(std::string(canonicalProperty(object->className, value.name)));
	value.type = intern(std::string(value.type));
	const Property* old = object->property(std::string(value.name));
	if (!old) {
		throw std::runtime_error("Unknown writable property: " + std::string(value.name));
	}
	if (old->type != value.type) {
		throw std::runtime_error("Property type mismatch");
	}
	if (object->className == "Humanoid" && value.name == "Health") {
		float health = std::strtof(value.text.c_str(), nullptr),
			  maximum = std::strtof(object->property("MaxHealth")->text.c_str(), nullptr);
		if (!std::isfinite(health)) {
			throw std::runtime_error("Humanoid health must be finite");
		}
		value.text = std::to_string(std::max(0.f, std::min(maximum, health)));
		if (std::strtof(old->text.c_str(), nullptr) == std::strtof(value.text.c_str(), nullptr)) {
			return;
		}
	}
	if (old->text == value.text && old->reference == value.reference && old->components == value.components) {
		return;
	}
	if (value.type == "Vector3" && propertyVector(*old) == propertyVector(value)) {
		return;
	}
	if (value.type == "CoordinateFrame" && propertyFrame(*old) == propertyFrame(value)) {
		return;
	}
	for (Property& p : object->properties.write()) {
		if (canonicalProperty(object->className, p.name) == value.name) {
			p = std::move(value);
			changed(id, p.name);
			return;
		}
	}
	object->properties.push_back(std::move(value));
	changed(id, object->properties.back().name);
}
void DataModel::changed(InstanceId id, std::string_view name)
{
	Instance* object = get(id);
	if (!object) {
		return;
	}
	++object->revision;
	if (object->className == "Humanoid" && name == "Health" && !object->dead &&
		std::strtof(object->property("Health")->text.c_str(), nullptr) <= 0) {
		object->dead = true;
		if (jointOperation && object->parent) {
			jointOperation(object->parent, false);
		}
		if (signal) {
			signal(id, "Died", {});
		}
	}
	if (!signal) {
		return;
	}
	const Property* value = object->property("Value");
	if (value) {
		if (name != "Value") {
			return; // Value<T>::Changed shadows Instance::Changed.
		}
		SignalValue argument;
		if (value->type == "Ref") {
			argument = value->reference;
		}
		else if (value->type == "bool") {
			argument = value->text == "true";
		}
		else if (value->type == "Vector3") {
			argument = propertyVector(*value);
		}
		else if (value->type == "CoordinateFrame") {
			argument = propertyFrame(*value);
		}
		else if (value->type == "string") {
			argument = value->text;
		}
		else {
			argument = std::strtod(value->text.c_str(), nullptr);
		}
		signal(id, "Changed", {argument});
	}
	else {
		signal(id, "Changed", {std::string(name)});
		if (name == "CFrame" && classIsA(object->className, "Part")) {
			signal(id, "Changed", {std::string("Position")});
		}
	}
}
InstanceId DataModel::findChild(InstanceId parent, const std::string& name, bool recursive) const
{
	const Instance* object = get(parent);
	if (!object) {
		return NoInstance;
	}
	for (InstanceId id : object->children) {
		if (get(id)->name == name) {
			return id;
		}
	}
	if (recursive) {
		for (InstanceId id : object->children) {
			InstanceId found = findChild(id, name, true);
			if (found) {
				return found;
			}
		}
	}
	return NoInstance;
}
InstanceId DataModel::service(const std::string& cls)
{
	if (!root()) {
		create("DataModel");
	}
	for (InstanceId id : get(root())->children) {
		if (get(id)->className == cls) {
			return id;
		}
	}
	return create(cls, root());
}
InstanceId DataModel::copyTree(
	InstanceId source, InstanceId parent, std::vector<std::pair<InstanceId, InstanceId>>& copied
)
{
	// Copies are necessary: create can relocate objects, invalidating references.
	Instance original = *get(source);
	InstanceId id = create(std::string(original.className), parent);
	Instance* object = get(id);
	object->name = original.name;
	object->properties = original.properties;
	// Geometry belongs to the source instance, not its uninstantiated clone.
	copied.push_back({source, id});
	for (InstanceId child : original.children) {
		const Property* archivable = get(child)->property("archivable");
		if (!archivable || archivable->text != "false") {
			copyTree(child, id, copied);
		}
	}
	return id;
}
InstanceId DataModel::clone(InstanceId source)
{
	if (!get(source)) {
		throw std::runtime_error("Cannot clone nil instance");
	}
	const Property* archivable = get(source)->property("archivable");
	if (archivable && archivable->text == "false") {
		return NoInstance;
	}
	std::vector<std::pair<InstanceId, InstanceId>> copied;
	InstanceId result = copyTree(source, NoInstance, copied);
	for (const auto& pair : copied) {
		auto& properties = get(pair.second)->properties;
		bool references =
			std::any_of(properties.begin(), properties.end(), [](const Property& p) { return p.type == "Ref"; });
		if (!references) {
			continue;
		}
		for (Property& p : properties.write()) {
			if (p.type == "Ref") {
				for (const auto& target : copied) {
					if (p.reference == target.first) {
						p.reference = target.second;
						break;
					}
				}
			}
		}
	}
	return result;
}
InstanceId DataModel::import(const DataModel& source, InstanceId object, InstanceId parent)
{
	if (&source == this) {
		InstanceId result = clone(object);
		if (result) {
			setParent(result, parent);
		}
		return result;
	}
	if (!source.get(object)) {
		throw std::runtime_error("Invalid import root");
	}
	std::vector<std::pair<InstanceId, InstanceId>> copied;
	std::function<InstanceId(InstanceId, InstanceId)> copy = [&](InstanceId from, InstanceId toParent) -> InstanceId {
		const Instance& original = *source.get(from);
		InstanceId id = create(std::string(original.className), toParent);
		Instance* target = get(id);
		target->name = original.name;
		for (Property p : original.properties) {
			p.name = intern(std::string(p.name));
			p.type = intern(std::string(p.type));
			for (auto& component : p.components) {
				component.first = intern(std::string(component.first));
			}
			target->properties.push_back(std::move(p));
		}
		copied.push_back({from, id});
		for (InstanceId child : original.children) {
			copy(child, id);
		}
		return id;
	};
	InstanceId result = copy(object, NoInstance);
	for (const auto& pair : copied) {
		for (Property& p : get(pair.second)->properties.write()) {
			if (p.type == "Ref") {
				InstanceId reference = NoInstance;
				for (const auto& target : copied) {
					if (p.reference == target.first) {
						reference = target.second;
						break;
					}
				}
				p.reference = reference;
			}
		}
	}
	setParent(result, parent);
	return result;
}
void DataModel::remove(InstanceId source)
{
	if (!get(source) || source == root()) {
		return;
	}
	// Classic Remove recursively detaches children; Lua-held handles stay valid.
	while (!get(source)->children.empty()) {
		remove(get(source)->children.back());
	}
	setParent(source, NoInstance);
}
void DataModel::retire(InstanceId source)
{
	Instance* object = get(source);
	if (!object || object->retired) {
		return;
	}
	object->retired = true;
	for (InstanceId child : object->children) {
		retire(child);
	}
}
void DataModel::resolveReferences()
{
	std::map<std::string, InstanceId> referents;
	for (size_t i = 0; i < objects.size(); ++i) {
		const std::string& ref = objects[i].referent;
		if (objects[i].alive && !ref.empty() && ref != "null" && !referents.emplace(ref, idAt(i + 1)).second) {
			throw std::runtime_error("Duplicate RBXL instance referent");
		}
	}
	for (Instance& object : objects) {
		bool needsBinding = std::any_of(object.properties.begin(), object.properties.end(), [&](const Property& p) {
			if (p.type != "Ref") {
				return false;
			}
			auto target = referents.find(p.text);
			return p.reference != (target == referents.end() ? NoInstance : target->second);
		});
		if (!needsBinding) {
			continue;
		}
		for (Property& p : object.properties.write()) {
			if (p.type == "Ref") {
				auto target = referents.find(p.text);
				p.reference = target == referents.end() ? NoInstance : target->second;
			}
		}
	}
}
size_t DataModel::bytes() const
{
	return storageBytes(false);
}
size_t DataModel::pspBytes() const
{
	return storageBytes(true);
}
size_t DataModel::storageBytes(bool pspLayout) const
{
	// Native budget must not depend on the host checker's pointer size. These
	// libstdc++ layout constants are verified by the actual 32-bit PSP build.
	static_assert(
		sizeof(void*) != 4 ||
			(sizeof(Instance) == 108 && sizeof(Property) == 56 && sizeof(std::string) == 24 &&
			 sizeof(std::pair<std::string_view, std::string>) == 32 && sizeof(PropertyList::Storage) == 12),
		"PSP allocation accounting ABI changed"
	);
	// libstdc++ embeds short strings inside their owning object. Those bytes are
	// already in sizeof(Instance/Property); count only separately allocated text.
	auto allocated = [](const std::string& text) -> size_t {
		uintptr_t data = reinterpret_cast<uintptr_t>(text.data());
		uintptr_t object = reinterpret_cast<uintptr_t>(&text);
		return data >= object && data < object + sizeof(text) ? 0 : text.capacity() + 1;
	};
	size_t total = objects.capacity() * (pspLayout ? 108 : sizeof(Instance)) + freeSlots.capacity() * sizeof(uint16_t);
	std::set<const void*> shared;
	for (const Instance& object : objects) {
		total += allocated(object.name) + allocated(object.referent);
		total += object.children.capacity() * sizeof(InstanceId);
		if (!object.properties.identity() ||
			(object.properties.owners() > 1 && !shared.insert(object.properties.identity()).second)) {
			continue;
		}
		total += object.properties.capacity() * (pspLayout ? 56 : sizeof(Property)) +
				 (pspLayout ? 20 : sizeof(PropertyList::Storage) + 2 * sizeof(void*));
		for (const Property& p : object.properties) {
			total += allocated(p.text);
			total += p.components.capacity() * (pspLayout ? 32 : sizeof(std::pair<std::string_view, std::string>));
			for (const auto& component : p.components) {
				total += allocated(component.second);
			}
		}
	}
	for (const std::string& symbol : symbols) {
		total += (pspLayout ? 40 : sizeof(symbol) + 4 * sizeof(void*)) + allocated(symbol);
	}
	return total;
}
} // namespace openrbx

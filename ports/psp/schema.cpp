#include "schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace openrbx {
struct ClassEntry
{
	const char* name;
	const char* base;
};
// Engine ancestry, from App/include/v8datamodel, script/Script.h, and Network.
static const ClassEntry Classes[] = {
	{"Instance", ""},
	{"DataModel", "Instance"},
	{"PVInstance", "Instance"},
	{"Model", "PVInstance"},
	{"RootInstance", "Model"},
	{"Workspace", "RootInstance"},
	{"Part", "PVInstance"},
	{"SpawnLocation", "Part"},
	{"Seat", "Part"},
	{"WedgePart", "Part"},
	{"TrussPart", "Part"},
	{"Humanoid", "Instance"},
	{"Player", "Instance"},
	{"Players", "Instance"},
	{"Script", "Instance"},
	{"LocalScript", "Script"},
	{"IntValue", "Instance"},
	{"NumberValue", "Instance"},
	{"BoolValue", "Instance"},
	{"StringValue", "Instance"},
	{"ObjectValue", "Instance"},
	{"Vector3Value", "Instance"},
	{"CFrameValue", "Instance"},
	{"Message", "Instance"},
	{"Hint", "Instance"},
	{"RunService", "Instance"},
	{"Lighting", "Instance"},
	{"Teams", "Instance"},
	{"Team", "Instance"},
	{"StarterPack", "Instance"},
	{"Backpack", "Instance"},
	{"HopperBin", "Instance"},
	{"Sound", "Instance"},
	{"SoundService", "Instance"},
	{"Camera", "Instance"},
	{"JointInstance", "Instance"},
	{"AutoJoint", "JointInstance"},
	{"Weld", "AutoJoint"},
	{"Motor", "AutoJoint"},
	{"Snap", "AutoJoint"},
	{"Glue", "AutoJoint"},
	{"Rotate", "AutoJoint"},
	{"RotateP", "AutoJoint"},
	{"RotateV", "AutoJoint"},
	{"ForceField", "Instance"},
	{"Decal", "Instance"}
};
bool knownClass(std::string_view cls)
{
	for (const auto& entry : Classes) {
		if (cls == entry.name) {
			return true;
		}
	}
	return false;
}
bool classIsA(std::string_view cls, std::string_view base)
{
	// BasePart is accepted as a compatibility alias for old Part subclasses.
	if (base == "BasePart") {
		base = "Part";
	}
	while (!cls.empty()) {
		if (cls == base) {
			return true;
		}
		std::string_view parent;
		for (const auto& entry : Classes) {
			if (cls == entry.name) {
				parent = entry.base;
				break;
			}
		}
		if (parent.empty()) {
			return base == "Instance" && cls != "Instance";
		}
		cls = parent;
	}
	return false;
}
std::string_view canonicalProperty(std::string_view cls, std::string_view name)
{
	if (name == "Archivable") {
		return "archivable";
	}
	if ((name == "Size" || name == "Shape" || name == "CoordinateFrame") && classIsA(cls, "Part")) {
		if (name == "Size") {
			return "size";
		}
		if (name == "Shape") {
			return "shape";
		}
		if (name == "CoordinateFrame") {
			return "CFrame";
		}
	}
	return name;
}
const char* signalName(std::string_view cls, std::string_view name)
{
	for (const char* event :
		 {"Changed", "ChildAdded", "ChildRemoved", "AncestryChanged", "DescendentAdded", "DescendentRemoving"}) {
		if (name == event) {
			return event;
		}
	}
	if (classIsA(cls, "Part") && name == "Touched") {
		return "Touched";
	}
	if (classIsA(cls, "Humanoid")) {
		for (const char* event :
			 {"Died", "Running", "Climbing", "Jumping", "FreeFalling", "GettingUp", "FallingDown", "Seated"}) {
			if (name == event) {
				return event;
			}
		}
	}
	if (cls == "Players") {
		for (const char* event : {"PlayerAdded", "PlayerRemoving"}) {
			if (name == event) {
				return event;
			}
		}
	}
	if (cls == "RunService" && name == "Heartbeat") {
		return "Heartbeat";
	}
	if (cls == "RunService" && name == "Stepped") {
		return "Stepped";
	}
	if (cls == "Player" && name == "CharacterAdded") {
		return "CharacterAdded";
	}
	return nullptr;
}
struct Defaults
{
	const char* cls;
	std::vector<Property> properties;
};
const Property* defaultProperty(std::string_view cls, std::string_view name)
{
	// Shared immutable defaults avoid duplicating a reflection table on every part.
	// Values are sourced from PartInstance, Value, Script, Humanoid and MotorJoint.
	static const Defaults defaults[] = {
		{"Instance", {{"archivable", "bool", "true"}}},
		{"Part",
		 {{"CFrame", "CoordinateFrame", ""},
		  {"size", "Vector3", "", {{"X", "4"}, {"Y", "1.2"}, {"Z", "2"}}},
		  {"Velocity", "Vector3", ""},
		  {"RotVelocity", "Vector3", ""},
		  {"Anchored", "bool", "false"},
		  {"CanCollide", "bool", "true"},
		  {"Transparency", "float", "0"},
		  {"Reflectance", "float", "0"},
		  {"Friction", "float", "0.3"},
		  {"Elasticity", "float", "0.5"},
		  {"Locked", "bool", "false"},
		  {"BrickColor", "int", "194"},
		  {"shape", "token", "1"}}},
		{"Part",
		 {{"RightSurface", "token", "0"},
		  {"TopSurface", "token", "3"},
		  {"BackSurface", "token", "0"},
		  {"LeftSurface", "token", "0"},
		  {"BottomSurface", "token", "4"},
		  {"FrontSurface", "token", "0"}}},
		{"Script", {{"Source", "ProtectedString", ""}, {"Disabled", "bool", "false"}}},
		// SurfaceData::empty() in the real world engine, including NO_INPUT=0.
		{"Part",
		 {{"RightParamA", "float", "-0.5"},
		  {"RightParamB", "float", "0.5"},
		  {"RightSurfaceInput", "token", "0"},
		  {"TopParamA", "float", "-0.5"},
		  {"TopParamB", "float", "0.5"},
		  {"TopSurfaceInput", "token", "0"},
		  {"BackParamA", "float", "-0.5"},
		  {"BackParamB", "float", "0.5"},
		  {"BackSurfaceInput", "token", "0"},
		  {"LeftParamA", "float", "-0.5"},
		  {"LeftParamB", "float", "0.5"},
		  {"LeftSurfaceInput", "token", "0"},
		  {"BottomParamA", "float", "-0.5"},
		  {"BottomParamB", "float", "0.5"},
		  {"BottomSurfaceInput", "token", "0"},
		  {"FrontParamA", "float", "-0.5"},
		  {"FrontParamB", "float", "0.5"},
		  {"FrontSurfaceInput", "token", "0"}}},
		{"IntValue", {{"Value", "int", "0"}}},
		{"NumberValue", {{"Value", "double", "0"}}},
		{"BoolValue", {{"Value", "bool", "false"}}},
		{"StringValue", {{"Value", "string", ""}}},
		{"ObjectValue", {{"Value", "Ref", "null"}}},
		{"Vector3Value", {{"Value", "Vector3", ""}}},
		{"CFrameValue", {{"Value", "CoordinateFrame", ""}}},
		{"Message", {{"Text", "string", ""}}},
		{"Hint", {{"Text", "string", ""}}},
		{"Humanoid",
		 {{"Health", "float", "100"},
		  {"MaxHealth", "float", "100"},
		  {"WalkSpeed", "float", "16"},
		  {"Jump", "bool", "false"},
		  {"Sit", "bool", "false"}}},
		{"JointInstance",
		 {{"Part0", "Ref", "null"},
		  {"Part1", "Ref", "null"},
		  {"C0", "CoordinateFrame", ""},
		  {"C1", "CoordinateFrame", ""}}},
		{"Motor", {{"DesiredAngle", "float", "0"}, {"CurrentAngle", "float", "0"}, {"MaxVelocity", "float", "0"}}},
		{"Player", {{"Character", "Ref", "null"}, {"userId", "int", "0"}, {"Neutral", "bool", "true"}}},
		{"Players", {{"LocalPlayer", "Ref", "null"}}},
		{"SpawnLocation",
		 {{"Neutral", "bool", "true"}, {"AllowTeamChangeOnTouch", "bool", "false"}, {"Duration", "int", "10"}}}
	};
	name = canonicalProperty(cls, name);
	for (const auto& entry : defaults) {
		if (!classIsA(cls, entry.cls)) {
			continue;
		}
		for (const auto& p : entry.properties) {
			if (p.name == name) {
				return &p;
			}
		}
	}
	return nullptr;
}
static float scalar(const std::string& text)
{
	char* end;
	float value = std::strtof(text.c_str(), &end);
	while (*end && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) {
		++end;
	}
	if (end == text.c_str() || *end || !std::isfinite(value)) {
		throw std::runtime_error("Invalid structured property number");
	}
	return value;
}
Vec3 propertyVector(const Property& p)
{
	Vec3 v;
	for (const auto& c : p.components) {
		if (c.first == "X") {
			v.x = scalar(c.second);
		}
		else if (c.first == "Y") {
			v.y = scalar(c.second);
		}
		else if (c.first == "Z") {
			v.z = scalar(c.second);
		}
	}
	return v;
}
CFrame propertyFrame(const Property& p)
{
	CFrame f(propertyVector(p));
	for (const auto& c : p.components) {
		if (c.first.size() == 3 && c.first[0] == 'R' && c.first[1] >= '0' && c.first[1] <= '2' && c.first[2] >= '0' &&
			c.first[2] <= '2') {
			f.rotation[(c.first[1] - '0') * 3 + c.first[2] - '0'] = scalar(c.second);
		}
	}
	return f;
}
static std::string number(float value)
{
	if (!std::isfinite(value)) {
		throw std::runtime_error("Structured property must be finite");
	}
	char text[32];
	std::snprintf(text, sizeof(text), "%.9g", (double) value);
	return text;
}
Property vectorProperty(std::string_view name, Vec3 v)
{
	return {name, "Vector3", "", {{"X", number(v.x)}, {"Y", number(v.y)}, {"Z", number(v.z)}}};
}
Property frameProperty(std::string_view name, const CFrame& f)
{
	static const char* labels[] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
	Property p = vectorProperty(name, f.position);
	p.type = "CoordinateFrame";
	for (int i = 0; i < 9; ++i) {
		p.components.push_back({labels[i], number(f.rotation[i])});
	}
	return p;
}
void compactDefaults(Instance& object)
{
	// Preserve every unknown/overridden value; omit only values already supplied
	// by the same inherited reflection default. No script source is truncated.
	auto redundant = [&](const Property& p) {
		const Property* d = defaultProperty(object.className, p.name);
		if (!d || d->type != p.type) {
			return false;
		}
		// Do not expose a later duplicate by removing an earlier default-valued
		// property. Preserve the original lookup/serialization order verbatim.
		if (std::count_if(object.properties.begin(), object.properties.end(), [&](const Property& other) {
				return canonicalProperty(object.className, other.name) == d->name;
			}) != 1) {
			return false;
		}
		if (p.type == "Vector3" || p.type == "CoordinateFrame") {
			for (const auto& component : p.components) {
				auto key = component.first;
				bool vector = key == "X" || key == "Y" || key == "Z";
				bool matrix = p.type == "CoordinateFrame" && key.size() == 3 && key[0] == 'R' && key[1] >= '0' &&
							  key[1] <= '2' && key[2] >= '0' && key[2] <= '2';
				if (!vector && !matrix) {
					return false;
				}
			}
			if (!p.text.empty()) {
				return false;
			}
			return p.type == "Vector3" ? propertyVector(p) == propertyVector(*d)
									   : propertyFrame(p) == propertyFrame(*d);
		}
		if (!p.components.empty()) {
			return false;
		}
		if (p.type == "float" || p.type == "double" || p.type == "int" || p.type == "token") {
			char* end;
			double value = std::strtod(p.text.c_str(), &end);
			if (end == p.text.c_str() || *end || !std::isfinite(value)) {
				return false;
			}
			double baseline = std::strtod(d->text.c_str(), nullptr);
			return p.type == "float" ? (float) value == (float) baseline : value == baseline;
		}
		// Ref binding has not run yet: never discard a non-null XML referent.
		return p.text == d->text && (!p.reference || p.reference == d->reference);
	};
	if (std::none_of(object.properties.begin(), object.properties.end(), redundant)) {
		return;
	}
	auto& values = object.properties.write();
	values.erase(std::remove_if(values.begin(), values.end(), redundant), values.end());
}
} // namespace openrbx

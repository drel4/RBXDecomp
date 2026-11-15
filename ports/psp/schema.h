#pragma once
#include "runtime.h"

namespace openrbx {
bool classIsA(std::string_view cls, std::string_view base);
bool knownClass(std::string_view cls);
std::string_view canonicalProperty(std::string_view cls, std::string_view name);
const Property* defaultProperty(std::string_view cls, std::string_view name);
const char* signalName(std::string_view cls, std::string_view name);
Vec3 propertyVector(const Property& property);
CFrame propertyFrame(const Property& property);
Property vectorProperty(std::string_view name, Vec3 value);
Property frameProperty(std::string_view name, const CFrame& value);
void compactDefaults(Instance& object);
} // namespace openrbx

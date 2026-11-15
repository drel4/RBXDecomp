#include "scene.h"

#include "schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace openrbx {
static const size_t MaxParts = 8000, MaxVertices = 600000, MaxDepth = 192;
static const float Pi = 3.14159265358979323846f;
Part::Part() : size(4, 1.2f, 2), color(0xffa5a3a2), transparency(0), radius(1), first(0), count(0), shape(1)
{
	for (int i = 0; i < 9; ++i) {
		rotation[i] = (i % 4 == 0) ? 1.0f : 0.0f;
	}
}
Scene::Scene() : skipped(0), hasSpawn(false)
{
}
void Scene::clear()
{
	Scene empty;
	model.clear();
	parts.swap(empty.parts);
	vertices.swap(empty.vertices);
	skipped = 0;
	hasSpawn = false;
	collectionRevision = 0;
	minimum = maximum = focus = Vec3();
}
size_t Scene::bytes() const
{
	return parts.capacity() * sizeof(Part) + vertices.capacity() * sizeof(Vertex) + model.bytes();
}

uint32_t brickColor(int number)
{
	struct Color
	{
		int number;
		uint8_t r, g, b;
	};
	// Generated from the existing 2007 engine's BrickColor table, not a new palette.
	static const Color colors[]={
#include "brick_colors.inc"
    };
	for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
		if (colors[i].number == number) {
			return 0xff000000u | colors[i].r | (colors[i].g << 8) | (colors[i].b << 16);
		}
	}
	return 0xffa5a3a2u;
}

static Vec3 rotate(const Part& p, const Vec3& v)
{
	return Vec3(
		p.rotation[0] * v.x + p.rotation[1] * v.y + p.rotation[2] * v.z,
		p.rotation[3] * v.x + p.rotation[4] * v.y + p.rotation[5] * v.z,
		p.rotation[6] * v.x + p.rotation[7] * v.y + p.rotation[8] * v.z
	);
}
static Vec3 point(const Part& p, const Vec3& v)
{
	return p.position + rotate(p, Vec3(v.x * p.size.x, v.y * p.size.y, v.z * p.size.z));
}
static void triangle(Scene& s, const Part& p, Vec3 a, Vec3 b, Vec3 c)
{
	if (s.vertices.size() + 3 > MaxVertices) {
		throw std::runtime_error("Geometry exceeds PSP 9.6 MB vertex budget");
	}
	a = point(p, a);
	b = point(p, b);
	c = point(p, c);
	Vec3 n = normalized(cross(b - a, c - a));
	float light = 0.48f + 0.52f * std::max(0.0f, dot(n, normalized(Vec3(-0.4f, 0.85f, 0.3f))));
	unsigned r = (unsigned) ((p.color & 255) * light), g = (unsigned) (((p.color >> 8) & 255) * light),
			 bl = (unsigned) (((p.color >> 16) & 255) * light);
	uint32_t color = (uint32_t) ((1 - p.transparency) * 255) << 24 | r | (g << 8) | (bl << 16);
	Vertex v = {color, a.x, a.y, a.z};
	s.vertices.push_back(v);
	v.x = b.x;
	v.y = b.y;
	v.z = b.z;
	s.vertices.push_back(v);
	v.x = c.x;
	v.y = c.y;
	v.z = c.z;
	s.vertices.push_back(v);
}
static int segments(float size)
{
	return std::max(1, std::min(32, (int) std::ceil(size / 32.0f)));
}
static size_t boxVertices(const Part& p)
{
	int x = segments(p.size.x), y = segments(p.size.y), z = segments(p.size.z);
	return 12 * (x * y + x * z + y * z);
}
static size_t vertexCount(const Part& p)
{
	return p.shape == 0 ? 240 : p.shape == 2 ? 144 : p.shape == 3 ? 24 : boxVertices(p);
}
static void face(Scene& s, const Part& p, Vec3 a, Vec3 b, Vec3 d, int nu, int nv)
{
	// Large unbroken floor triangles can cross PSP GE's near/guard clipping
	// limits. Subdivide once on load, not on every rendered frame.
	Vec3 u = (b - a) * (1.0f / nu), v = (d - a) * (1.0f / nv);
	for (int j = 0; j < nv; ++j) {
		for (int i = 0; i < nu; ++i) {
			Vec3 q = a + u * float(i) + v * float(j);
			triangle(s, p, q, q + u, q + u + v);
			triangle(s, p, q, q + u + v, q + v);
		}
	}
}
static void box(Scene& s, const Part& p)
{
	static const Vec3 v[] = {
		Vec3(-.5f, -.5f, -.5f),
		Vec3(.5f, -.5f, -.5f),
		Vec3(.5f, .5f, -.5f),
		Vec3(-.5f, .5f, -.5f),
		Vec3(-.5f, -.5f, .5f),
		Vec3(.5f, -.5f, .5f),
		Vec3(.5f, .5f, .5f),
		Vec3(-.5f, .5f, .5f)
	};
	int x = segments(p.size.x), y = segments(p.size.y), z = segments(p.size.z);
	face(s, p, v[0], v[3], v[1], y, x);
	face(s, p, v[4], v[5], v[7], x, y);
	face(s, p, v[0], v[1], v[4], x, z);
	face(s, p, v[3], v[7], v[2], z, x);
	face(s, p, v[0], v[4], v[3], z, y);
	face(s, p, v[1], v[2], v[5], y, z);
}
static Vec3 spherePoint(float theta, float phi)
{
	return Vec3(.5f * std::sin(theta) * std::cos(phi), .5f * std::cos(theta), .5f * std::sin(theta) * std::sin(phi));
}
static void mesh(Scene& s, Part& p)
{
	p.first = (uint32_t) s.vertices.size();
	p.radius = length(p.size) * .5f;
	if (p.shape == 0) { // Ball. Low-poly on purpose: 8 slices, 6 latitude bands.
		for (int j = 0; j < 6; ++j) {
			for (int i = 0; i < 8; ++i) {
				Vec3 a = spherePoint(Pi * j / 6, 2 * Pi * i / 8), b = spherePoint(Pi * (j + 1) / 6, 2 * Pi * i / 8);
				Vec3 c = spherePoint(Pi * j / 6, 2 * Pi * (i + 1) / 8),
					 d = spherePoint(Pi * (j + 1) / 6, 2 * Pi * (i + 1) / 8);
				if (j != 0) {
					triangle(s, p, a, c, b);
				}
				if (j != 5) {
					triangle(s, p, b, c, d);
				}
			}
		}
	}
	else if (p.shape == 2) { // Cylinder axis is X, matching PartInstance.
		for (int i = 0; i < 12; ++i) {
			float a = 2 * Pi * i / 12, b = 2 * Pi * (i + 1) / 12;
			Vec3 v0(-.5f, .5f * std::cos(a), .5f * std::sin(a)), v1(-.5f, .5f * std::cos(b), .5f * std::sin(b));
			Vec3 v2(.5f, v0.y, v0.z), v3(.5f, v1.y, v1.z);
			triangle(s, p, v0, v1, v2);
			triangle(s, p, v1, v3, v2);
			triangle(s, p, Vec3(-.5f, 0, 0), v1, v0);
			triangle(s, p, Vec3(.5f, 0, 0), v2, v3);
		}
	}
	else if (p.shape == 3) {
		Vec3 a(-.5f, -.5f, -.5f), b(.5f, -.5f, -.5f), c(-.5f, -.5f, .5f), d(.5f, -.5f, .5f), e(-.5f, .5f, .5f),
			f(.5f, .5f, .5f);
		triangle(s, p, a, c, b);
		triangle(s, p, b, c, d);
		triangle(s, p, c, e, f);
		triangle(s, p, c, f, d);
		triangle(s, p, a, e, c);
		triangle(s, p, b, d, f);
		triangle(s, p, a, b, f);
		triangle(s, p, a, f, e);
	}
	else {
		box(s, p);
	}
	p.count = (uint32_t) s.vertices.size() - p.first;
	Vec3 extent;
	extent.x = (std::fabs(p.rotation[0]) * p.size.x + std::fabs(p.rotation[1]) * p.size.y +
				std::fabs(p.rotation[2]) * p.size.z) *
			   .5f;
	extent.y = (std::fabs(p.rotation[3]) * p.size.x + std::fabs(p.rotation[4]) * p.size.y +
				std::fabs(p.rotation[5]) * p.size.z) *
			   .5f;
	extent.z = (std::fabs(p.rotation[6]) * p.size.x + std::fabs(p.rotation[7]) * p.size.y +
				std::fabs(p.rotation[8]) * p.size.z) *
			   .5f;
	Vec3 lo = p.position - extent, hi = p.position + extent;
	if (p.first == 0) {
		s.minimum = lo;
		s.maximum = hi;
	}
	else {
		s.minimum = Vec3(std::min(s.minimum.x, lo.x), std::min(s.minimum.y, lo.y), std::min(s.minimum.z, lo.z));
		s.maximum = Vec3(std::max(s.maximum.x, hi.x), std::max(s.maximum.y, hi.y), std::max(s.maximum.z, hi.z));
	}
}

namespace {
struct Node
{
	std::string tag, name;
	int property = -1;
};
struct Item
{
	Part part;
	InstanceId instance = NoInstance;
	bool render, workspace, spawn;
	Item() : render(false), workspace(false), spawn(false) {}
};
static std::string attribute(const std::string& s, const char* key)
{
	size_t p = 0;
	while ((p = s.find(key, p)) != std::string::npos) {
		if (p > 0 && s[p - 1] != ' ' && s[p - 1] != '\t' && s[p - 1] != '\n' && s[p - 1] != '\r') {
			p += std::strlen(key);
			continue;
		}
		size_t q = p + std::strlen(key);
		while (q < s.size() && std::isspace((unsigned char) s[q])) {
			++q;
		}
		if (q >= s.size() || s[q] != '=') {
			p = q;
			continue;
		}
		++q;
		while (q < s.size() && std::isspace((unsigned char) s[q])) {
			++q;
		}
		if (q >= s.size() || (s[q] != '\'' && s[q] != '"')) {
			return "";
		}
		char quote = s[q++];
		size_t e = s.find(quote, q);
		return e == std::string::npos ? "" : s.substr(q, e - q);
	}
	return "";
}
static std::string decodeXml(const std::string& value)
{
	std::string result;
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] != '&') {
			result += value[i];
			continue;
		}
		size_t end = value.find(';', i + 1);
		if (end == std::string::npos || end - i > 12) {
			throw std::runtime_error("Invalid XML entity");
		}
		std::string entity = value.substr(i + 1, end - i - 1);
		if (entity == "amp") {
			result += '&';
		}
		else if (entity == "lt") {
			result += '<';
		}
		else if (entity == "gt") {
			result += '>';
		}
		else if (entity == "quot") {
			result += '"';
		}
		else if (entity == "apos") {
			result += '\'';
		}
		else if (!entity.empty() && entity[0] == '#') {
			bool hex = entity.size() > 1 && entity[1] == 'x';
			const char* start = entity.c_str() + (hex ? 2 : 1);
			char* stop;
			unsigned long c = std::strtoul(start, &stop, hex ? 16 : 10);
			if (start == stop || *stop || !c || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
				throw std::runtime_error("Invalid XML character reference");
			}
			if (c < 0x80) {
				result += (char) c;
			}
			else {
				if (c >= 0x10000) {
					result += (char) (0xf0 | (c >> 18));
				}
				if (c >= 0x800) {
					result += (char) ((c >= 0x10000 ? 0x80 : 0xe0) | ((c >> 12) & 63));
				}
				result += (char) ((c >= 0x800 ? 0x80 : 0xc0) | ((c >> 6) & 63));
				result += (char) (0x80 | (c & 63));
			}
		}
		else {
			throw std::runtime_error("Unsupported XML entity");
		}
		i = end;
	}
	return result;
}
static float number(const std::string& s)
{
	char* end = NULL;
	float v = std::strtof(s.c_str(), &end);
	if (end == s.c_str() || !std::isfinite(v) || std::fabs(v) > 1e7f) {
		throw std::runtime_error("Invalid numeric RBXL property");
	}
	while (*end && std::isspace((unsigned char) *end)) {
		++end;
	}
	if (*end) {
		throw std::runtime_error("Invalid numeric RBXL property");
	}
	return v;
}
static void scalar(Item& item, const std::vector<Node>& nodes, const std::string& text)
{
	if (!item.render || nodes.size() < 2) {
		return;
	}
	const Node& leaf = nodes.back();
	const Node& parent = nodes[nodes.size() - 2];
	Part& p = item.part;
	if (parent.tag == "Properties") {
		if (leaf.name == "BrickColor") {
			p.color = brickColor((int) number(text));
		}
		else if (leaf.name == "Color3uint8" || leaf.name == "Color") {
			unsigned long rgb = std::strtoul(text.c_str(), NULL, 10);
			p.color = 0xff000000u | ((rgb >> 16) & 255) | (rgb & 0xff00) | ((rgb & 255) << 16);
		}
		else if (leaf.name == "shape" || leaf.name == "Shape") {
			p.shape = (int) number(text);
		}
		else if (leaf.name == "Transparency") {
			p.transparency = std::max(0.0f, std::min(1.0f, number(text)));
		}
	}
	else if (parent.name == "size" || parent.name == "Size") {
		if (leaf.tag == "X") {
			p.size.x = number(text);
		}
		else if (leaf.tag == "Y") {
			p.size.y = number(text);
		}
		else if (leaf.tag == "Z") {
			p.size.z = number(text);
		}
	}
	else if (parent.name == "CFrame" || parent.name == "CoordinateFrame") {
		if (leaf.tag == "X") {
			p.position.x = number(text);
		}
		else if (leaf.tag == "Y") {
			p.position.y = number(text);
		}
		else if (leaf.tag == "Z") {
			p.position.z = number(text);
		}
		else if (
			leaf.tag.size() == 3 && leaf.tag[0] == 'R' && leaf.tag[1] >= '0' && leaf.tag[1] <= '2' &&
			leaf.tag[2] >= '0' && leaf.tag[2] <= '2'
		) {
			p.rotation[(leaf.tag[1] - '0') * 3 + leaf.tag[2] - '0'] = number(text);
		}
	}
}
struct PropertyPool
{
	std::unordered_map<size_t, std::vector<PropertyList>> entries;
	void share(Instance& object)
	{
		if (object.properties.empty()) {
			return;
		}
		size_t hash = 0;
		auto mix = [&](std::string_view value) {
			hash ^= std::hash<std::string_view>()(value) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
		};
		for (const Property& p : object.properties) {
			mix(p.name);
			mix(p.type);
			mix(p.text);
			for (const auto& c : p.components) {
				mix(c.first);
				mix(c.second);
			}
		}
		auto& bucket = entries[hash];
		for (const auto& existing : bucket) {
			if (existing.size() != object.properties.size()) {
				continue;
			}
			bool equal = true;
			for (size_t i = 0; i < existing.size(); ++i) {
				const Property& a = existing[i];
				const Property& b = object.properties[i];
				if (a.name != b.name || a.type != b.type || a.text != b.text || a.reference != b.reference ||
					a.components != b.components) {
					equal = false;
					break;
				}
			}
			if (equal) {
				object.properties = existing;
				return;
			}
		}
		bucket.push_back(object.properties);
	}
};
static void parse(FILE* file, Scene& scene, bool assetMode)
{
	PropertyPool propertyPool;
	std::vector<Node> nodes;
	std::vector<Item> items;
	std::string text, plain;
	scene.model.create("DataModel");
	size_t propertyCount = 0, retainedText = 0;
	size_t textLimit = 256 * 1024;
	bool sawRoot = false;
	int ch;
	while ((ch = std::fgetc(file)) != EOF) {
		if (ch != '<') {
			if (plain.size() + text.size() >= textLimit) {
				throw std::runtime_error(
					textLimit > 256 * 1024 ? "RBXL binary content exceeds 1 MiB PSP budget"
										   : "RBXL property exceeds 256 KiB PSP budget"
				);
			}
			plain += (char) ch;
			continue;
		}
		text += decodeXml(plain);
		plain.clear();
		std::string tag;
		char quote = 0;
		while ((ch = std::fgetc(file)) != EOF) {
			if (!quote && ch == '>') {
				break;
			}
			tag += (char) ch;
			if ((ch == '"' || ch == '\'') && (!quote || quote == ch)) {
				quote = quote ? 0 : (char) ch;
			}
			if (tag.size() == 8 && tag == "![CDATA[") {
				int a = 0, b = 0, c;
				bool ended = false;
				while ((c = std::fgetc(file)) != EOF) {
					text += (char) c;
					if (text.size() > textLimit + 3) {
						throw std::runtime_error("RBXL source exceeds 256 KiB PSP budget");
					}
					if (a == ']' && b == ']' && c == '>') {
						text.resize(text.size() - 3);
						ended = true;
						break;
					}
					a = b;
					b = c;
				}
				if (!ended) {
					throw std::runtime_error("Truncated CDATA");
				}
				tag = "?preserve";
				break;
			}
			if (tag.size() == 3 && tag == "!--") {
				int a = 0, b = 0, c;
				bool ended = false;
				while ((c = std::fgetc(file)) != EOF) {
					if (a == '-' && b == '-' && c == '>') {
						ended = true;
						break;
					}
					a = b;
					b = c;
				}
				if (!ended) {
					throw std::runtime_error("Truncated XML comment");
				}
				tag = "?preserve";
				break;
			}
			if (tag.size() > 2048) {
				throw std::runtime_error("RBXL XML tag too long");
			}
		}
		if (ch == EOF) {
			throw std::runtime_error("Truncated RBXL XML");
		}
		if (tag.empty()) {
			throw std::runtime_error("Empty XML tag");
		}
		if (tag[0] == '?') {
			if (tag != "?preserve") {
				text.clear();
			}
			continue;
		}
		if (tag[0] == '!') {
			throw std::runtime_error("XML entity/DOCTYPE declarations are not supported");
		}
		bool closing = tag[0] == '/', selfClosing = tag.back() == '/';
		size_t begin = closing ? 1 : 0;
		size_t end = tag.find_first_of(" \t\r\n/", begin);
		std::string name = tag.substr(begin, end == std::string::npos ? end : end - begin);
		if (closing) {
			if (nodes.empty() || nodes.back().tag != name) {
				throw std::runtime_error("Mismatched RBXL XML tags");
			}
			if (!items.empty()) {
				Instance* object = scene.model.get(items.back().instance);
				for (size_t k = nodes.size(); k > 0; --k) {
					if (nodes[k - 1].tag == "Item") {
						break;
					}
					if (nodes[k - 1].property < 0) {
						continue;
					}
					Property& p = object->properties.edit(nodes[k - 1].property);
					if (retainedText + text.size() > 8 * 1024 * 1024) {
						throw std::runtime_error("RBXL properties exceed 8 MiB text budget");
					}
					retainedText += text.size();
					if (k == nodes.size()) {
						// Structured properties retain their leaves, not XML indentation.
						if (p.components.empty()) {
							p.text = text;
						}
						p.components.shrink_to_fit();
						if (p.name == "Name") {
							object->name = text;
						}
					}
					else {
						if (p.components.size() >= 4096) {
							throw std::runtime_error("Structured property exceeds PSP component budget");
						}
						std::string path;
						for (size_t j = k; j < nodes.size(); ++j) {
							if (!path.empty()) {
								path += '/';
							}
							path += nodes[j].tag;
						}
						p.components.push_back({scene.model.intern(path), text});
					}
					break;
				}
				scalar(items.back(), nodes, text);
			}
			if (name == "Item") {
				if (items.empty()) {
					throw std::runtime_error("Invalid Item nesting");
				}
				Item& item = items.back();
				compactDefaults(*scene.model.get(item.instance));
				scene.model.get(item.instance)->properties.shrink_to_fit();
				propertyPool.share(*scene.model.get(item.instance));
				scene.model.get(item.instance)->children.shrink_to_fit();
				if (item.render && item.workspace && item.part.transparency < .99f) {
					if (scene.parts.size() >= MaxParts) {
						throw std::runtime_error("Place exceeds 8000-part PSP budget");
					}
					if (item.part.size.x <= 0 || item.part.size.y <= 0 || item.part.size.z <= 0) {
						throw std::runtime_error("Part size must be positive");
					}
					scene.model.get(item.instance)->renderPart = (int) scene.parts.size();
					item.part.source = item.instance;
					scene.model.get(item.instance)->renderRevision = scene.model.get(item.instance)->revision;
					scene.parts.push_back(item.part);
					if (item.spawn && !scene.hasSpawn) {
						scene.focus = item.part.position;
						scene.hasSpawn = true;
					}
				}
				else if (item.render) {
					++scene.skipped;
				}
				items.pop_back();
			}
			nodes.pop_back();
		}
		else {
			if (nodes.size() >= MaxDepth) {
				throw std::runtime_error("RBXL nesting exceeds PSP limit");
			}
			if (nodes.empty()) {
				if (name != "roblox" || sawRoot) {
					throw std::runtime_error("Not a Roblox XML place");
				}
				sawRoot = true;
			}
			Node n;
			n.tag = name;
			n.name = decodeXml(attribute(tag, "name"));
			if (!items.empty() && !nodes.empty() && nodes.back().tag == "Properties") {
				if (++propertyCount > 500000) {
					throw std::runtime_error("RBXL exceeds 500000-property PSP budget");
				}
				if (propertyCount % 4096 == 0 && scene.model.pspBytes() > 12 * 1024 * 1024) {
					throw std::runtime_error("Instance data exceeds 12 MiB PSP budget");
				}
				Instance* object = scene.model.get(items.back().instance);
				n.property = (int) object->properties.size();
				Property p;
				p.name = scene.model.intern(n.name);
				p.type = scene.model.intern(name);
				object->properties.push_back(std::move(p));
			}
			nodes.push_back(n);
			if (name == "Item") {
				Item item;
				item.workspace = items.empty() ? assetMode : items.back().workspace;
				std::string cls = attribute(tag, "class");
				item.instance =
					scene.model.create(decodeXml(cls), items.empty() ? scene.model.root() : items.back().instance);
				scene.model.get(item.instance)->referent = decodeXml(attribute(tag, "referent"));
				if (cls == "Workspace") {
					item.workspace = true;
				}
				item.spawn = cls == "SpawnLocation";
				item.render = cls == "Part" || item.spawn || cls == "WedgePart" || cls == "Seat" ||
							  cls == "VehicleSeat" || cls == "TrussPart";
				if (cls == "WedgePart") {
					item.part.shape = 3;
				}
				items.push_back(item);
			}
			if (selfClosing) {
				if (name == "Item") {
					items.pop_back();
				}
				nodes.pop_back();
			}
		}
		text.clear();
		textLimit = !nodes.empty() && nodes.back().tag == "binary" ? 1024 * 1024 : 256 * 1024;
	}
	if (std::ferror(file) || !nodes.empty() || !sawRoot) {
		throw std::runtime_error("Incomplete RBXL XML file");
	}
	if (scene.parts.empty() && !assetMode) {
		throw std::runtime_error("No visible Workspace parts found");
	}
	scene.model.resolveReferences();
	if (scene.model.pspBytes() > 12 * 1024 * 1024) {
		throw std::runtime_error(
			"Instance data exceeds 12 MiB PSP budget (" + std::to_string(scene.model.pspBytes()) + " bytes)"
		);
	}
	size_t count = 0;
	for (size_t i = 0; i < scene.parts.size(); ++i) {
		count += vertexCount(scene.parts[i]);
		if (count > MaxVertices) {
			throw std::runtime_error("Geometry exceeds PSP 9.6 MB vertex budget");
		}
	}
	scene.vertices.reserve(count);
	for (size_t i = 0; i < scene.parts.size(); ++i) {
		mesh(scene, scene.parts[i]);
	}
	if (!scene.hasSpawn) {
		scene.focus = (scene.minimum + scene.maximum) * .5f;
	}
}
} // namespace
static Part runtimePart(const Instance& object, InstanceId id)
{
	Part p;
	p.source = id;
	CFrame f = propertyFrame(*object.property("CFrame"));
	p.position = f.position;
	std::copy(f.rotation, f.rotation + 9, p.rotation);
	p.size = propertyVector(*object.property("size"));
	if (p.size.x <= 0 || p.size.y <= 0 || p.size.z <= 0) {
		throw std::runtime_error("Runtime part size must be positive");
	}
	p.shape =
		object.className == "WedgePart" ? 3 : (int) std::strtol(object.property("shape")->text.c_str(), nullptr, 10);
	p.color = brickColor((int) std::strtol(object.property("BrickColor")->text.c_str(), nullptr, 10));
	if (const Property* c = object.property("Color3uint8")) {
		unsigned long rgb = std::strtoul(c->text.c_str(), nullptr, 10);
		p.color = 0xff000000u | ((rgb >> 16) & 255) | (rgb & 0xff00) | ((rgb & 255) << 16);
	}
	p.transparency = std::max(0.f, std::min(1.f, std::strtof(object.property("Transparency")->text.c_str(), nullptr)));
	return p;
}
static bool sameGeometry(const Part& a, const Part& b)
{
	if (!(a.position == b.position) || !(a.size == b.size) || a.shape != b.shape || a.color != b.color ||
		a.transparency != b.transparency) {
		return false;
	}
	for (int i = 0; i < 9; ++i) {
		if (a.rotation[i] != b.rotation[i]) {
			return false;
		}
	}
	return true;
}
bool refreshScene(Scene& scene, bool& changed, std::string& error)
{
	changed = false;
	error.clear();
	try {
		InstanceId workspace = scene.model.service("Workspace");
		bool rebuild = false;
		if (scene.collectionRevision != scene.model.collections()) {
			for (const Part& part : scene.parts) {
				if (!scene.model.get(part.source)) {
					rebuild = true;
					break;
				}
			}
		}
		std::vector<std::pair<size_t, Part>> updates;
		for (size_t slotIndex = 1; slotIndex <= scene.model.size(); ++slotIndex) {
			InstanceId id = scene.model.idAt(slotIndex);
			Instance* object = scene.model.get(id);
			if (!object || !object->spatial || object->renderRevision == object->revision) {
				continue;
			}
			bool world = scene.model.isDescendant(id, workspace);
			if (!world) {
				if (object->renderPart >= 0) {
					rebuild = true;
				}
				continue;
			}
			Part p = runtimePart(*object, id);
			if (p.transparency >= .99f) {
				if (object->renderPart >= 0) {
					rebuild = true;
				}
				continue;
			}
			if (object->renderPart < 0) {
				rebuild = true;
				continue;
			}
			const Part& old = scene.parts[object->renderPart];
			if (vertexCount(p) != old.count) {
				rebuild = true;
			}
			else if (!sameGeometry(p, old)) {
				updates.push_back({(size_t) object->renderPart, p});
			}
		}
		if (rebuild) {
			Scene output;
			size_t vertices = 0;
			for (size_t slotIndex = 1; slotIndex <= scene.model.size(); ++slotIndex) {
				InstanceId id = scene.model.idAt(slotIndex);
				const Instance* object = scene.model.get(id);
				if (!object || !object->spatial || !scene.model.isDescendant(id, workspace)) {
					continue;
				}
				Part p = runtimePart(*object, id);
				if (p.transparency >= .99f) {
					continue;
				}
				if (output.parts.size() >= MaxParts) {
					throw std::runtime_error("Runtime exceeds 8000 visible-part budget");
				}
				vertices += vertexCount(p);
				if (vertices > MaxVertices) {
					throw std::runtime_error("Runtime exceeds PSP vertex budget");
				}
				output.parts.push_back(p);
				if (object->className == "SpawnLocation" && !output.hasSpawn) {
					output.hasSpawn = true;
					output.focus = p.position;
				}
			}
			output.vertices.reserve(vertices);
			for (Part& p : output.parts) {
				mesh(output, p);
			}
			scene.parts.swap(output.parts);
			scene.vertices.swap(output.vertices);
			scene.minimum = output.minimum;
			scene.maximum = output.maximum;
			scene.hasSpawn = output.hasSpawn;
			scene.focus = output.hasSpawn ? output.focus : (output.minimum + output.maximum) * .5f;
			for (size_t slotIndex = 1; slotIndex <= scene.model.size(); ++slotIndex) {
				InstanceId id = scene.model.idAt(slotIndex);
				if (Instance* object = scene.model.get(id)) {
					object->renderPart = -1;
				}
			}
			for (size_t i = 0; i < scene.parts.size(); ++i) {
				scene.model.get(scene.parts[i].source)->renderPart = (int) i;
			}
			changed = true;
		}
		else if (!updates.empty()) {
			Scene scratch;
			for (auto& update : updates) {
				Part p = update.second;
				scratch.vertices.clear();
				mesh(scratch, p);
				uint32_t first = scene.parts[update.first].first;
				std::copy(scratch.vertices.begin(), scratch.vertices.end(), scene.vertices.begin() + first);
				p.first = first;
				scene.parts[update.first] = p;
			}
			changed = true;
		}
		for (size_t slotIndex = 1; slotIndex <= scene.model.size(); ++slotIndex) {
			InstanceId id = scene.model.idAt(slotIndex);
			if (Instance* object = scene.model.get(id)) {
				object->renderRevision = object->revision;
			}
		}
		scene.collectionRevision = scene.model.collections();
		return true;
	}
	catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
bool loadPlace(const char* path, Scene& scene, std::string& error, bool assetMode)
{
	scene.clear();
	error.clear();
	FILE* file = std::fopen(path, "rb");
	if (!file) {
		error = "Cannot open place";
		return false;
	}
	try {
		std::fseek(file, 0, SEEK_END);
		long size = std::ftell(file);
		std::rewind(file);
		if (size < 8 || size > 64 * 1024 * 1024) {
			throw std::runtime_error("Place must be 8 bytes to 64 MB");
		}
		char head[16] = {0};
		std::fread(head, 1, 15, file);
		std::rewind(file);
		if (std::memcmp(head, "<roblox!", 8) == 0) {
			throw std::runtime_error("Binary RBXL unsupported; use legacy XML RBXL/RBXLX");
		}
		parse(file, scene, assetMode);
		std::fclose(file);
		return true;
	}
	catch (const std::bad_alloc&) {
		error = "Not enough PSP memory for this place";
	}
	catch (const std::exception& e) {
		error = e.what();
	}
	std::fclose(file);
	scene.clear();
	return false;
}
} // namespace openrbx

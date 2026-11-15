#ifndef UTIL_ASSETPROXY_H
#define UTIL_ASSETPROXY_H
#include <string>
namespace RBX { namespace AssetProxy {
bool enabled();
void setEnabled(bool value);
bool preference();
int policy();
void setPolicy(const void* owner, int mode);
void releasePolicy(const void* owner);
inline bool isProxyUrl(const std::string& source)
{
	const std::string prefix("http://assetproxy.blox.ralsei.tech/");
	std::string id = source.substr(0, prefix.size()) == prefix ? source.substr(prefix.size()) : "";
	return !id.empty() && id.size() <= 19 && id[0] != '0' && id.find_first_not_of("0123456789") == std::string::npos;
}
// Rewrite only Roblox asset references, never unrelated game API/auth requests.
inline std::string url(const std::string& source)
{
	std::string lower(source);
	for (size_t i = 0; i < lower.size(); ++i) {
		if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 'a' - 'A';
	}
	std::string id;
	if (lower.compare(0, 13, "rbxassetid://") == 0) id = source.substr(13);
	else {
		size_t start;
		if (lower.compare(0, 7, "http://") == 0) start = 7;
		else if (lower.compare(0, 8, "https://") == 0) start = 8;
		else return source;
		size_t slash = lower.find('/', start);
		if (slash == std::string::npos) return source;
		std::string host = lower.substr(start, slash - start);
		if (host.find('@') != std::string::npos || host.find(':') != std::string::npos) return source;
		if (host != "roblox.com" && (host.size() < 11 || host.substr(host.size() - 11) != ".roblox.com")) return source;
		size_t query = lower.find('?', slash);
		if (query == std::string::npos) return source;
		std::string path = lower.substr(slash, query - slash);
		if (path != "/asset" && path != "/asset/" && path != "/asset.ashx" &&
			path != "/v1/asset" && path != "/v1/asset/") return source;
		size_t end = lower.find('#', query);
		if (end == std::string::npos) end = lower.size();
		for (size_t pos = query + 1; pos < end;) {
			size_t next = lower.find('&', pos);
			if (next == std::string::npos || next > end) next = end;
			if (lower.compare(pos, 3, "id=") == 0) {
				if (!id.empty()) return source;
				id = source.substr(pos + 3, next - pos - 3);
			}
			pos = next + 1;
		}
	}
	if (id.empty() || id.size() > 19 || id.find_first_not_of("0123456789") != std::string::npos) return source;
	size_t first = id.find_first_not_of('0');
	if (first == std::string::npos) return source;
	return "http://assetproxy.blox.ralsei.tech/" + id.substr(first);
}
} }
#endif

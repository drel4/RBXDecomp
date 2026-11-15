#include "util/AssetProxy.h"
#include <cassert>
#include <iostream>
int main() {
    using RBX::AssetProxy::url;
    const std::string expected("http://assetproxy.blox.ralsei.tech/1029523");
    assert(url("http://www.roblox.com/asset/?id=1029523")==expected);
    assert(url("https://assetdelivery.roblox.com/v1/asset/?id=1029523")==expected);
    assert(url("HTTPS://WWW.ROBLOX.COM/ASSET/?version=1&ID=001029523#ignored")==expected);
    assert(url("rbxassetid://1029523")==expected);
    const char* untouched[]={"rbxasset://textures/a.png", "file://G:/place.rbxl", "27966ae26d9f9b311eab3e9723bf948f",
      "http://example.com/asset/?id=1029523", "http://evilroblox.com/asset/?id=1029523",
      "http://roblox.com.evil.com/asset/?id=1029523", "http://www.roblox.com/login?id=1029523",
      "http://www.roblox.com/asset/?id=1029523x", "http://www.roblox.com/asset/?id=0",
      "http://www.roblox.com/asset/?id=-1", "http://www.roblox.com/asset/?assetversionid=1029523",
      "http://roblox.com@evil.com/asset/?id=1029523", "http://www.roblox.com/asset/?id=1&id=2",
      "http://assetproxy.blox.ralsei.tech/1029523"};
    for(size_t i=0;i<sizeof(untouched)/sizeof(untouched[0]);++i) assert(url(untouched[i])==untouched[i]);
    assert(RBX::AssetProxy::isProxyUrl(expected));
    assert(!RBX::AssetProxy::isProxyUrl(expected+"/../admin"));
    std::cout << "ASSET_URL_ROUTING_PASS\n";
}

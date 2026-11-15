#include "util/Http.h"
#include "util/AssetProxy.h"
#include <atomic>
#include <mutex>
#include <curl/curl.h>
#include <boost/thread/once.hpp>
#include <sstream>
#include <stdexcept>
namespace RBX {
namespace AssetProxy {
static std::atomic<bool> active(true);
static std::atomic<int> forced(-1);
static std::mutex policyMutex;
static const void* policyOwner=NULL;
bool preference() { return active.load(); }
int policy() { return forced.load(); }
bool enabled() { int mode=policy(); return mode<0 ? preference() : mode!=0; }
void setEnabled(bool value) { active.store(value); }
void setPolicy(const void* owner,int mode) { std::lock_guard<std::mutex> lock(policyMutex); policyOwner=owner; forced.store(mode); }
void releasePolicy(const void* owner) { std::lock_guard<std::mutex> lock(policyMutex); if(policyOwner==owner) { policyOwner=NULL; forced.store(-1); } }
}
static boost::once_flag curlOnce=BOOST_ONCE_INIT;
static void initCurl() { if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) throw std::runtime_error("curl initialization failed"); }
static size_t append(char* data,size_t size,size_t count,void* out) { ((std::string*)out)->append(data,size*count); return size*count; }
static void request(const Http& http,const std::string* body,std::string& response) {
  boost::call_once(initCurl,curlOnce);
  CURL* curl=curl_easy_init(); if(!curl) throw std::runtime_error("curl handle allocation failed");
  const std::string target=!body && AssetProxy::enabled() ? AssetProxy::url(http.url) : http.url;
  response.clear(); curl_easy_setopt(curl,CURLOPT_URL,target.c_str());
  curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
  curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
  curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L); curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
  curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L); curl_easy_setopt(curl,CURLOPT_TIMEOUT,30L);
  curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L); curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
  curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append); curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
  curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
  if(body) { curl_easy_setopt(curl,CURLOPT_POSTFIELDS,body->data()); curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)body->size()); }
  curl_slist* headers=NULL; std::istringstream input(http.additionalHeaders); std::string line;
  while(std::getline(input,line)) { if(!line.empty() && line.back()=='\r') line.pop_back(); if(!line.empty()) headers=curl_slist_append(headers,line.c_str()); }
  if(headers) curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
  CURLcode result=curl_easy_perform(curl); curl_slist_free_all(headers); curl_easy_cleanup(curl);
  if(result!=CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ")+curl_easy_strerror(result));
}
void Http::get(std::string& response) { request(*this,NULL,response); }
void Http::post(std::istream& input,bool,std::string& response) { std::ostringstream data; data<<input.rdbuf(); std::string body=data.str(); request(*this,&body,response); }
bool Http::trustCheck(const char*) { return false; }
bool Http::isScript(const char* url) { return std::string(url).find("/asset")!=std::string::npos; }
bool Http::isRobloxSite(const char* url) { return std::string(url).find("http://www.roblox.com/")==0 || std::string(url).find("https://www.roblox.com/")==0; }
bool Http::isCached(const char*) { return false; }
}

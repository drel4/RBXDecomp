#pragma once
#include "atlstr.h"
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <strings.h>
class CPath {
public:
  CString m_strPath;
  CPath() {}
  CPath(const char* path):m_strPath(path) { normalize(); }
  void normalize() {
    for(size_t i=0;i<m_strPath.size();++i) if(m_strPath[i]=='\\') m_strPath[i]='/';
    // Legacy rbxasset URLs use mixed Fonts/fonts spelling. Resolve existing
    // components without requiring a case-insensitive Linux filesystem.
    std::string input=m_strPath, resolved=input.empty() || input[0]!='/' ? "" : "/";
    size_t begin=resolved.empty()?0:1;
    while(begin<input.size()) {
      size_t end=input.find('/',begin); if(end==std::string::npos) end=input.size();
      std::string component=input.substr(begin,end-begin);
      std::string candidate=resolved+component; struct stat st;
      if(!component.empty() && stat(candidate.c_str(),&st)!=0) {
        DIR* dir=opendir(resolved.empty()?".":resolved.c_str());
        if(dir) { dirent* entry; while((entry=readdir(dir))) if(strcasecmp(entry->d_name,component.c_str())==0) { component=entry->d_name; break; } closedir(dir); }
      }
      resolved+=component;
      if(end<input.size()) resolved+='/';
      begin=end+1;
    }
    m_strPath=resolved;
  }
  void Append(const char* value) { if(!m_strPath.empty() && m_strPath.back()!='/') m_strPath+='/'; m_strPath+=value; normalize(); }
  bool FileExists() const { struct stat st; return stat(m_strPath.c_str(),&st)==0; }
  bool IsRelative() const { return m_strPath.empty() || m_strPath.at(0)!='/'; }
  bool IsDirectory() const { struct stat st; return stat(m_strPath.c_str(),&st)==0 && S_ISDIR(st.st_mode); }
  void AddBackslash() { if(!m_strPath.empty() && m_strPath.back()!='/') m_strPath+='/'; }
  void Canonicalize() { char path[PATH_MAX]; if(realpath(m_strPath.c_str(),path)) m_strPath=path; }
  operator const char*() const { return m_strPath.c_str(); }
};

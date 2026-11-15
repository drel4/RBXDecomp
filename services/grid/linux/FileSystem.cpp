#include "util/FileSystem.h"
#include <cstdlib>
#include <sys/stat.h>
#include <stdexcept>
#include <cerrno>
namespace RBX {
static std::string directory(bool create,const char* suffix) {
  const char* root=std::getenv("GRID_CACHE");
  std::string path=root ? root : "/tmp/openrbx-cache";
  if(create && mkdir(path.c_str(),0700)!=0 && errno!=EEXIST) throw std::runtime_error("Cannot create native cache directory");
  if(suffix && *suffix) {
    path+='/'; path+=suffix;
    if(create && mkdir(path.c_str(),0700)!=0 && errno!=EEXIST) throw std::runtime_error("Cannot create native cache subdirectory");
  }
  return path+'/';
}
std::string FileSystem::getUserAppDataDirectory(bool create,const char* suffix) { return directory(create,suffix); }
std::string FileSystem::getCacheDirectory(bool create) { return directory(create,"content"); }
}

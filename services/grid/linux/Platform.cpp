#include "windows.h"
#include "util/FileSystem.h"
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <glob.h>
#include <sys/stat.h>
#include <openssl/rand.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <stdexcept>

static std::string posixPath(const char* value) { std::string path(value); for(size_t i=0;i<path.size();++i) if(path[i]=='\\') path[i]='/'; return path; }
int MessageBox(void*,const char* text,const char* title,unsigned) { std::fprintf(stderr,"Headless prompt declined: %s: %s\n",title,text); return IDNO; }
void Sleep(DWORD ms) { timespec t={ms/1000,(long)(ms%1000)*1000000}; while(nanosleep(&t,&t)<0 && errno==EINTR) {} }
BOOL QueryPerformanceCounter(LARGE_INTEGER* value) { timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) return FALSE; value->QuadPart=(int64_t)t.tv_sec*1000000000+t.tv_nsec; return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER* value) { value->QuadPart=1000000000; return TRUE; }
DWORD TlsAlloc() { pthread_key_t key; if(pthread_key_create(&key,NULL)) throw std::runtime_error("pthread TLS allocation failed"); return (DWORD)key+1; }
void* TlsGetValue(DWORD key) { return pthread_getspecific(key-1); }
BOOL TlsSetValue(DWORD key,void* value) { return pthread_setspecific(key-1,value)==0; }
HANDLE GetCurrentThread() { return NULL; }
DWORD GetCurrentThreadId() { return (DWORD)syscall(SYS_gettid); }
BOOL GetThreadTimes(HANDLE,FILETIME* creation,FILETIME* exit,FILETIME* kernel,FILETIME* user) {
  rusage usage; if(getrusage(RUSAGE_THREAD,&usage)) return FALSE;
  uint64_t k=(uint64_t)usage.ru_stime.tv_sec*10000000+usage.ru_stime.tv_usec*10;
  uint64_t u=(uint64_t)usage.ru_utime.tv_sec*10000000+usage.ru_utime.tv_usec*10;
  creation->dwLowDateTime=creation->dwHighDateTime=exit->dwLowDateTime=exit->dwHighDateTime=0;
  kernel->dwLowDateTime=k; kernel->dwHighDateTime=k>>32;
  user->dwLowDateTime=u; user->dwHighDateTime=u>>32; return TRUE;
}
int GetThreadPriority(HANDLE) { return 0; }
BOOL SetThreadPriority(HANDLE,int) { return FALSE; } // Do not silently renice simulation threads.
LONG InterlockedIncrement(long* value) { return __sync_add_and_fetch(value,1); }
void GetLocalTime(SYSTEMTIME* out) { timespec now; clock_gettime(CLOCK_REALTIME,&now); tm t; localtime_r(&now.tv_sec,&t); out->wYear=t.tm_year+1900; out->wMonth=t.tm_mon+1; out->wDay=t.tm_mday; out->wDayOfWeek=t.tm_wday; out->wHour=t.tm_hour; out->wMinute=t.tm_min; out->wSecond=t.tm_sec; out->wMilliseconds=now.tv_nsec/1000000; }
DWORD GetLastError() { return errno; }
DWORD GetEnvironmentVariableA(const char* key,char* out,DWORD size) { const char* value=getenv(key); if(!value) return 0; size_t n=strlen(value); if(n>=size) return n+1; memcpy(out,value,n+1); return n; }
BOOL SetEnvironmentVariableA(const char* key,const char* value) { return (value ? setenv(key,value,1) : unsetenv(key))==0; }
BOOL MoveFileExA(const char* src,const char* dst,DWORD) { return rename(posixPath(src).c_str(),posixPath(dst).c_str())==0; }
BOOL DeleteFile(const char* path) { return unlink(posixPath(path).c_str())==0; }
DWORD GetTempPathA(DWORD size,char* out) { const char* tmp=getenv("TMPDIR"); std::string path=tmp ? tmp : "/tmp"; if(path.back()!='/') path+='/'; if(path.size()+1>size) return path.size()+1; memcpy(out,path.c_str(),path.size()+1); return path.size(); }
DWORD GetCurrentProcessId() { return getpid(); }
BOOL CreateDirectoryA(const char* path,void*) { return mkdir(posixPath(path).c_str(),0700)==0; }
BOOL RemoveDirectoryA(const char* path) { return rmdir(posixPath(path).c_str())==0; }
struct FindFiles { glob_t entries; size_t index; };
static void findData(FindFiles* files,WIN32_FIND_DATA* out) { const char* path=files->entries.gl_pathv[files->index]; const char* name=strrchr(path,'/'); snprintf(out->cFileName,sizeof(out->cFileName),"%s",name ? name+1 : path); struct stat st; out->dwFileAttributes=stat(path,&st)==0 && S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0; }
HANDLE FindFirstFile(const char* pattern,WIN32_FIND_DATA* out) { FindFiles* f=new FindFiles(); f->index=0; std::string path=posixPath(pattern); if(path.size()>=3 && path.substr(path.size()-3)=="*.*") path.resize(path.size()-2); if(glob(path.c_str(),0,NULL,&f->entries)) { globfree(&f->entries); delete f; return INVALID_HANDLE_VALUE; } findData(f,out); return f; }
BOOL FindNextFile(HANDLE handle,WIN32_FIND_DATA* out) { FindFiles* f=(FindFiles*)handle; if(++f->index>=f->entries.gl_pathc) return FALSE; findData(f,out); return TRUE; }
BOOL FindClose(HANDLE handle) { FindFiles* f=(FindFiles*)handle; globfree(&f->entries); delete f; return TRUE; }
int CoCreateGuid(GUID* guid) { if(RAND_bytes((unsigned char*)guid,sizeof(*guid))!=1) throw std::runtime_error("GUID entropy unavailable"); guid->c=(guid->c&0x0fff)|0x4000; guid->d[0]=(guid->d[0]&0x3f)|0x80; return 0; }
int StringFromGUID2(const GUID& g,WCHAR* out,int size) { return swprintf(out,size,L"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.a,g.b,g.c,g.d[0],g.d[1],g.d[2],g.d[3],g.d[4],g.d[5],g.d[6],g.d[7])+1; }
int WideCharToMultiByte(unsigned,DWORD,const WCHAR* in,int n,char* out,int size,const char*,BOOL*) { int count=n<size?n:size; for(int i=0;i<count;++i) out[i]=(char)in[i]; return count; }
HRESULT SHGetFolderPathAndSubDir(void*,int,void*,DWORD,const char*,char* out) { std::string path=RBX::FileSystem::getUserAppDataDirectory(true,NULL); snprintf(out,MAX_PATH,"%s",path.c_str()); return S_OK; }

#include "renderlib/Profiler.h"
namespace RBX { namespace Render {
bool SpecData::glLoaded=false;
SpecData::SpecData():shaderModel(0),videoMemory(0),CPUSpeed(0),glVendor("headless"),gfxcard("headless"),cpu("native x86"),ram((int)((int64_t)sysconf(_SC_PHYS_PAGES)*sysconf(_SC_PAGESIZE)/1048576)),resolution("headless") {}
} }

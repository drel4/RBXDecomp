#pragma once
// Small source-compatibility surface backed by POSIX, not a Win32 runtime.
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define TRUE 1
#define FALSE 0
#define MAX_PATH 4096
#define S_OK 0
#define CP_ACP 0
#define MOVEFILE_REPLACE_EXISTING 1
#define FILE_ATTRIBUTE_DIRECTORY 16
#define INVALID_HANDLE_VALUE ((void*)-1)
#define THREAD_PRIORITY_BELOW_NORMAL 0
#define EXCEPTION_CONTINUE_EXECUTION 0
#define MB_YESNO 4
#define IDNO 7
#define IDYES 6
typedef uint32_t DWORD;
typedef long LONG;
typedef int BOOL;
typedef unsigned char BYTE;
typedef BYTE* PBYTE;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef wchar_t WCHAR;
typedef void* HANDLE;
typedef void* HMODULE;
typedef long HRESULT;
typedef uintptr_t ULONG_PTR;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef union { struct { DWORD LowPart, HighPart; }; uint64_t QuadPart; } ULARGE_INTEGER;
typedef union { struct { DWORD LowPart; int32_t HighPart; }; int64_t QuadPart; } LARGE_INTEGER;
typedef struct { uint16_t wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } SYSTEMTIME;
typedef struct { DWORD dwFileAttributes; char cFileName[MAX_PATH]; } WIN32_FIND_DATA;
typedef WIN32_FIND_DATA WIN32_FIND_DATAA;
#define FindFirstFileA FindFirstFile
#define FindNextFileA FindNextFile
int MessageBox(void*,const char*,const char*,unsigned);
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } GUID;
void Sleep(DWORD ms);
BOOL QueryPerformanceCounter(LARGE_INTEGER* value);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* value);
DWORD TlsAlloc();
void* TlsGetValue(DWORD key);
BOOL TlsSetValue(DWORD key, void* value);
HANDLE GetCurrentThread();
DWORD GetCurrentThreadId();
BOOL GetThreadTimes(HANDLE,FILETIME*,FILETIME*,FILETIME*,FILETIME*);
int GetThreadPriority(HANDLE);
BOOL SetThreadPriority(HANDLE,int);
LONG InterlockedIncrement(long* value);
void GetLocalTime(SYSTEMTIME* value);
DWORD GetLastError();
DWORD GetEnvironmentVariableA(const char*,char*,DWORD);
BOOL SetEnvironmentVariableA(const char*,const char*);
BOOL MoveFileExA(const char*,const char*,DWORD);
BOOL DeleteFile(const char*);
HANDLE FindFirstFile(const char*,WIN32_FIND_DATA*);
BOOL FindNextFile(HANDLE,WIN32_FIND_DATA*);
BOOL FindClose(HANDLE);
DWORD GetTempPathA(DWORD,char*);
DWORD GetCurrentProcessId();
BOOL CreateDirectoryA(const char*,void*);
BOOL RemoveDirectoryA(const char*);
#define DeleteFileA DeleteFile
int CoCreateGuid(GUID*);
int StringFromGUID2(const GUID&,WCHAR*,int);
int WideCharToMultiByte(unsigned,DWORD,const WCHAR*,int,char*,int,const char*,BOOL*);

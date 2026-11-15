#pragma once
#include "windows.h"
#define CSIDL_COMMON_APPDATA 0
HRESULT SHGetFolderPathAndSubDir(void*,int,void*,DWORD,const char*,char*);

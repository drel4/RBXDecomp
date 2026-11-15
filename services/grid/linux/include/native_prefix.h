#pragma once
#ifdef __cplusplus
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
#include <strings.h>
#include <ctime>
#include "windows.h"
// Boost 1.34 predates libstdc++'s renamed pthread feature macro and C11 TIME_UTC.
#if defined(_GLIBCXX_HAS_GTHREADS) && !defined(_GLIBCXX_HAVE_GTHR_DEFAULT)
#define _GLIBCXX_HAVE_GTHR_DEFAULT 1
#endif
#undef TIME_UTC
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _isnan std::isnan
#define _finite std::isfinite
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define __forceinline inline
#define __int64 long long
inline long long _time64(long long* result) { timespec now; clock_gettime(CLOCK_REALTIME,&now); if(result) *result=now.tv_sec; return now.tv_sec; }
#endif

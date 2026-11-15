#include "GetTime.h"
#include <time.h>
#include <stdexcept>
// RakNet's old POSIX implementation reset its epoch every 200 calls and shared
// mutable timeval objects across network threads. Use one monotonic clock, in
// the original protocol's microsecond/millisecond units, without mutable state.
RakNetTimeNS RakNet::GetTimeNS() {
    timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now)) throw std::runtime_error("Network monotonic clock unavailable");
    return (RakNetTimeNS)now.tv_sec*1000000+now.tv_nsec/1000;
}
RakNetTime RakNet::GetTime() { return (RakNetTime)(GetTimeNS()/1000); }

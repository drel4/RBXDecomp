#include "GetTime.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include <unistd.h>

int main() {
    static_assert(sizeof(RakNetTime)==4,"Client protocol uses 32-bit milliseconds");
    static_assert(sizeof(RakNetTimeNS)==8,"Client protocol uses 64-bit microseconds");
    std::atomic<bool> okay(true);
    std::vector<std::thread> readers;
    for(int n=0;n<4;++n) readers.push_back(std::thread([&]() {
        RakNetTimeNS previous=RakNet::GetTimeNS();
        for(int i=0;i<100000;++i) {
            RakNetTimeNS next=RakNet::GetTimeNS();
            if(next<previous) okay=false;
            previous=next;
        }
    }));
    for(auto& reader:readers) reader.join();
    RakNetTimeNS before=RakNet::GetTimeNS(); usleep(30000);
    RakNetTimeNS delta=RakNet::GetTimeNS()-before;
    if(!okay || delta<20000 || delta>2000000) return 1;
    RakNetTimeNS now=RakNet::GetTimeNS();
    RakNetTime millis=RakNet::GetTime();
    if((RakNetTime)(millis-(RakNetTime)(now/1000))>10) return 2;
    std::puts("NATIVE_NETWORK_CLOCK_MONOTONIC_UNITS_THREADS_PASS");
    return 0;
}

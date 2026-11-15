#include <G3D/debugAssert.h>
#include <cstdio>
namespace G3D { namespace _internal {
bool _handleDebugAssert_(const char* expression,const std::string& message,const char* file,int line,bool&,bool) {
    std::fprintf(stderr,"ENGINE_ASSERT %s:%d: %s: %s\n",file,line,expression,message.c_str());
    std::fflush(stderr);
    return true;
}
bool _handleErrorCheck_(const char* expression,const std::string& message,const char* file,int line,bool& ignore,bool gui) {
    return _handleDebugAssert_(expression,message,file,line,ignore,gui);
}
AssertionHook _debugHook=_handleDebugAssert_;
AssertionHook _failureHook=_handleErrorCheck_;
void _releaseInputGrab_() {}
void _restoreInputGrab_() {}
} // namespace _internal
void setAssertionHook(AssertionHook hook) { _internal::_debugHook=hook; }
AssertionHook assertionHook() { return _internal::_debugHook; }
void setFailureHook(AssertionHook hook) { _internal::_failureHook=hook; }
AssertionHook failureHook() { return _internal::_failureHook; }
} // namespace G3D

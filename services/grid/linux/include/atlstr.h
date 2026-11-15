#pragma once
#include <string>
#include <cstdarg>
#include <cstdio>
class CString : public std::string {
public:
  using std::string::string;
  using std::string::operator=;
  CString(const std::string& value):std::string(value){}
  operator const char*() const { return c_str(); }
  void Format(const char* fmt, ...) {
    va_list args; va_start(args,fmt); va_list copy; va_copy(copy,args);
    int size=vsnprintf(NULL,0,fmt,copy); va_end(copy);
    if(size>=0) { std::string out(size+1,'\0'); vsnprintf(&out[0],out.size(),fmt,args); assign(out.data(),size); }
    va_end(args);
  }
};
inline CString operator+(const CString& lhs,const char* rhs) { return CString(static_cast<const std::string&>(lhs)+rhs); }
inline CString operator+(const CString& lhs,const CString& rhs) { return CString(static_cast<const std::string&>(lhs)+static_cast<const std::string&>(rhs)); }

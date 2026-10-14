#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <vector>
using std::min;
#define HIGH 1
inline void digitalWrite(int, int) {}
inline unsigned long mockMillis=0;
inline unsigned long millis() { return mockMillis; }
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t)=0;
    virtual size_t write(const uint8_t *p,size_t n) {
        size_t result=0; while (result<n && write(p[result])) ++result; return result;
    }
    size_t println(const char *s="") { size_t n=write((const uint8_t*)s,strlen(s));return n+write((const uint8_t*)"\n",1); }
    size_t printf(const char *format,...) {
        va_list args,copy;va_start(args,format);va_copy(copy,args);
        int len=vsnprintf(nullptr,0,format,copy);va_end(copy);
        std::vector<char> buf(len+1);vsnprintf(buf.data(),buf.size(),format,args);va_end(args);
        return write((const uint8_t*)buf.data(),len);
    }
};

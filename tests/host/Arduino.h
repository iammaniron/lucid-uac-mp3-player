#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
class String {
    std::string s;
public:
    String(const char *p = "") : s(p) {}
    String(std::string p) : s(std::move(p)) {}
    const char *c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    int lastIndexOf(char c) const { auto p=s.rfind(c); return p==std::string::npos ? -1 : int(p); }
    String substring(size_t p) const { return s.substr(p); }
    void toLowerCase() { std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);}); }
    bool startsWith(const char *p) const { return s.rfind(p,0)==0; }
    bool endsWith(const char *p) const { std::string x(p); return s.size()>=x.size() && s.compare(s.size()-x.size(),x.size(),x)==0; }
    bool equalsIgnoreCase(const char *p) const { String a=*this,b=p; a.toLowerCase();b.toLowerCase();return a.s==b.s; }
    bool operator==(const char *p) const { return s==p; }
    bool operator<(const String &other) const { return s<other.s; }
    String operator+(const String &other) const { return s+other.s; }
};
struct HostSerial {
    void println(const char *) {}
    template<typename... Args> void printf(const char *, Args...) {}
};
inline HostSerial Serial0;

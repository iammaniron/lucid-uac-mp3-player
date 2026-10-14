#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#define FILE_READ "r"
class File {
    struct State {
        std::filesystem::path path;
        std::ifstream file;
        std::vector<std::filesystem::path> entries;
        size_t next=0;
        bool directory=false;
    };
    std::shared_ptr<State> s;
public:
    File() = default;
    explicit File(std::filesystem::path p) {
        if (!std::filesystem::exists(p)) return;
        s=std::make_shared<State>();s->path=p;s->directory=std::filesystem::is_directory(p);
        if (s->directory) for (auto &entry:std::filesystem::directory_iterator(p)) s->entries.push_back(entry.path());
        else s->file.open(p,std::ios::binary);
    }
    explicit operator bool() const { return s && (s->directory || s->file.is_open()); }
    bool isDirectory() const { return s && s->directory; }
    File openNextFile() { return s && s->next<s->entries.size() ? File(s->entries[s->next++]) : File(); }
    String name() const { return s ? s->path.filename().string() : ""; }
    void close() { s.reset(); }
    uint32_t size() const { return s && !s->directory ? std::filesystem::file_size(s->path) : 0; }
    int read(uint8_t *out,size_t bytes) { if (!s) return -1; s->file.read((char*)out,bytes);return s->file.gcount(); }
    bool seek(uint32_t pos) { if (!s) return false;s->file.clear();s->file.seekg(pos);return bool(s->file); }
};
struct HostSD {
    std::filesystem::path root;
    File open(const String &path, const char * = FILE_READ) { return File(root / std::filesystem::path(path.c_str()).relative_path()); }
};
inline HostSD SD;

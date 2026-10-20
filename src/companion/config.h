// Simple INI-style settings and logging.
#pragma once
#include <map>
#include <string>
#include <cstdio>

class Config {
public:
    bool load(const std::string& path);
    bool save();
    std::string get(const std::string& key, const std::string& def) const;
    int getInt(const std::string& key, int def) const;
    bool getBool(const std::string& key, bool def) const { return getInt(key, def ? 1 : 0) != 0; }
    void set(const std::string& key, const std::string& v) { kv_[key] = v; }
    void setInt(const std::string& key, int v) { kv_[key] = std::to_string(v); }
    void setBool(const std::string& key, bool v) { setInt(key, v ? 1 : 0); }
private:
    std::string path_;
    std::map<std::string, std::string> kv_;   // "section.key" -> value
};

class Logger {
public:
    bool open(const std::string& path);
    void log(const char* fmt, ...);
    ~Logger();
private:
    FILE* f_ = nullptr;
};
extern Logger gLog;
#define LOGF(...) gLog.log(__VA_ARGS__)

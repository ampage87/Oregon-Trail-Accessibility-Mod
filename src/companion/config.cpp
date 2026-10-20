#include "config.h"
#include "platform.h"
#include <cstdarg>
#include <cstdlib>
#include <vector>

Logger gLog;

static std::string trimS(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

bool Config::load(const std::string& path) {
    path_ = path;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512];
    std::string section;
    while (fgets(line, sizeof line, f)) {
        std::string l = trimS(line);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            size_t e = l.find(']');
            section = trimS(l.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimS(l.substr(0, eq)), v = trimS(l.substr(eq + 1));
        kv_[section.empty() ? k : section + "." + k] = v;
    }
    fclose(f);
    return true;
}

bool Config::save() {
    if (path_.empty()) return false;
    FILE* f = fopen(path_.c_str(), "w");
    if (!f) return false;
    fprintf(f, "; Oregon Trail Accessibility Mod settings.\n");
    fprintf(f, "; Most of these can also be changed in game with the function keys.\n");
    std::string cur = "\x01";
    for (auto& kv : kv_) {
        size_t dot = kv.first.find('.');
        std::string sec = dot == std::string::npos ? "" : kv.first.substr(0, dot);
        std::string key = dot == std::string::npos ? kv.first : kv.first.substr(dot + 1);
        if (sec != cur) { fprintf(f, "\n[%s]\n", sec.c_str()); cur = sec; }
        fprintf(f, "%s=%s\n", key.c_str(), kv.second.c_str());
    }
    fclose(f);
    return true;
}

std::string Config::get(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}
int Config::getInt(const std::string& key, int def) const {
    auto it = kv_.find(key);
    if (it == kv_.end() || it->second.empty()) return def;
    return atoi(it->second.c_str());
}

bool Logger::open(const std::string& path) {
    if (f_) fclose(f_);
    f_ = fopen(path.c_str(), "w");
    return f_ != nullptr;
}
void Logger::log(const char* fmt, ...) {
    if (!f_) return;
    fprintf(f_, "[%8.3f] ", nowMs() / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f_, fmt, ap);
    va_end(ap);
    fputc('\n', f_);
    fflush(f_);
}
Logger::~Logger() { if (f_) fclose(f_); }

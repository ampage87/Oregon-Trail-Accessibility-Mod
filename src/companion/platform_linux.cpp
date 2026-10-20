// Linux test driver (not shipped): attaches to a running DOSBox, prints
// speech to stdout and reads key commands from a FIFO.
//   build:  g++ -std=c++17 -O1 -o otaccess_test *.cpp   (excluding platform_win.cpp)
//   run:    ./otaccess_test <game dir> [fifo]
//   keys:   echo "key DOWN" > /tmp/otcmd   |  echo "char a" > /tmp/otcmd  |  echo "text Aaron" > ...
#ifndef _WIN32
#include "mod.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

static auto t0 = std::chrono::steady_clock::now();
uint64_t nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

class LinuxMem : public ProcessMemory {
public:
    explicit LinuxMem(int pid) : pid_(pid) {
        char p[64];
        snprintf(p, sizeof p, "/proc/%d/mem", pid);
        fd_ = open(p, O_RDWR);
    }
    ~LinuxMem() { if (fd_ >= 0) close(fd_); }
    bool read(uint64_t a, void* b, size_t n) override { return fd_ >= 0 && pread(fd_, b, n, (off_t)a) == (ssize_t)n; }
    bool write(uint64_t a, const void* b, size_t n) override { return fd_ >= 0 && pwrite(fd_, b, n, (off_t)a) == (ssize_t)n; }
    bool alive() override { char p[64]; snprintf(p, sizeof p, "/proc/%d", pid_); struct stat s; return stat(p, &s) == 0; }
    void scan(const uint8_t* pat, size_t n, std::vector<uint64_t>& hits) override {
        char p[64];
        snprintf(p, sizeof p, "/proc/%d/maps", pid_);
        std::ifstream f(p);
        std::string line;
        std::vector<uint8_t> buf;
        while (std::getline(f, line)) {
            unsigned long lo, hi;
            char perms[8];
            if (sscanf(line.c_str(), "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
            if (perms[0] != 'r' || perms[1] != 'w') continue;
            if (hi - lo > (512ul << 20)) continue;
            for (unsigned long pos = lo; pos < hi;) {
                size_t len = (size_t)std::min<unsigned long>(4ul << 20, hi - pos);
                buf.resize(len);
                if (pread(fd_, buf.data(), len, (off_t)pos) != (ssize_t)len) break;
                for (size_t i = 0; i + n <= len; i++)
                    if (buf[i] == pat[0] && memcmp(&buf[i], pat, n) == 0) hits.push_back(pos + i);
                if (pos + len >= hi) break;
                pos += len - n;
            }
        }
    }
private:
    int pid_, fd_ = -1;
};

class StdoutSpeech : public Speech {
public:
    void speak(const std::string& t, bool intr) override {
        printf("%s[%7.2f] %s\n", intr ? "!" : " ", nowMs() / 1000.0, t.c_str());
        fflush(stdout);
        until_ = nowMs() + 300 + t.size() * 55;
    }
    void silence() override { printf("  (silence)\n"); until_ = 0; }
    bool isSpeaking() override { return nowMs() < until_; }
    std::string name() override { return "stdout"; }
private:
    uint64_t until_ = 0;
};

static KeyEvent parseKey(const std::string& name, bool shift, bool ctrl) {
    KeyEvent k;
    k.shift = shift; k.ctrl = ctrl;
    static const std::pair<const char*, KeyCode> t[] = {
        {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"TAB", KEY_TAB},
        {"ENTER", KEY_ENTER}, {"ESC", KEY_ESCAPE}, {"SPACE", KEY_SPACE}, {"BACK", KEY_BACKSPACE},
        {"HOME", KEY_HOME}, {"END", KEY_END}, {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3}, {"F4", KEY_F4},
        {"F5", KEY_F5}, {"F6", KEY_F6}, {"F7", KEY_F7}, {"F8", KEY_F8}, {"F9", KEY_F9}, {"F11", KEY_F11},
        {"F12", KEY_F12}, {"CTRL", KEY_CTRL_ONLY}};
    for (auto& e : t) if (name == e.first) k.code = e.second;
    return k;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <game dir> [fifo] [pid]\n", argv[0]); return 1; }
    std::string fifo = argc > 2 ? argv[2] : "/tmp/otcmd";
    int pid = argc > 3 ? atoi(argv[3]) : 0;
    if (!pid) {
        FILE* p = popen("pgrep -n -x dosbox", "r");
        if (p) { if (fscanf(p, "%d", &pid) != 1) pid = 0; pclose(p); }
    }
    if (!pid) { fprintf(stderr, "no dosbox process\n"); return 1; }
    gLog.open("/tmp/otaccess_test.log");
    mkfifo(fifo.c_str(), 0666);
    int ffd = open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    LinuxMem mem(pid);
    StdoutSpeech sp;
    Config cfg;
    cfg.load("/tmp/otaccess_test.ini");
    Guest g(&mem);
    Mod mod(g, sp, cfg, argv[1]);
    std::string acc;
    while (mem.alive()) {
        mod.poll();
        char buf[512];
        ssize_t n = read(ffd, buf, sizeof buf);
        if (n > 0) acc.append(buf, (size_t)n);
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, nl);
            acc.erase(0, nl + 1);
            std::istringstream is(line);
            std::string cmd, arg;
            is >> cmd;
            std::getline(is, arg);
            if (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
            if (cmd == "quit") return 0;
            if (cmd == "key") {
                std::istringstream as(arg);
                std::string nm, m;
                as >> nm;
                bool sh = false, ct = false;
                while (as >> m) { if (m == "shift") sh = true; if (m == "ctrl") ct = true; }
                mod.onKey(parseKey(nm, sh, ct));
            } else if (cmd == "char" && !arg.empty()) {
                KeyEvent k; k.code = KEY_CHAR; k.ch = arg[0]; mod.onKey(k);
            } else if (cmd == "text") {
                for (char c : arg) { KeyEvent k; k.code = c == ' ' ? KEY_SPACE : KEY_CHAR; k.ch = c; mod.onKey(k);
                                     for (int i = 0; i < 4; i++) { mod.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(30)); } }
            }
        }
        if (n == 0) { close(ffd); ffd = open(fifo.c_str(), O_RDONLY | O_NONBLOCK); }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return 0;
}
#endif

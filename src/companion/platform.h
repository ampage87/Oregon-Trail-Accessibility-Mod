// Platform abstraction: process memory access and speech output.
// Windows implementations live in platform_win.cpp, the Linux test
// harness implementations in platform_linux.cpp.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class ProcessMemory {
public:
    virtual ~ProcessMemory() {}
    virtual bool read(uint64_t addr, void* buf, size_t n) = 0;
    virtual bool write(uint64_t addr, const void* buf, size_t n) = 0;
    // Scan readable+writable memory of the target for a byte pattern.
    // Returns host addresses of every hit.
    virtual void scan(const uint8_t* pat, size_t n, std::vector<uint64_t>& hits) = 0;
    virtual bool alive() = 0;
};

class Speech {
public:
    virtual ~Speech() {}
    // interrupt = stop anything currently being spoken first
    virtual void speak(const std::string& text, bool interrupt) = 0;
    virtual void silence() = 0;
    // best effort: true while speech is still playing (exact for SAPI,
    // estimated for screen readers that cannot report it)
    virtual bool isSpeaking() = 0;
    virtual std::string name() = 0;
};

// Short sound cues.  Names: "field_on", "field_off" (entering / leaving a
// text box), "water" (rafting, looped).
class Sound {
public:
    virtual ~Sound() {}
    virtual void play(const std::string& name) = 0;
    virtual void loop(const std::string& name, bool on) = 0;
};

// milliseconds since program start
uint64_t nowMs();

#include "patcher.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct Patch {
    unsigned long offset;
    const char* original;      // hex
    const char* replacement;   // hex
    const char* what;
};

// "CD 61 id" = INT 61h followed by the hook number (see otmod.asm).
const Patch kPatches[] = {
    {0x019831, "558BEC", "CD6101", "text output (m17B8:00B1)"},
    {0x02EB87, "8B56D28B46D0", "CD6102909090", "CTR loaded (o12:0E17)"},
    {0x02F297, "558BEC", "CD6103", "DoDialog (o12:1527)"},
    {0x02EBBD, "558BEC", "CD6104", "FreeDialog (o12:0E4D)"},
    {0x012524, "558BEC", "CD6105", "evGetEvent (m107C:0164)"},
    {0x02FF90, "558BEC", "CD6106", "game start/state (o15:00E0)"},
    {0x028199, "558BEC", "CD6107", "hunt sprites (o6:1949)"},
    {0x02B8FF, "558BEC", "CD6108", "raft update (o10:057F)"},
    {0x02ECE2, "558BEC", "CD6109", "DrawDialog (o12:0F72)"},
};
const size_t kExeSize = 273648;

std::vector<unsigned char> hex(const char* s) {
    std::vector<unsigned char> v;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
        unsigned x = 0;
        sscanf(s + i, "%2x", &x);
        v.push_back((unsigned char)x);
    }
    return v;
}

bool readFile(const std::string& p, std::vector<unsigned char>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

}  // namespace

PatchResult ensurePatchedExe(const std::string& gameDir) {
    PatchResult r;
    std::string src = gameDir + "\\OREGON.EXE", dst = gameDir + "\\OREGONM.EXE";
#ifndef _WIN32
    src = gameDir + "/OREGON.EXE"; dst = gameDir + "/OREGONM.EXE";
#endif
    std::vector<unsigned char> d;
    if (!readFile(src, d)) { r.message = "OREGON.EXE was not found in " + gameDir; return r; }
    if (d.size() != kExeSize) {
        r.message = "OREGON.EXE is not the expected version (size " + std::to_string(d.size()) +
                    "; the mod needs The Oregon Trail Deluxe for DOS, version 3.0, size 273648).";
        return r;
    }
    for (const Patch& p : kPatches) {
        std::vector<unsigned char> o = hex(p.original), n = hex(p.replacement);
        if (memcmp(&d[p.offset], o.data(), o.size()) != 0) {
            r.message = std::string("OREGON.EXE does not match the expected bytes at ") + p.what +
                        ". Is it already modified?";
            return r;
        }
        memcpy(&d[p.offset], n.data(), n.size());
    }
    std::vector<unsigned char> cur;
    if (readFile(dst, cur) && cur == d) { r.ok = true; r.message = "OREGONM.EXE is up to date."; return r; }
    FILE* f = fopen(dst.c_str(), "wb");
    if (!f) { r.message = "Could not write " + dst; return r; }
    bool good = fwrite(d.data(), 1, d.size(), f) == d.size();
    good = (fclose(f) == 0) && good;
    if (!good) { r.message = "Could not write " + dst; return r; }
    r.ok = true;
    r.written = true;
    r.message = "Created OREGONM.EXE.";
    return r;
}

#include "guest.h"
#include <cstring>

static const uint8_t kSig[16] = {'O','T','A','C','C','E','S','S','-','M','B','O','X','-','1',0};

bool Guest::attach() {
    std::vector<uint64_t> hits;
    pm_->scan(kSig, sizeof kSig, hits);
    for (uint64_t h : hits) {
        uint32_t self = 0;
        if (!pm_->read(h + MB::SELF, &self, 4)) continue;
        if (self < 0x400 || self >= 0xA0000) continue;
        membase_ = h - self;
        mbox_ = self;
        uint8_t chk[16];
        if (read(mbox_, chk, 16) && memcmp(chk, kSig, 16) == 0) {
            rsize_ = mb16(MB::RSIZE);
            roff_ = mb16(MB::ROFF);
            if (rsize_ == 0 || (rsize_ & (rsize_ - 1)) || rsize_ > 32768) rsize_ = 8192;
            rbuf_.resize(rsize_);
            ringReset();
            return true;
        }
        mbox_ = 0;
        membase_ = 0;
    }
    return false;
}

bool Guest::stillValid() {
    if (!mbox_) return false;
    uint8_t chk[16];
    return read(mbox_, chk, 16) && memcmp(chk, kSig, 16) == 0;
}

bool Guest::read(uint32_t lin, void* buf, size_t n) {
    if (!mbox_) { memset(buf, 0, n); return false; }
    if (lin + n > 0x110000) { memset(buf, 0, n); return false; }
    if (!pm_->read(membase_ + lin, buf, n)) { memset(buf, 0, n); return false; }
    return true;
}
bool Guest::write(uint32_t lin, const void* buf, size_t n) {
    if (!mbox_ || lin + n > 0x110000) return false;
    return pm_->write(membase_ + lin, buf, n);
}
uint8_t Guest::rd8(uint32_t lin) { uint8_t v = 0; read(lin, &v, 1); return v; }
uint16_t Guest::rd16(uint32_t lin) { uint8_t b[2] = {0, 0}; read(lin, b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }
uint32_t Guest::rd32(uint32_t lin) { return rd16(lin) | ((uint32_t)rd16(lin + 2) << 16); }
FarPtr Guest::rdFar(uint32_t lin) { FarPtr p; p.off = rd16(lin); p.seg = rd16(lin + 2); return p; }
void Guest::wr16(uint32_t lin, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; write(lin, b, 2); }
std::string Guest::rdStr(uint32_t lin, size_t max) {
    std::string s;
    std::vector<char> b(max);
    if (!read(lin, b.data(), max)) return s;
    for (size_t i = 0; i < max && b[i]; i++) s += b[i];
    return s;
}

bool Guest::queueFree() {
    uint16_t h = mb16(MB::INQH), t = mb16(MB::INQT);
    return (uint16_t)(h - t) <= MB::INQSIZE - 2;
}

bool Guest::inject(int type, int a, int b, int c) {
    if (!mbox_) return false;
    uint16_t h = mb16(MB::INQH), t = mb16(MB::INQT);
    if ((uint16_t)(h - t) >= MB::INQSIZE) return false;
    uint8_t e[8] = {(uint8_t)type, (uint8_t)(type >> 8), (uint8_t)a, (uint8_t)(a >> 8),
                    (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)c, (uint8_t)(c >> 8)};
    write(mbox_ + MB::INQ + (h & (MB::INQSIZE - 1)) * 8, e, 8);
    setMb16(MB::INQH, (uint16_t)(h + 1));
    return true;
}

void Guest::ringReset() {
    // Start from the oldest complete record still in the ring so that a
    // late attach still learns which screens are up.
    uint16_t head = mb16(MB::RHEAD);
    rpos_ = head;
    if (!read(mbox_ + roff_, rbuf_.data(), rsize_)) return;
    const uint32_t mask = rsize_ - 1u;
    uint16_t seq = mb16(MB::RSEQ);
    if (seq == 0) return;
    for (uint32_t skip = 0; skip < 600; skip++) {
        // skip 0 tries offset 0 (ring not yet wrapped), then scan past head
        uint32_t start = skip == 0 ? 0 : (head + skip) & mask;
        if (skip == 0 && head == 0) continue;
        uint32_t p = start;
        uint32_t walked = 0;
        bool ok = true;
        while (walked < rsize_) {
            if (p == head) break;
            if (rbuf_[p] != 0xA5) { ok = false; break; }
            uint32_t len = rbuf_[(p + 12) & mask];
            p = (p + 13 + len) & mask;
            walked += 13 + len;
            if (walked > rsize_) { ok = false; break; }
        }
        if (ok && p == head) { rpos_ = (uint16_t)start; return; }
    }
}

void Guest::ringRead(std::vector<RingRecord>& out) {
    if (!mbox_) return;
    uint16_t head = mb16(MB::RHEAD);
    if (head == rpos_) return;
    if (!read(mbox_ + roff_, rbuf_.data(), rsize_)) return;
    const uint16_t mask = rsize_ - 1;
    auto b = [&](uint32_t i) -> uint8_t { return rbuf_[i & mask]; };
    auto w = [&](uint32_t i) -> uint16_t { return (uint16_t)(b(i) | (b(i + 1) << 8)); };
    uint32_t pos = rpos_;
    uint32_t avail = (uint16_t)(head - rpos_) & mask;
    uint64_t t = nowMs();
    while (avail >= 13) {
        if (b(pos) != 0xA5) { rpos_ = head; return; }   // lost sync
        RingRecord r;
        r.type = b(pos + 1);
        r.seq = w(pos + 2);
        r.p1 = (int16_t)w(pos + 4);
        r.p2 = (int16_t)w(pos + 6);
        r.p3 = w(pos + 8);
        r.p4 = w(pos + 10);
        uint32_t len = b(pos + 12);
        if (13 + len > avail) break;
        for (uint32_t i = 0; i < len; i++) r.text += (char)b(pos + 13 + i);
        r.when = t;
        out.push_back(r);
        pos = (pos + 13 + len) & mask;
        avail -= 13 + len;
    }
    rpos_ = (uint16_t)pos;
}

uint16_t biosKeyForChar(char c) {
    static const char* row1 = "1234567890-=";     // 0x02..0x0D
    static const char* row1s = "!@#$%^&*()_+";
    static const char* row2 = "qwertyuiop[]";     // 0x10..0x1B
    static const char* row2s = "QWERTYUIOP{}";
    static const char* row3 = "asdfghjkl;'`";     // 0x1E..0x29
    static const char* row3s = "ASDFGHJKL:\"~";
    static const char* row4 = "zxcvbnm,./";       // 0x2C..0x35
    static const char* row4s = "ZXCVBNM<>?";
    unsigned char uc = (unsigned char)c;
    for (int i = 0; row1[i]; i++) if (c == row1[i] || c == row1s[i]) return (uint16_t)(((0x02 + i) << 8) | uc);
    for (int i = 0; row2[i]; i++) if (c == row2[i] || c == row2s[i]) return (uint16_t)(((0x10 + i) << 8) | uc);
    for (int i = 0; row3[i]; i++) if (c == row3[i] || c == row3s[i]) return (uint16_t)(((0x1E + i) << 8) | uc);
    for (int i = 0; row4[i]; i++) if (c == row4[i] || c == row4s[i]) return (uint16_t)(((0x2C + i) << 8) | uc);
    if (c == '\\' || c == '|') return (uint16_t)((0x2B << 8) | uc);
    if (c == ' ') return BK_SPACE;
    return 0;
}

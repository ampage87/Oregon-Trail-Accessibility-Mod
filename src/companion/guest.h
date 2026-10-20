// Access to the emulated DOS machine through the OTMOD mailbox.
#pragma once
#include "platform.h"
#include <cstdint>
#include <string>
#include <vector>

// Mailbox layout - keep in sync with src/dos/otmod.asm
namespace MB {
enum : uint32_t {
    SIG = 0x00, SELF = 0x10, VER = 0x14, CS_EVT = 0x16, DGROUP = 0x18,
    HEART = 0x1A, POLLS = 0x1C, HOLD = 0x1E, RHEAD = 0x20, RSEQ = 0x22,
    RSIZE = 0x24, ROFF = 0x26, STATE = 0x28, DLG = 0x2C, HUNT = 0x30,
    RAFT = 0x34, INQH = 0x38, INQT = 0x3A, HOLDING = 0x3C, RELPEND = 0x3E,
    INQ = 0x40, LASTID = 0xC4, HOOKS = 0xC6, DROPPED = 0xC8, FLAGS = 0xCA,
    DTOFF = 0xCC, DTN = 0xCE, DSEQ = 0xD0, DTSZ = 20
};
enum { INQSIZE = 16 };
}

// ring record types
enum RecType { R_TEXT = 1, R_CTR = 2, R_DLGRUN = 3, R_DLGFREE = 4, R_STATE = 6, R_HUNT = 7, R_RAFT = 8, R_DLGDRAW = 9 };
// input queue commands
enum InCmd { I_KEY = 1, I_CLICK = 2, I_MOVE = 3, I_PRESS = 4, I_RELEASE = 5 };

struct RingRecord {
    int type = 0;
    uint16_t seq = 0;
    int16_t p1 = 0, p2 = 0;      // text: x,y   dialog records: ptr off,seg
    uint16_t p3 = 0, p4 = 0;     // text: caller return off,seg
    std::string text;
    uint64_t when = 0;           // ms
};

// far pointer helper
struct FarPtr {
    uint16_t off = 0, seg = 0;
    uint32_t lin() const { return ((uint32_t)seg << 4) + off; }
    bool null() const { return off == 0 && seg == 0; }
    bool operator==(const FarPtr& o) const { return off == o.off && seg == o.seg; }
    bool operator!=(const FarPtr& o) const { return !(*this == o); }
};

class Guest {
public:
    explicit Guest(ProcessMemory* pm) : pm_(pm) {}
    // locate the mailbox; returns true when attached
    bool attach();
    bool attached() const { return mbox_ != 0; }
    void detach() { mbox_ = 0; membase_ = 0; }
    bool stillValid();          // signature still there?

    // raw guest memory (linear addresses)
    bool read(uint32_t lin, void* buf, size_t n);
    bool write(uint32_t lin, const void* buf, size_t n);
    uint16_t rd16(uint32_t lin);
    int16_t rds16(uint32_t lin) { return (int16_t)rd16(lin); }
    uint32_t rd32(uint32_t lin);
    uint8_t rd8(uint32_t lin);
    FarPtr rdFar(uint32_t lin);
    std::string rdStr(uint32_t lin, size_t max = 256);
    void wr16(uint32_t lin, uint16_t v);

    // mailbox
    uint32_t mbox() const { return mbox_; }
    uint16_t mb16(uint32_t off) { return rd16(mbox_ + off); }
    FarPtr mbFar(uint32_t off) { return rdFar(mbox_ + off); }
    void setMb16(uint32_t off, uint16_t v) { wr16(mbox_ + off, v); }
    uint16_t dgroup() { return mb16(MB::DGROUP); }
    uint32_t dgLin(uint16_t off) { return ((uint32_t)dgroup() << 4) + off; }

    // input
    bool inject(int type, int a = 0, int b = 0, int c = 0);
    bool queueFree();           // room for at least 2 entries?
    bool key(uint16_t biosWord) { return inject(I_KEY, biosWord); }
    bool click(int x, int y) { return inject(I_CLICK, x, y); }
    bool moveCursor(int x, int y) { return inject(I_MOVE, x, y); }
    void setHold(bool on) { setMb16(MB::HOLD, on ? 1 : 0); }
    bool holding() { return mb16(MB::HOLDING) != 0; }

    // ring
    void ringReset();           // skip everything written so far
    void ringRead(std::vector<RingRecord>& out);

private:
    ProcessMemory* pm_;
    uint64_t membase_ = 0;
    uint32_t mbox_ = 0;
    uint16_t rpos_ = 0;
    uint16_t rsize_ = 8192;
    uint16_t roff_ = 0x100;
    std::vector<uint8_t> rbuf_;
};

// translate a printable character to a BIOS keyboard word (US layout)
uint16_t biosKeyForChar(char c);
enum BiosKey : uint16_t {
    BK_ENTER = 0x1C0D, BK_ESC = 0x011B, BK_BACKSPACE = 0x0E08, BK_TAB = 0x0F09,
    BK_SHIFTTAB = 0x0F00, BK_F1 = 0x3B00, BK_UP = 0x4800, BK_DOWN = 0x5000,
    BK_LEFT = 0x4B00, BK_RIGHT = 0x4D00, BK_DEL = 0x5300, BK_HOME = 0x4700, BK_END = 0x4F00,
    BK_SPACE = 0x3920
};

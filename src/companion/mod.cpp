#include "mod.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// relative segment of the word-wrapping message routine (m18EC)
static const int SEG_WRAP = 0x18EC;
static const int SEG_EVT = 0x107C;

static bool isDateLine(const std::string& t) {
    static const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                   "August", "September", "October", "November", "December"};
    for (auto m : months) {
        size_t n = strlen(m);
        if (t.compare(0, n, m) == 0 && t.size() > n + 2 && t[n] == ' ' && isdigit((unsigned char)t[n + 1]) &&
            t.find(", 18") != std::string::npos)
            return true;
    }
    return false;
}

static bool numericish(const std::string& t) {
    std::string s = trim(t);
    if (s.empty()) return true;
    for (char c : s)
        if (!(isdigit((unsigned char)c) || c == '$' || c == '.' || c == ',' || c == ' ')) return false;
    return true;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

Mod::Mod(Guest& g, Speech& sp, Config& cfg, const std::string& gameDir)
    : g_(g), sp_(sp), cfg_(cfg), gameDir_(gameDir) {
    if (!gxl_.open(gameDir + "/OREGON.GXL")) LOGF("could not open %s/OREGON.GXL", gameDir.c_str());
    autoSteer_ = cfg_.getBool("rafting.auto_steer", true);
}

void Mod::say(const std::string& text, bool interrupt) {
    std::string t = trim(text);
    if (t.empty()) return;
    lastSpoken_ = t;
    LOGF("SAY%s: %s", interrupt ? "!" : "", t.c_str());
    sp_.speak(t, interrupt);
}

// =====================================================================
// main loop
// =====================================================================
void Mod::poll() {
    uint64_t t = nowMs();
    if (!attached_) {
        if (t - lastAttachTry_ > 700) {
            lastAttachTry_ = t;
            if (g_.attach()) {
                attached_ = true;
                dialogs_.clear(); items_.clear(); controls_.clear(); focus_ = -1;
                statePtr_ = g_.mbFar(MB::STATE);
                huntPtr_ = FarPtr(); raftPtr_ = FarPtr();
                LOGF("attached: mailbox at %05X dgroup %04X", g_.mbox(), g_.dgroup());
                say("The Oregon Trail accessibility mod is connected. Press F1 for help.", false);
                // rebuild screen context from the ring history without speaking it
                std::vector<RingRecord> old;
                g_.ringRead(old);
                replaying_ = true;
                for (auto& r : old) handleRecord(r);
                reconcileDialogs();         // screens whose records left the ring
                finishLogRender();          // marks the visible log as already heard
                replaying_ = false;
                pending_.clear();
                pendingInterrupt_ = false;
                holdByEvent_ = false;
                controlsDirty_ = nowMs() + 50;
            }
        }
        return;
    }
    if (!g_.stillValid()) {
        attached_ = false;
        g_.detach();
        dialogs_.clear(); items_.clear(); controls_.clear();
        LOGF("mailbox lost");
        say("The game has closed.", true);
        return;
    }
    std::vector<RingRecord> recs;
    g_.ringRead(recs);
    for (auto& r : recs) handleRecord(r);
    if (!logRender_.empty() && t - logRenderLast_ > 180) finishLogRender();
    flushPending(false);
    if (controlsDirty_ && t >= controlsDirty_ && pending_.empty()) {
        controlsDirty_ = 0;
        refreshControls(true);
    }
    if (t - lastStateRead_ > 150) {
        lastStateRead_ = t;
        readState();
        travelTick();
    }
    if (storeRowAt_ && t >= storeRowAt_) {
        storeRowAt_ = 0;
        refreshControls(false);
        if (storeRowIdx_ >= 0 && storeRowIdx_ < (int)controls_.size()) {
            const Control& c = controls_[storeRowIdx_];
            if (inStore()) say(storeRow(c) + ". " + storeTotals(), true);
            else { std::string v = fieldValue(c); say("Done editing. " + c.label + ", " + (v.empty() ? "blank" : v), true); }
        }
    }
    if (storeTotalsAt_ && t >= storeTotalsAt_) {
        storeTotalsAt_ = 0;
        if (inStore()) {
            std::string tot = storeTotals();
            if (tot.find("You have") != std::string::npos) say(tot, false);
        }
    }
    if (pendingClickAt_ && t >= pendingClickAt_) {
        pendingClickAt_ = 0;
        clickAt(pendingClickX_, pendingClickY_);
    }
    if (listWantAt_ && t >= listWantAt_) {
        listWantAt_ = 0;
        refreshControls(false);
        for (size_t i = 0; i < controls_.size(); i++)
            if (controls_[i].type == 15 && controls_[i].value == listWant_) { focusControl((int)i, true); break; }
    }
    if (hunting_) huntTick();
    if (rafting_) raftTick();
    applyHold();
}

// The helper keeps a table of loaded screen scripts in the mailbox (layout 3+).
// After a late attach the ring may no longer hold the records for screens that
// are still up (the trail screen under a pop-up, say), so rebuild the stack
// from that table.
void Mod::reconcileDialogs() {
    if (g_.mb16(MB::VER) < 3) return;
    int n = g_.mb16(MB::DTN), off = g_.mb16(MB::DTOFF);
    if (n <= 0 || n > 64 || off < 0x40 || off > 0x1000) return;
    struct E { FarPtr p; int seq; std::string name; };
    std::vector<E> live;
    for (int i = 0; i < n; i++) {
        uint32_t a = g_.mbox() + (uint32_t)off + (uint32_t)i * MB::DTSZ;
        E e;
        e.p.off = g_.rd16(a); e.p.seg = g_.rd16(a + 2);
        if (e.p.seg == 0) continue;
        e.seq = g_.rd16(a + 4);
        e.name = upper(g_.rdStr(a + 6, 14));
        if (e.name.empty()) continue;
        live.push_back(e);
    }
    std::sort(live.begin(), live.end(), [](const E& a, const E& b) { return (int16_t)(a.seq - b.seq) < 0; });
    // drop screens the table says are gone
    dialogs_.erase(std::remove_if(dialogs_.begin(), dialogs_.end(), [&](const Dialog& d) {
        for (auto& e : live) if (e.p == d.ptr) return false;
        LOGF("reconcile: drop %s", d.ctr.c_str());
        return true;
    }), dialogs_.end());
    // add the missing ones
    for (auto& e : live) {
        bool have = false;
        for (auto& d : dialogs_) if (d.ptr == e.p) have = true;
        if (have) continue;
        RingRecord r;
        r.type = R_CTR; r.p1 = (int16_t)e.p.off; r.p2 = (int16_t)e.p.seg; r.text = e.name; r.when = nowMs();
        LOGF("reconcile: add %s", e.name.c_str());
        lastRecType_ = 0;
        onCtr(r);
    }
    // stack order = load order
    std::map<uint32_t, int> order;
    for (size_t i = 0; i < live.size(); i++) order[live[i].p.lin()] = (int)i;
    std::stable_sort(dialogs_.begin(), dialogs_.end(), [&](const Dialog& a, const Dialog& b) {
        return order[a.ptr.lin()] < order[b.ptr.lin()];
    });
    onTrail_ = false;
    for (auto& d : dialogs_) if (d.ctr == "TRAIL.CTR") onTrail_ = true;
}

void Mod::onDlgDraw(const RingRecord& r) {
    drawSeen_ = true;
    FarPtr p; p.off = (uint16_t)r.p1; p.seg = (uint16_t)r.p2;
    for (size_t i = 0; i < dialogs_.size(); i++) {
        if (dialogs_[i].ptr != p) continue;
        Dialog d = dialogs_[i];
        if (d.hidden) {
            dialogs_.erase(dialogs_.begin() + (long)i);
            LOGF("SHOW %s", d.ctr.c_str());
            showDialog(d);
        } else if (i + 1 < dialogs_.size() && d.interactive) {
            // an interactive screen drawn again over others is back on top
            // (an error box was dismissed, say)
            bool coveredByInteractive = false;
            for (size_t k = i + 1; k < dialogs_.size(); k++) if (dialogs_[k].interactive) coveredByInteractive = true;
            if (coveredByInteractive && d.rw < 560) {
                dialogs_.erase(dialogs_.begin() + (long)i);
                dialogs_.push_back(d);
                LOGF("RAISE %s", d.ctr.c_str());
                controlsDirty_ = nowMs() + 120;
            }
        }
        break;
    }
}

void Mod::handleRecord(const RingRecord& r) {
    struct Last { Mod* m; int t; ~Last() { m->lastRecType_ = t; } } last{this, r.type};
    switch (r.type) {
    case R_TEXT: onText(r); break;
    case R_CTR: onCtr(r); break;
    case R_DLGRUN: onDlgRun(r); break;
    case R_DLGFREE: onDlgFree(r); break;
    case R_DLGDRAW: onDlgDraw(r); break;
    case R_STATE: onState(r); break;
    case R_HUNT:
        huntPtr_.off = (uint16_t)r.p1; huntPtr_.seg = (uint16_t)r.p2;
        LOGF("hunt table %04X:%04X", huntPtr_.seg, huntPtr_.off);
        break;
    case R_RAFT:
        raftPtr_.off = (uint16_t)r.p1; raftPtr_.seg = (uint16_t)r.p2;
        LOGF("raft table %04X:%04X", raftPtr_.seg, raftPtr_.off);
        break;
    default: break;
    }
}

// =====================================================================
// dialogs
// =====================================================================
Dialog* Mod::dialogBySerial(int s) {
    for (auto& d : dialogs_) if (d.serial == s) return &d;
    return nullptr;
}
Dialog* Mod::topDialog() { return dialogs_.empty() ? nullptr : &dialogs_.back(); }

Dialog* Mod::activeDialog() {
    // the most recent live dialog that has something to press
    for (int i = (int)dialogs_.size() - 1; i >= 0; i--)
        if (dialogs_[i].interactive && !dialogs_[i].hidden) return &dialogs_[i];
    return nullptr;
}

void Mod::onCtr(const RingRecord& r) {
    Dialog d;
    d.serial = nextSerial_++;
    d.ctr = upper(trim(r.text));
    d.ptr.off = (uint16_t)r.p1;
    d.ptr.seg = (uint16_t)r.p2;
    d.when = r.when;
    std::string data;
    if (gxl_.get(d.ctr, data)) d.images = ctrImageList(data);
    std::vector<Control> cs;
    readControls(d, cs);
    static const char* full[] = {"TITLE.CTR", "TRAIL.CTR", "HUNT.CTR", "RAFTING.CTR", "LEGEND.CTR",
                                 "VALLEY.CTR", "DEATH.CTR", "SCORE1.CTR", "SCORE2.CTR", "WHOAMI.CTR",
                                 "STORE1.CTR", "STORE2.CTR"};
    bool isFull = false;
    for (auto f : full) if (d.ctr == f) isFull = true;
    auto sane = [](const Control& c) { return c.x >= 0 && c.y >= 0 && c.w > 0 && c.h > 0 && c.x + c.w <= 660 && c.y + c.h <= 500; };
    bool region = isFull;
    for (auto& c : cs) {
        if (c.actionable() || (c.type == 14 && d.ctr != "TRAILLOG.CTR")) d.interactive = true;
        if (!region && c.type == 12 && c.w > 40 && c.h > 20 && sane(c)) {
            d.rx = c.x; d.ry = c.y; d.rw = c.w; d.rh = c.h; region = true;
        }
    }
    if (!region && !cs.empty()) {
        // no backdrop: bounding box of the interactive controls
        int x0 = 9999, y0 = 9999, x1 = 0, y1 = 0;
        for (auto& c : cs) {
            if (!(c.actionable() || c.type == 14) || !sane(c)) continue;
            x0 = std::min(x0, c.x); y0 = std::min(y0, c.y);
            x1 = std::max(x1, c.x + c.w); y1 = std::max(y1, c.y + c.h);
        }
        if (x1 > x0 && y1 > y0) { d.rx = x0; d.ry = y0; d.rw = x1 - x0; d.rh = y1 - y0; }
    }
    // Screens loaded straight after another one, with nothing drawn in
    // between, are usually loaded ahead of time (a file box loads its error
    // boxes too).  They stay hidden until the game draws them.
    d.hidden = lastRecType_ == R_CTR && drawSeen_ && d.ctr != "TRAILLOG.CTR";
    LOGF("CTR %s ptr %04X:%04X controls=%d region=%d,%d %dx%d%s", d.ctr.c_str(), d.ptr.seg, d.ptr.off,
         (int)cs.size(), d.rx, d.ry, d.rw, d.rh, d.hidden ? " (hidden)" : "");
    if (d.hidden) { dialogs_.insert(dialogs_.begin(), d); return; }
    showDialog(d);
}

void Mod::showDialog(Dialog d) {
    d.hidden = false;
    bool fullScreen = d.rw >= 560 && d.rh >= 400;
    if (fullScreen) {
        items_.clear();
        // a full screen replaces everything except dialogs still alive below
        // (their texts were overwritten anyway)
    } else {
        // drop texts that the new region covers
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const TextItem& it) {
                         return it.x >= d.rx && it.x < d.rx + d.rw && it.y >= d.ry && it.y < d.ry + d.rh;
                     }), items_.end());
    }
    if (d.ctr == "TRAIL.CTR") {
        onTrail_ = true;
        logShown_.clear();
        // give blind hunters more time (Options > Hunting Time, DS:7798)
        // once per session, so a choice made later in Options still counts
        int ht = cfg_.getInt("hunting.time_seconds", 120);
        if (!huntTimeSet_ && ht >= 20 && ht <= 300 && g_.dgroup()) {
            g_.wr16(g_.dgLin(0x7798), (uint16_t)ht);
            huntTimeSet_ = true;
        }
    }
    if (d.ctr == "HUNT.CTR") {
        hunting_ = true;
        huntLive_.clear();
        huntTracks_.clear();
        huntBullets_.clear();
        huntTarget_ = -1;
        huntTargetKind_ = -1;
        autoHunt_ = false;
        if (!replaying_) say("Hunting. Animals will be announced as they appear. Press Space to fire at the selected animal, "
            "left and right arrows to choose an animal, A for automatic hunting, L to list animals, "
            "M to move to a new area, E to end hunting.", true);
    }
    if (d.ctr != "RAFTING.CTR" && rafting_) {
        rafting_ = false;
        raftPtr_ = FarPtr();
        if (raftSound_) { raftSound_ = false; cueLoop("water", false); }
        LOGF("rafting ended");
    }
    if (d.ctr == "RAFTING.CTR") {
        rafting_ = true;
        rockTrack_.clear(); manualTarget_ = -1; lastRaftX_ = INT32_MIN;
        rockWarned_.clear();
        if (!replaying_)
            say(std::string("Rafting down the Columbia River. ") +
                (autoSteer_ ? "Automatic steering is on; press A to steer yourself with the left and right arrows. "
                            : "Steer with the left and right arrows; rocks will be announced. Press A for automatic steering. ") +
                "Choose Begin rafting to start.", true);
    }
    std::string title = screenTitle(d.ctr);
    bool attract = (d.ctr == "TITLE.CTR" || d.ctr == "LEGEND.CTR") &&
                   (lastAnnouncedScreen_ == "TITLE.CTR" || lastAnnouncedScreen_ == "LEGEND.CTR");
    if (d.ctr == "LEGEND.CTR") attract = true;     // the title screen's idle loop
    if (!attract) {
        if (fullScreen && !title.empty() && d.ctr != "HUNT.CTR") queueText(title + ".", -1, true);
        else if (fullScreen) pendingInterrupt_ = true;
    }
    dialogs_.push_back(d);
    // the store draws its item names after the screen itself
    controlsDirty_ = nowMs() + ((d.ctr == "STORE1.CTR" || d.ctr == "STORE2.CTR") ? 700 : 120);
}

void Mod::onDlgRun(const RingRecord& r) {
    FarPtr p; p.off = (uint16_t)r.p1; p.seg = (uint16_t)r.p2;
    for (auto& d : dialogs_) if (d.ptr == p) { d.run = true; LOGF("RUN %s", d.ctr.c_str()); }
    controlsDirty_ = nowMs() + 120;
}

void Mod::onDlgFree(const RingRecord& r) {
    FarPtr p; p.off = (uint16_t)r.p1; p.seg = (uint16_t)r.p2;
    for (size_t i = 0; i < dialogs_.size(); i++) {
        if (dialogs_[i].ptr != p) continue;
        Dialog d = dialogs_[i];
        LOGF("FREE %s", d.ctr.c_str());
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const TextItem& it) { return it.dlg == d.serial; }), items_.end());
        dialogs_.erase(dialogs_.begin() + i);
        if (d.ctr == "TRAIL.CTR") { onTrail_ = false; releaseHolds(); }
        if (d.ctr == "HUNT.CTR") { hunting_ = false; autoHunt_ = false; huntPtr_ = FarPtr(); }
        // RAFTING.CTR is freed the moment rafting starts; the river run keeps
        // going without a screen script, so rafting_ stays set until the next
        // screen script loads (see onCtr).
        break;
    }
    if (typing_) { typing_ = false; typingField_ = -1; cue("field_off"); }
    controlsDirty_ = nowMs() + 150;
}

void Mod::onState(const RingRecord& r) {
    statePtr_.off = (uint16_t)r.p1;
    statePtr_.seg = (uint16_t)r.p2;
    lastDayKey_ = -1;
    logHistory_.clear();
    logAll_.clear();
    LOGF("state struct at %04X:%04X", statePtr_.seg, statePtr_.off);
}

void Mod::readControls(const Dialog& d, std::vector<Control>& out) {
    out.clear();
    if (d.ptr.null()) return;
    uint32_t base = d.ptr.lin();
    int count = g_.rd16(base);
    FarPtr arr = g_.rdFar(base + 2);
    if (count <= 0 || count > 200 || arr.null()) return;
    for (int i = 0; i < count; i++) {
        FarPtr cp = g_.rdFar(arr.lin() + 4 * i);
        if (cp.null()) continue;
        uint8_t b[0x30];
        if (!g_.read(cp.lin(), b, sizeof b)) continue;
        auto w = [&](int k) { return (int)(int16_t)(b[2 * k] | (b[2 * k + 1] << 8)); };
        Control c;
        c.ptr = cp;
        c.h = w(0); c.w = w(1); c.x = w(2); c.y = w(3); c.frame = w(4); c.image = w(5); c.type = w(6);
        if (c.type == 8) { c.id = w(7); c.hotkey = w(9); }
        else if (c.type == 9) { c.value = w(7); c.var = w(8); }
        else if (c.type == 11) {
            c.id = w(7); c.maxlen = w(11); c.cursor = w(12); c.focused = (w(15) & 1) != 0;
            c.buf.off = (uint16_t)w(18); c.buf.seg = (uint16_t)w(19);
        } else if (c.type == 14) {
            // scroll list: w11 item count, w12 selected item, w13 top visible item
            c.id = w(7); c.value = w(11); c.cursor = w(12); c.var = w(13);
        }
        if (c.image >= 0 && c.image < (int)d.images.size()) c.imageName = d.images[c.image];
        out.push_back(c);
    }
}

std::string Mod::fieldValue(const Control& c) {
    if (c.buf.null()) return "";
    std::string s = g_.rdStr(c.buf.lin(), 64);
    std::string o;
    for (char ch : s) if (ch >= 32 && ch < 127) o += ch;
    return o;
}

bool Mod::inStore() {
    Dialog* d = activeDialog();
    return d && (d->ctr == "STORE1.CTR" || d->ctr == "STORE2.CTR");
}

// "Oxen, buying 6, up to 20, 20 dollars each, cost 120 dollars"
std::string Mod::storeRow(const Control& c) {
    std::string mx, price, cost;
    for (auto& it : items_) {
        if (std::abs(it.y - c.y) > 6) continue;
        std::string t = trim(it.text);
        if (t.empty()) continue;
        if (it.x < c.x) mx = t;
        else if (it.x >= 420 && it.x < 520) price = t;
        else if (it.x >= 520) cost = t;
    }
    std::string v = fieldValue(c);
    std::string out = c.label + ", buying " + (v.empty() ? "0" : v);
    if (!mx.empty()) out += ", up to " + mx;
    if (!price.empty()) out += ", " + price + " each";
    if (!cost.empty() && cost != "$.00") out += ", cost " + cost;
    return out;
}

std::string Mod::storeTotals() {
    std::string total, have;
    for (auto& it : items_) {
        if (it.x < 480) continue;
        if (std::abs(it.y - 370) <= 6) total = trim(it.text);
        if (std::abs(it.y - 388) <= 6) have = trim(it.text);
    }
    // the totals are drawn after the item rows; before they appear, fall
    // back to the money in the game state
    readState();
    if (have.empty() && st_.valid) have = fmtMoney(st_.money);
    if (total.empty()) total = "$0.00";
    std::string out = "Total " + total + ". ";
    if (!have.empty()) {
        out += "You have " + have + ".";
        auto cents = [](const std::string& m) {
            long v = 0; bool any = false;
            for (char ch : m) if (isdigit((unsigned char)ch)) { v = v * 10 + (ch - '0'); any = true; }
            return any ? v : -1L;
        };
        long t = cents(total), h = cents(have);
        if (t > 0 && h >= 0) out += t <= h ? " " + fmtMoney(h - t) + " left after this purchase." : " That is more than you have.";
    }
    return out;
}

std::string Mod::labelFor(const Dialog& d, Control& c) {
    std::string l = screenControlLabel(d.ctr, c.id, c.frame, c.imageName);
    if (!l.empty()) return l;
    std::string img = upper(c.imageName);
    bool iconWithText = img.find("CROSSBUT") == 0 || img.find("PACE") == 0 || img.find("RATION") == 0 ||
                        img.find("SPLIT") == 0;
    if ((d.ctr == "OPTIONS.CTR" || d.ctr == "OPT1.CTR") && c.type == 9) {
        if (c.var == 1) {
            static const char* sp[] = {"slow", "normal", "fast"};
            return std::string("Simulation speed ") + (c.value >= 0 && c.value < 3 ? sp[c.value] : "?");
        }
        if (c.var == 2) return "Hunting time " + std::to_string(c.value) + " seconds";
    }
    if ((d.ctr == "PUTFILE1.CTR" || d.ctr == "PUTFILE2.CTR" || d.ctr == "GETFILE.CTR") && c.type == 11) return "File name";
    if (d.ctr == "REST.CTR" && c.type == 11) return "Days to rest";
    if (d.ctr == "WHOAMI.CTR" && c.type == 11) {
        if (c.id == 10) return "Your name";
        if (c.id >= 11 && c.id <= 14) return "Party member " + std::to_string(c.id - 10);
    }
    if (c.type == 9 || c.type == 11 || iconWithText) {
        // texts on the same row: right side first (radio labels, icon captions,
        // store item names), then the left side (field captions)
        std::vector<const TextItem*> right, left;
        for (auto& it : items_) {
            if (it.text.empty()) continue;
            bool rowHit = (it.y >= c.y - 6 && it.y <= c.y + std::max(c.h, 14));
            // field captions may sit a little above the box ("Number:")
            if (!rowHit && c.type == 11 && it.x <= c.x + 4 && it.x > c.x - 120 && it.y >= c.y - 24 && it.y < c.y &&
                !trim(it.text).empty() && trim(it.text).back() == ':') {
                left.push_back(&it);
                continue;
            }
            if (!rowHit) continue;
            if (it.x >= c.x + c.w - 2 && it.x <= c.x + c.w + 320) right.push_back(&it);
            else if (it.x < c.x && it.x > c.x - 240) left.push_back(&it);
        }
        if (c.type == 9 || c.type == 11)
            std::sort(right.begin(), right.end(), [](const TextItem* a, const TextItem* b) { return a->x < b->x; });
        else
            std::sort(right.begin(), right.end(), [](const TextItem* a, const TextItem* b) {
                return a->y != b->y ? a->y < b->y : a->x < b->x;
            });
        if (c.type == 11) {
            // a caption ending in a colon on the left ("Number:") names the field
            const TextItem* cap = nullptr;
            for (auto* it : left) {
                std::string tt = trim(it->text);
                if (!tt.empty() && tt.back() == ':' && (!cap || it->x > cap->x)) cap = it;
            }
            if (cap) { std::string tt = trim(cap->text); tt.pop_back(); return tt; }
        }
        std::string acc;
        for (auto* it : right) {
            if (numericish(it->text)) continue;
            if (c.type == 11 && !acc.empty()) break;
            if (!acc.empty()) acc += " ";
            acc += trim(it->text);
            if (c.type == 9) break;
        }
        if (!acc.empty()) return acc;
        const TextItem* best = nullptr;
        for (auto* it : left) if (!numericish(it->text) && (!best || it->x > best->x)) best = it;
        if (best) return trim(best->text);
    }
    if (c.type == 8) {
        l = imageFrameLabel(c.imageName, c.frame);
        if (!l.empty()) return l;
        if (c.hotkey == 59) return "Help";
        return "Button " + std::to_string(c.id);
    }
    if (c.type == 9) return "Option " + std::to_string(c.value);
    if (c.type == 11) return "Text field";
    return "";
}

std::string Mod::describe(const Control& c, bool withPosition) {
    std::string s;
    if (c.type == 8) s = c.label + ", button";
    else if (c.type == 9) s = c.label + ", radio button, " + (c.frame == 1 ? "selected" : "not selected");
    else if (c.type == 11 && inStore()) s = storeRow(c) + ", edit";
    else if (c.type == 11) {
        std::string v = fieldValue(c);
        s = c.label + ", edit, " + (v.empty() ? std::string("blank") : v);
    } else if (c.type == 15) {
        s = c.label + (c.value == c.cursor ? ", selected" : "");
        if (withPosition) { s += ", " + std::to_string(c.value + 1) + " of " + std::to_string(c.maxlen); return s; }
    }
    else if (c.type == 16) s = c.label + ", text, press Enter to read it again";
    else s = c.label;
    if (c.accel) s += std::string(", shortcut ") + c.accel;
    if (withPosition && !controls_.empty()) {
        int idx = 0;
        for (size_t i = 0; i < controls_.size(); i++) if (&controls_[i] == &c) idx = (int)i;
        s += ", " + std::to_string(idx + 1) + " of " + std::to_string(controls_.size());
    }
    return s;
}

void Mod::refreshControls(bool announce) {
    Dialog* d = activeDialog();
    int prevSerial = lastActiveSerial_;
    int prevId = (focus_ >= 0 && focus_ < (int)controls_.size()) ? controls_[focus_].id : -1;
    int prevType = (focus_ >= 0 && focus_ < (int)controls_.size()) ? controls_[focus_].type : -1;
    int prevVal = (focus_ >= 0 && focus_ < (int)controls_.size()) ? controls_[focus_].value : -1;
    bool prevTrail = (focus_ >= 0 && focus_ < (int)controls_.size()) ? controls_[focus_].fromTrail : false;
    controls_.clear();
    focus_ = -1;
    if (!d) { lastActiveSerial_ = 0; return; }
    std::vector<Control> all;
    readControls(*d, all);
    for (auto& c : all) {
        if (!c.actionable()) {
            if (c.type == 14) {
                if (d->ctr == "TRAILLOG.CTR") continue;
                // list box: the game keeps every entry in memory (w18:w19 is a
                // far pointer to an array of far string pointers), so the whole
                // list is offered, not just the rows on screen
                std::vector<std::string> names;
                if (readListItems(c, names)) {
                    for (size_t i = 0; i < names.size(); i++) {
                        Control li;
                        li.type = 15; li.virt = true;
                        li.x = c.x; li.y = c.y; li.w = c.w; li.h = c.h;
                        li.label = names[i];
                        li.value = (int)i;
                        li.id = 1000 + (int)i;
                        li.maxlen = (int)names.size();
                        li.cursor = c.cursor;
                        li.ptr = c.ptr;
                        controls_.push_back(li);
                    }
                    continue;
                }
                std::vector<const TextItem*> rows;
                for (auto& it : items_)
                    if (it.x >= c.x && it.x < c.x + c.w && it.y >= c.y && it.y < c.y + c.h && !trim(it.text).empty())
                        rows.push_back(&it);
                std::sort(rows.begin(), rows.end(), [](const TextItem* a, const TextItem* b) { return a->y < b->y; });
                bool isLog = d->ctr == "TRAILLOG.CTR";
                if (isLog) continue;
                int visible = std::max(1, c.h / 12);
                if ((int)rows.size() > visible) rows.resize((size_t)visible);
                for (size_t r = 0; r < rows.size(); r++) {
                    const TextItem* it = rows[r];
                    Control li;
                    li.type = 15; li.x = it->x; li.y = it->y; li.w = 40; li.h = 10;
                    li.label = trim(it->text);
                    li.value = c.var + (int)r;          // index in the whole list
                    li.id = 1000 + li.value;
                    li.maxlen = c.value;                // list length
                    li.cursor = c.cursor;               // selected item
                    li.frame = (int)r;                  // visible row
                    li.var = (int)rows.size();
                    li.ptr = c.ptr;                     // the list control (for scrolling)
                    li.buf.off = (uint16_t)(c.x + c.w); li.buf.seg = (uint16_t)c.y;   // scroll bar column / list top
                    li.hotkey = c.y + c.h;              // list bottom
                    controls_.push_back(li);
                }
            }
            continue;
        }
        c.label = labelFor(*d, c);
        c.accel = screenAccelerator(d->ctr, c.id);
        if (c.type == 8 && c.label.find("unavailable") != std::string::npos) continue;
        controls_.push_back(c);
    }
    if (!d->lastText.empty() && d->ctr != "TITLE.CTR") {
        Control tc;
        tc.type = 16;
        tc.id = 900;
        std::string first = d->lastText;
        size_t cut = first.find_first_of(".?!");
        if (cut != std::string::npos && cut < 70) first = first.substr(0, cut);
        else if (first.size() > 60) { first = first.substr(0, 60); size_t sp = first.rfind(' '); if (sp != std::string::npos) first = first.substr(0, sp); first += "..."; }
        tc.label = (d->ctr == "GUIDE.CTR" ? "Page: " : "Message: ") + first;
        controls_.insert(controls_.begin(), tc);
    }
    // Pop-ups that open inside the trail screen's picture panel (pace,
    // rations, rest, guidebook...) leave the trail buttons usable: offer them
    // after the pop-up's own choices.
    panelMode_ = false;
    if (d->ctr != "TRAIL.CTR" && d->rx >= 110 && d->ry >= 28 && d->rx + d->rw <= 385 && d->ry + d->rh <= 240) {
        for (auto& td : dialogs_) {
            if (td.ctr != "TRAIL.CTR") continue;
            std::vector<Control> tc;
            readControls(td, tc);
            for (auto& c : tc) {
                if (!c.actionable()) continue;
                c.label = labelFor(td, c);
                c.accel = screenAccelerator(td.ctr, c.id);
                if (c.type == 8 && c.label.find("unavailable") != std::string::npos) continue;
                c.fromTrail = true;
                controls_.push_back(c);
                panelMode_ = true;
            }
        }
    }
    if (controls_.empty()) { lastActiveSerial_ = d->serial; return; }
    // keep focus on the same control when the same dialog refreshes
    if (d->serial == prevSerial) {
        for (size_t i = 0; i < controls_.size(); i++)
            if (controls_[i].type == prevType && controls_[i].id == prevId && controls_[i].fromTrail == prevTrail &&
                (prevType != 9 || controls_[i].value == prevVal)) { focus_ = (int)i; break; }
    }
    if (focus_ < 0) {
        for (size_t i = 0; i < controls_.size(); i++)
            if (controls_[i].hotkey == 28 && !controls_[i].fromTrail) { focus_ = (int)i; break; }
        if (focus_ < 0) focus_ = (controls_[0].type == 16 && controls_.size() > 1) ? 1 : 0;
        if (d->ctr == "GUIDE.CTR" && controls_[0].type == 16) focus_ = 0;
        {
            // a pop-up whose only input is one text box (Rest) starts on it
            int fieldIdx = -1;
            if (singleFieldDialog(&fieldIdx)) focus_ = fieldIdx;
        }
        if (d->ctr == "INDEX.CTR" || d->ctr == "GETFILE.CTR")
            for (size_t i = 0; i < controls_.size(); i++)
                if (controls_[i].type == 15 && controls_[i].value == controls_[i].cursor) { focus_ = (int)i; break; }
        // on the trail screen start on Continue
        if (d->ctr == "TRAIL.CTR")
            for (size_t i = 0; i < controls_.size(); i++) if (controls_[i].id == 20) focus_ = (int)i;
    }
    lastActiveSerial_ = d->serial;
    if (announce && d->serial != prevSerial && labelRetries_ < 4) {
        // a screen shown again after a full-screen pop-up redraws its captions
        // a moment later: wait for them rather than say "Option 50"
        bool fallback = false;
        for (auto& c : controls_)
            if (c.label.compare(0, 7, "Option ") == 0 || c.label == "Text field" || c.label.compare(0, 7, "Button ") == 0)
                fallback = true;
        if (fallback) {
            labelRetries_++;
            lastActiveSerial_ = prevSerial;
            controlsDirty_ = nowMs() + 250;
            LOGF("labels not drawn yet on %s, waiting (%d)", d->ctr.c_str(), labelRetries_);
            return;
        }
    }
    if (announce && d->serial != prevSerial) labelRetries_ = 0;
    if (announce && d->serial != prevSerial && !(d->ctr == "TITLE.CTR" && lastAnnouncedScreen_ == "TITLE.CTR")) {
        lastAnnouncedScreen_ = d->ctr;
        flushPending(true);
        std::string s;
        if (d->ctr == "TRAIL.CTR") s = "Trail menu. " + describe(controls_[focus_], false);
        else if (d->ctr == "STORE1.CTR" || d->ctr == "STORE2.CTR")
        {
            std::string tot = storeTotals();
            if (tot.find("You have") == std::string::npos) storeTotalsAt_ = nowMs() + 1000;   // money not drawn yet
            s = tot + " Type how many of each item to buy, then choose Buy. Press T or F2 for the total. " +
                describe(controls_[focus_], false);
        }
        else if (controls_.size() == 1) s = describe(controls_[focus_], false);
        else {
            size_t own = 0;
            bool list = false;
            for (auto& c : controls_) {
                if (c.fromTrail) continue;
                if (c.type == 15) list = true; else own++;
            }
            if (list) own++;   // a list counts as one choice
            const Control& fc = controls_[focus_];
            s = (fc.type == 15 ? describe(fc, true) : describe(fc, false)) + ". " + std::to_string(own) +
                (own == 1 ? " choice" : " choices") +
                (panelMode_ ? ", then the trail menu." : ".");
        }
        say(s, false);
    }
    statusLine_ = d->ctr;
}

void Mod::focusControl(int idx, bool speak) {
    if (controls_.empty()) { if (speak) say(noChoicesText(), true); return; }
    if (idx < 0) idx = 0;
    if (idx >= (int)controls_.size()) idx = (int)controls_.size() - 1;
    focus_ = idx;
    // refresh state of this control (radio selection, field text)
    if (speak) say(describe(controls_[focus_], true), true);
}

bool Mod::readListItems(const Control& c, std::vector<std::string>& out) {
    out.clear();
    if (c.ptr.null() || c.value <= 0 || c.value > 1000) return false;
    FarPtr arr = g_.rdFar(c.ptr.lin() + 36);
    if (arr.null() || arr.seg < 0x100) return false;
    for (int i = 0; i < c.value; i++) {
        FarPtr sp = g_.rdFar(arr.lin() + 4 * (uint32_t)i);
        if (sp.null()) return false;
        std::string t = trim(cp437ToSpeech(g_.rdStr(sp.lin(), 80)));
        for (char ch : t) if ((unsigned char)ch < 0x20) return false;
        out.push_back(t);
    }
    return true;
}

// Select entry li.value in a list the way a mouse user would: scroll the
// list so the entry is on screen, then click its row.
void Mod::selectListItem(const Control& li) {
    int rows = std::max(1, li.h / 12);
    int top = li.value - rows / 2;
    if (top > li.maxlen - rows) top = li.maxlen - rows;
    if (top < 0) top = 0;
    g_.wr16(li.ptr.lin() + 26, (uint16_t)top);
    clickAt(li.x + 10, li.y + (li.value - top) * 12 + 5);
    LOGF("list select %d (%s), top %d", li.value, li.label.c_str(), top);
}

bool Mod::scrollList(const Control& li, int dir) {
    // the arrows sit in the scroll bar just right of the list
    int ax = li.buf.off + 7;
    int ay = dir > 0 ? li.hotkey - 6 : (int)li.buf.seg + 6;
    if (dir > 0 && li.value + 1 >= li.maxlen) return false;
    if (dir < 0 && li.value <= 0) return false;
    clickAt(ax, ay);
    listWant_ = li.value + dir;
    listWantAt_ = nowMs() + 300;
    return true;
}

// true when the screen's only input is one text box (plus buttons)
bool Mod::singleFieldDialog(int* idx) {
    int fields = 0, other = 0, fieldIdx = -1;
    for (size_t i = 0; i < controls_.size(); i++) {
        const Control& c = controls_[i];
        if (c.fromTrail) continue;
        if (c.type == 11) { fields++; fieldIdx = (int)i; }
        else if (c.type == 9 || c.type == 15) other++;
    }
    if (idx) *idx = fieldIdx;
    return fields == 1 && other == 0 && !inStore();
}

std::string Mod::noChoicesText() {
    if (rafting_ && raftSound_)
        return "Rafting, " + std::to_string(raftPct_) + " percent of the way. " +
               (autoSteer_ ? "Automatic steering is on." : "Steer with the left and right arrows.");
    if (dialogs_.empty()) return "Please wait, the game is loading the next screen.";
    return "Nothing to choose here. Press Enter to continue, or F6 to read the screen.";
}

void Mod::moveFocus(int delta) {
    refreshControls(false);
    if (controls_.empty()) { say(noChoicesText(), true); return; }
    if (focus_ >= 0 && focus_ < (int)controls_.size() && controls_[focus_].type == 15 && !controls_[focus_].virt) {
        const Control& li = controls_[focus_];
        bool atBottom = delta > 0 && li.frame == li.var - 1;
        bool atTop = delta < 0 && li.frame == 0;
        if ((atBottom || atTop) && scrollList(li, delta)) return;
    }
    int n = (int)controls_.size();
    int i = focus_ < 0 ? 0 : (focus_ + delta + n) % n;
    focusControl(i, true);
}

// Tab treats a whole list as one stop (landing on its selected entry);
// the arrow keys walk through the entries.
void Mod::tabFocus(int delta) {
    refreshControls(false);
    int n = (int)controls_.size();
    if (!n) { say(noChoicesText(), true); return; }
    int i = focus_ < 0 ? 0 : focus_;
    bool fromList = focus_ >= 0 && controls_[i].type == 15;
    for (int step = 0; step < n; step++) {
        i = (i + delta + n) % n;
        if (controls_[i].type != 15) break;
        if (!fromList) {
            int sel = i;
            for (int k = 0; k < n; k++)
                if (controls_[k].type == 15 && controls_[k].value == controls_[k].cursor) { sel = k; break; }
            i = sel;
            break;
        }
    }
    focusControl(i, true);
}

void Mod::clickAt(int x, int y) {
    releaseHolds();
    freshUntil_ = nowMs() + 1500;
    freshSpoken_.clear();
    if (!g_.click(x, y)) say("Input queue is full, try again.", true);
    LOGF("click %d,%d", x, y);
}

void Mod::sendKey(uint16_t w) {
    releaseHolds();
    if (!g_.key(w)) say("Input queue is full, try again.", true);
}

void Mod::activate(int idx, bool openDefault) {
    refreshControls(false);
    if (idx < 0 || idx >= (int)controls_.size()) {
        if (!controls_.empty()) idx = focus_ >= 0 ? focus_ : 0;
        else if (rafting_ && raftSound_) {
            say("Rafting, " + std::to_string(raftPct_) + " percent of the way. " +
                (autoSteer_ ? "Automatic steering is on." : "You are steering with the left and right arrows."), true);
            return;
        } else {
            // screens without buttons (message pages, the raft crash summary)
            // continue on a key press or click
            sendKey(BK_ENTER);
            clickAt(320, 240);
            return;
        }
    }
    Control c = controls_[idx];
    focus_ = idx;
    if (c.type == 11) {
        clickAt(c.x + 3, c.y + c.h / 2);
        typing_ = true;
        typingField_ = idx;
        cue("field_on");
        std::string v = fieldValue(c);
        // the sound marks typing mode; the long hint is given once per session
        say("Editing " + c.label + ". " + (v.empty() ? "blank" : v) +
            (typingHintGiven_ ? "." : ". Type, then press Enter when done. Tab moves to the next field."), true);
        typingHintGiven_ = true;
        return;
    }
    if (c.type == 16) {
        Dialog* td = activeDialog();
        if (td) say(td->lastText, true);
        return;
    }
    if (c.type == 15) {
        if (c.virt) selectListItem(c);
        else clickAt(c.x + 10, c.y + 5);
        Dialog* ld = activeDialog();
        if (openDefault && ld && (ld->ctr == "INDEX.CTR" || ld->ctr == "GETFILE.CTR")) {
            const Control* def = nullptr;
            for (auto& b : controls_) if (b.type == 8 && b.hotkey == 28) { def = &b; break; }
            for (auto& b : controls_) {
                if (def) break;
                std::string l = lower(b.label);
                if (b.type == 8 && (l == "ok" || l == "okay" || l == "open" || l == "load")) def = &b;
            }
            if (def) { pendingClickX_ = def->cx(); pendingClickY_ = def->cy(); pendingClickAt_ = nowMs() + 250; }
        }
        say(c.label + " selected.", true);
        return;
    }
    clickAt(c.cx(), c.cy());
    Dialog* ad = activeDialog();
    if (((ad && ad->ctr == "TRAIL.CTR") || c.fromTrail) && c.type == 8 && c.id == 10) {
        // the map is a picture: describe where the wagon is instead
        readState();
        if (st_.valid) { say(mapText(st_), true); return; }
    }
    if (c.type == 9) say(c.label + " selected.", true);
    else if (c.type == 15) say(c.label, true);
    else sp_.silence();
}

void Mod::announceActiveDialog() {
    refreshControls(false);
    Dialog* d = activeDialog();
    if (!d || controls_.empty()) { say("No choices on this screen.", true); return; }
    std::string s = screenTitle(d->ctr);
    if (!s.empty()) s += ". ";
    for (size_t i = 0; i < controls_.size(); i++) {
        s += controls_[i].label;
        s += i + 1 < controls_.size() ? ", " : ".";
    }
    say(s, true);
}

// =====================================================================
// text
// =====================================================================
int Mod::ownerForPoint(int x, int y, bool fromWrapper) {
    if (dialogs_.empty()) return -1;
    if (fromWrapper) {
        // word-wrapped text (talk bubbles, guidebook pages, messages) belongs
        // to the top screen, never to the trail log under it
        for (int i = (int)dialogs_.size() - 1; i >= 0; i--)
            if (!dialogs_[i].hidden && dialogs_[i].ctr != "TRAILLOG.CTR") return dialogs_[i].serial;
        return dialogs_.back().serial;
    }
    for (int i = (int)dialogs_.size() - 1; i >= 0; i--) {
        const Dialog& d = dialogs_[i];
        if (d.hidden) continue;
        if (x >= d.rx && x < d.rx + d.rw && y >= d.ry - 4 && y < d.ry + d.rh) return d.serial;
    }
    return dialogs_.back().serial;
}

bool Mod::suppressAutoSpeech(const Dialog* d, int x, int y, const std::string& t) {
    (void)t;
    if (!d) return false;
    if (d->ctr == "LEGEND.CTR") return true;              // title screen idle loop
    if (d->ctr == "TRAIL.CTR") {
        if (x >= 380 && y <= 340) return true;            // conditions panel
    }
    if (d->ctr == "STORE1.CTR" || d->ctr == "STORE2.CTR") {
        if (y >= 205 && y <= 360) return true;            // item table and headings
        if (y >= 365 && x >= 500) return true;            // totals (read on demand)
        if (y >= 365 && !trim(t).empty() && trim(t).back() == ':') return true;   // "Total:" / "You have:" labels
    }
    // contents of text fields are read with the field (and echoed while typing)
    std::vector<Control> cs;
    readControls(*d, cs);
    for (auto& c : cs) {
        if (c.type == 11 && x >= c.x - 2 && x <= c.x + c.w && y >= c.y - 4 && y <= c.y + c.h) return true;
        // rows of a list box are read as the player moves through them
        if (c.type == 14 && x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return true;
    }
    return false;
}

void Mod::onText(const RingRecord& r) {
    int x = r.p1, y = r.p2;
    std::string t = cp437ToSpeech(r.text);
    uint16_t base = (uint16_t)(g_.mb16(MB::CS_EVT) - SEG_EVT);
    bool wrapper = (uint16_t)(r.p4 - base) == SEG_WRAP;
    int owner = ownerForPoint(x, y, wrapper);
    Dialog* d = dialogBySerial(owner);
    // typing echo / field contents are never spoken from here
    for (auto& it : items_) {
        if (it.x == x && it.y == y && it.dlg == owner) {
            if (it.text == t) {
                // a plain redraw stays quiet, except right after the player
                // clicked something (a panel shown again with the same text)
                long k = (long)y * 1000 + x;
                if (nowMs() >= freshUntil_ || freshSpoken_.count(k)) return;
                freshSpoken_.insert(k);
                it.when = r.when;
                goto routed;
            }
            it.text = t;
            it.when = r.when;
            it.order = ++textOrder_;
            goto routed;
        }
    }
    {
        freshSpoken_.insert((long)y * 1000 + x);
        TextItem it;
        it.x = x; it.y = y; it.text = t; it.dlg = owner; it.when = r.when; it.order = ++textOrder_;
        items_.push_back(it);
    }
routed:
    if (trim(t).empty()) return;
    if (d && d->ctr == "TRAILLOG.CTR" && !wrapper) { logLine(y, t); return; }
    if (suppressAutoSpeech(d, x, y, t)) return;
    queueText(t, y, false, x, wrapper);
    if (d && d->interactive) controlsDirty_ = nowMs() + 200;   // labels may have arrived
}

// The game's fonts came over from the Macintosh version: 0x8E is e-acute and
// 0xC9 draws a division sign (score screen).  CP437 codes are mapped as well.
std::string cp437ToSpeech(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        if (c < 0x80) { o += (char)c; continue; }
        switch (c) {
        case 0xC9: case 0xF6: o += " divided by "; break;
        case 0x8E: o += 'e'; break;
        case 0xF8: o += " degrees"; break;
        case 0xAB: o += " and a half"; break;
        case 0xAC: o += " and a quarter"; break;
        case 0xF1: o += " plus or minus "; break;
        case 0x9B: o += " cents"; break;
        case 0x9C: o += " pounds"; break;
        case 0xFA: case 0xF9: case 0x07: o += " "; break;
        case 0x82: case 0x8A: case 0x88: case 0x89: o += 'e'; break;
        case 0x85: case 0xA0: case 0x83: case 0x84: o += 'a'; break;
        case 0x87: o += 'c'; break;
        case 0xE9: case 0xE8: case 0xEA: o += 'e'; break;
        case 0xA4: o += 'n'; break;
        default: o += ' '; break;
        }
    }
    return o;
}

void Mod::queueText(const std::string& t, int y, bool interrupt, int x, bool wrap) {
    if (replaying_) return;
    uint64_t now = nowMs();
    if (pending_.empty()) pendingFirst_ = now;
    pendingLast_ = now;
    if (interrupt) pendingInterrupt_ = true;
    pending_.push_back(PendingText{t, y, x, wrap, 0, 0});
}

void Mod::flushPending(bool force) {
    if (pending_.empty()) return;
    uint64_t now = nowMs();
    if (!force && now - pendingLast_ < 160 && now - pendingFirst_ < 1500) return;
    // read each burst top to bottom (titles use y = -1 and stay first)
    // Word-wrapped paragraphs are drawn with coordinates relative to their
    // box, so they cannot be sorted against ordinary text.  Each paragraph is
    // placed after the ordinary text that was drawn before it.
    {
        int lastY = -1;
        bool inWrap = false;
        int block = 0;
        for (auto& p : pending_) {
            if (!p.wrap) { lastY = p.y; inWrap = false; p.key = (long)p.y * 2; p.sub = 0; continue; }
            if (!inWrap) { block++; inWrap = true; }
            p.key = (long)lastY * 2 + 1;
            p.sub = block * 100000L + p.y;
        }
    }
    // The status page shows two independent lists side by side (supplies on
    // the left, health on the right): read the left list first.
    bool twoLists = false;
    for (auto& p : pending_) if (trim(p.t) == "Current Supplies:") twoLists = true;
    auto band = [&](const PendingText& p) {
        if (!twoLists || p.wrap || p.y < 50 || p.y > 190) return 0;
        return p.x >= 260 ? 2 : 1;
    };
    std::stable_sort(pending_.begin(), pending_.end(), [&](const PendingText& a, const PendingText& b) {
        if (a.y < 0 || b.y < 0) { if ((a.y < 0) != (b.y < 0)) return a.y < 0; return false; }
        int ba = band(a), bb = band(b);
        if (ba && bb && ba != bb) return ba < bb;
        if (a.wrap || b.wrap) {
            if (a.key != b.key) return a.key < b.key;
            if (a.sub != b.sub) return a.sub < b.sub;
            return a.x < b.x;
        }
        if (std::abs(a.y - b.y) > 5) return a.y < b.y;
        return a.x < b.x;
    });
    if (twoLists) {
        // the status page: give the parts on number keys instead of reading it all
        pending_.clear();
        pendingInterrupt_ = false;
        say(statusHintGiven_ ? "Status. Number keys 1 through 9, or 0 for everything."
                             : "Status. Press 1 through 9 for each part, or 0 for everything. "
                               "1 date and weather, 2 health, 3 food, 4 oxen, 5 clothing and bullets, 6 spare parts, "
                               "7 money, 8 pace and rations, 9 distance. These keys work anywhere on the trail.", true);
        statusHintGiven_ = true;
        return;
    }
    std::string out;
    std::string prev;
    int prevY = INT32_MIN;
    bool prevWrap = false;
    int prevX = 0;
    for (auto& p : pending_) {
        std::string t = trim(p.t);
        if (t.empty()) continue;
        auto norm = [](std::string v) {
            std::string o;
            for (char c : v) if (isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c);
            return o;
        };
        if (!prev.empty() && norm(prev) == norm(t)) continue;   // screen title repeated as a heading
        if (!out.empty()) {
            char last = prev.empty() ? '.' : prev.back();
            bool sameLine = p.y >= 0 && prevY >= 0 && std::abs(p.y - prevY) <= 5;
            bool cont = islower((unsigned char)t[0]) || last == ',' || last == '-' || last == '=' ||
                        (sameLine && (isdigit((unsigned char)t[0]) || last == 'x')) ||
                        (sameLine && p.x >= prevX && p.x - (prevX + (int)prev.size() * 7) < 30) ||   // same line, next piece
                        (p.y > prevY && p.y - prevY <= 24 &&
                         ((p.wrap && prevWrap) || prev.size() >= 40));   // word-wrapped paragraph
            if (strchr(".!?:;", last) || cont) out += " ";
            else out += ". ";
        }
        out += t;
        prev = t;
        prevY = p.y;
        prevWrap = p.wrap;
        prevX = p.x;
    }
    bool prose = false;
    for (auto& p : pending_) if (p.wrap || trim(p.t).size() >= 40) prose = true;
    if (out.size() >= 60 && prose) {
        // keep the passage so it can be read again from the screen's choices
        Dialog* ad = activeDialog();
        if (ad && ad->ctr != "TRAIL.CTR" && ad->ctr != "HUNT.CTR" && ad->ctr != "STORE1.CTR" && ad->ctr != "STORE2.CTR") {
            std::string t = out;
            std::string title = screenTitle(ad->ctr);
            if (!title.empty() && t.compare(0, title.size() + 1, title + ".") == 0) t = trim(t.substr(title.size() + 1));
            ad->lastText = t;
        }
    }
    bool intr = pendingInterrupt_;
    pending_.clear();
    pendingInterrupt_ = false;
    if (!out.empty()) say(out, intr);
}

std::string Mod::readScreen() {
    std::vector<const TextItem*> v;
    for (auto& it : items_) if (!trim(it.text).empty()) v.push_back(&it);
    std::sort(v.begin(), v.end(), [](const TextItem* a, const TextItem* b) {
        if (std::abs(a->y - b->y) > 6) return a->y < b->y;
        return a->x < b->x;
    });
    std::string out;
    for (auto* it : v) {
        if (!out.empty()) out += (strchr(".!?:", out.back()) ? " " : ". ");
        out += trim(it->text);
    }
    return out;
}

// =====================================================================
// trail log
// =====================================================================
void Mod::logLine(int y, const std::string& t) {
    if (!logRender_.empty() && y <= logRender_.back().first) finishLogRender();
    logRender_.push_back({y, t});
    logRenderLast_ = nowMs();
}

void Mod::finishLogRender() {
    if (logRender_.empty()) return;
    std::vector<std::string> lines;
    for (auto& p : logRender_) lines.push_back(trim(p.second));
    logRender_.clear();
    // The log box shows a window onto the whole log, and the game sometimes
    // redraws it in pieces.  Find where this piece sits in everything seen so
    // far: only lines that continue past the end of the known log are new.
    // Lines that match history anywhere (an old stretch redrawn) are old.
    size_t n = lines.size(), L = logAll_.size();
    size_t newFrom = 0;
    bool aligned = false;
    size_t bestM = 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < L; k++) {
            if (logAll_[k] != lines[i]) continue;
            size_t m = 0;
            while (i + m < n && k + m < L && lines[i + m] == logAll_[k + m]) m++;
            bool reachesEnd = k + m == L;     // continues past the known log
            // the rest of the piece is history (a single repeated line such as
            // "No grass for the oxen." is not enough to call it old)
            bool inside = i + m == n && m >= 2;
            if (!(reachesEnd || inside)) continue;
            // prefer the longest match; on a tie prefer the most recent place
            if (m > bestM || (m == bestM && aligned)) {
                bestM = m;
                aligned = true;
                newFrom = reachesEnd ? i + m : n;
            }
        }
    }
    if (newFrom > n) newFrom = n;
    if (!aligned && L > 0) {
        LOGF("log piece not found in history: %d lines, %d known (treated as new)", (int)n, (int)L);
        for (auto& l : lines) LOGF("   vis [%s]", l.c_str());
    }
    std::vector<std::string> fresh(lines.begin() + (long)newFrom, lines.end());
    // date in force before the fresh lines (for lines drawn without their date)
    // (new lines always follow the end of the known log, so its last date applies)
    std::string date = logDate_;
    for (size_t k = logAll_.size(); k-- > 0;) if (isDateLine(logAll_[k])) { date = logAll_[k]; break; }
    for (size_t i = 0; i < newFrom && i < lines.size(); i++) if (isDateLine(lines[i])) date = lines[i];
    for (auto& l : fresh) logAll_.push_back(l);
    if (logAll_.size() > 2000) logAll_.erase(logAll_.begin(), logAll_.begin() + 500);
    logShown_ = lines;
    if (fresh.empty()) return;
    // Every log line is spoken once, keyed by its date: a log that is
    // redrawn after a pop-up, or scrolled, does not repeat old days.
    std::vector<std::string> messages;
    std::string msg, msgDate;
    bool anyEvent = false;
    auto flushMsg = [&]() {
        if (!msg.empty()) messages.push_back((msgDate.empty() ? "" : msgDate + ". ") + msg);
        msg.clear();
    };
    for (auto& l : fresh) {
        if (isDateLine(l)) {
            flushMsg();
            date = l;
            msgDate = l;
            continue;
        }
        std::string key = date + "|" + l;
        if (logSpoken_.count(key)) continue;
        logSpoken_.insert(key);
        logSpokenOrder_.push_back(key);
        if (msg.empty() && msgDate.empty()) msgDate = date;
        anyEvent = true;
        msg += msg.empty() ? l : " " + l;
    }
    flushMsg();
    logDate_ = date;
    while (logSpokenOrder_.size() > 3000) { logSpoken_.erase(logSpokenOrder_.front()); logSpokenOrder_.pop_front(); }
    LOGF("log: %d visible, %d new, aligned=%d, %d to speak", (int)n, (int)fresh.size(), (int)aligned, (int)messages.size());
    if (!anyEvent) return;     // only a new date line: the daily report handles it
    for (auto& m : messages) {
        logHistory_.push_back(m);
        queueText(m, -1, false);
    }
    if (!replaying_ && traveling() && cfg_.getBool("travel.pause_on_events", true)) {
        holdByEvent_ = true;
        size_t chars = 0;
        for (auto& m : messages) chars += m.size();
        holdUntilSpeechDone_ = nowMs() + 600 + (uint64_t)chars * 60;
    }
    if (logHistory_.size() > 200) logHistory_.erase(logHistory_.begin(), logHistory_.begin() + 50);
}

// =====================================================================
// state & travel
// =====================================================================
void Mod::readState() {
    FarPtr p = g_.mbFar(MB::STATE);
    if (!p.null()) statePtr_ = p;
    if (statePtr_.null()) return;
    prevSt_ = st_;
    st_.read(g_, statePtr_);
}

bool Mod::traveling() const {
    if (!onTrail_ || !st_.valid || st_.stopped) return false;
    // no popup on top of the trail screen
    for (int i = (int)dialogs_.size() - 1; i >= 0; i--) {
        const std::string& c = dialogs_[i].ctr;
        if (c == "TRAIL.CTR" || c == "TRAILLOG.CTR") return true;
        if (dialogs_[i].interactive) return false;
    }
    return false;
}

void Mod::reportDay() {
    dayPending_ = false;
    if (!onTrail_) return;
    bool daily = cfg_.getBool("travel.daily_reports", false);
    bool dayByDay = cfg_.getInt("travel.mode", 0) == 1;
    LOGF("day: %s miles %ld landmark %d route %d resting %d stopped %d food %d health %d", fmtDate(st_).c_str(),
         st_.miles, st_.landmark, st_.route, st_.resting, st_.stopped, st_.food, st_.health);
    if (st_.resting || prevDayResting_) {
        std::string s = fmtDate(st_) + (st_.resting ? ". Resting." : ".");
        if (healthName(st_.health) != lastHealth_) s += " Health " + healthName(st_.health) + ".";
        queueText(s, -1, false);
    } else if (daily || dayByDay) {
        std::string s = fmtDate(st_) + ". ";
        int idx = nextLandmarkIndex(st_);
        int m = landmarkMiles(st_.route, idx);
        if (m >= 0 && idx <= 17) {
            long left = m - st_.miles;
            if (left > 0) s += std::to_string(left) + (left == 1 ? " mile to " : " miles to ") + landmarkName(idx) + ".";
        }
        if (st_.weather != lastWeather_) s += " Weather " + weatherName(st_.weather) + ".";
        if (healthName(st_.health) != lastHealth_) s += " Health " + healthName(st_.health) + ".";
        queueText(s, -1, false);
    }
    prevDayResting_ = st_.resting != 0;
    lastWeather_ = st_.weather;
    lastHealth_ = healthName(st_.health);
    if (dayByDay && traveling()) holdByDay_ = true;
}

void Mod::travelTick() {
    if (!st_.valid) return;
    int dayKey = st_.year * 400 + st_.month * 32 + st_.day;
    if (lastDayKey_ < 0) { lastDayKey_ = dayKey; return; }
    uint64_t now = nowMs();
    if (dayKey != lastDayKey_) {
        if (dayPending_) reportDay();          // the game moved on before the miles settled
        lastDayKey_ = dayKey;
        // The game turns the date before it adds the day's miles: wait for
        // the miles (or a moment, for days without travel) before reporting.
        dayPending_ = true;
        dayPendingAt_ = now;
        dayMiles_ = st_.miles;
    }
    if (dayPending_ && (st_.miles != dayMiles_ || now - dayPendingAt_ > 1200 || st_.resting || !onTrail_))
        reportDay();
    // food warnings
    if (prevSt_.valid && st_.valid && onTrail_) {
        if (prevSt_.food >= 100 && st_.food < 100) queueText("Warning: less than 100 pounds of food left.", -1, false);
        if (prevSt_.food > 0 && st_.food == 0) queueText("Warning: you are out of food.", -1, false);
    }
}

void Mod::applyHold() {
    if (!attached_) return;
    uint64_t now = nowMs();
    if (holdByEvent_ && cfg_.getBool("travel.auto_resume", true)) {
        if (now > holdUntilSpeechDone_ && !sp_.isSpeaking() && pending_.empty()) holdByEvent_ = false;
    }
    bool want = (holdByUser_ || holdByDay_ || holdByEvent_) && traveling();
    if (!traveling()) { holdByDay_ = false; holdByEvent_ = false; }
    uint16_t cur = g_.mb16(MB::HOLD);
    if ((cur != 0) != want) { g_.setHold(want); LOGF("hold %s (user %d day %d event %d)", want ? "ON" : "off", holdByUser_, holdByDay_, holdByEvent_); }
}

void Mod::releaseHolds() {
    holdByEvent_ = holdByDay_ = holdByUser_ = false;
    if (attached_ && g_.mb16(MB::HOLD)) g_.setHold(false);
}

// =====================================================================
// hunting
// =====================================================================
static const char* kAnimal[7] = {"buffalo", "deer", "rabbit", "squirrel", "elk", "bear", "pronghorn"};
static const int kAnimalValue[7] = {9, 4, 1, 1, 6, 7, 3};

void Mod::readHuntSprites(std::vector<HuntSprite>& out) {
    out.clear();
    huntPtr_ = g_.mbFar(MB::HUNT);
    if (huntPtr_.null()) return;
    for (int i = 0; i < 19; i++) {
        FarPtr sp = g_.rdFar(huntPtr_.lin() + 4 * i);
        if (sp.null()) break;
        uint8_t b[0x30];
        if (!g_.read(sp.lin(), b, sizeof b)) break;
        auto w = [&](int off) { return (int)(int16_t)(b[off] | (b[off + 1] << 8)); };
        HuntSprite s;
        s.slot = i;
        s.ptr = sp;
        s.type = w(0x1A);
        if (s.type == 0) break;
        s.x = w(4); s.y = w(6); s.h = w(8); s.w = w(0xA);
        s.interval = (long)(uint16_t)w(0x10) | ((long)w(0x12) << 16);
        s.speed = w(0x18); s.dir = w(0x1C); s.kind = w(0x1E);
        s.tx = w(0x2A); s.ty = w(0x2C);
        out.push_back(s);
    }
}

std::string Mod::huntDescribe(const HuntSprite& s) {
    std::string name = (s.kind >= 0 && s.kind < 7) ? kAnimal[s.kind] : "animal";
    int cx = s.x + s.w / 2;
    std::string where = cx < 230 ? "on the left" : cx > 410 ? "on the right" : "in the middle";
    std::string dist = s.y + s.h < 210 ? "far away" : s.y + s.h < 280 ? "" : "close";
    std::string dir = s.dir == 1 ? "running right" : s.dir == 2 ? "running left" : "standing";
    std::string out = name + " " + where;
    if (!dist.empty()) out += ", " + dist;
    out += ", " + dir;
    return out;
}

void Mod::huntTick() {
    uint64_t now = nowMs();
    if (now - lastHuntPoll_ < 50) return;
    lastHuntPoll_ = now;
    std::vector<HuntSprite> v;
    readHuntSprites(v);
    huntSprites_ = v;
    std::map<int, HuntSprite> live;
    std::set<int> bullets;
    bool killed = false;
    std::vector<bool> matched(huntTracks_.size(), false);
    for (auto& s : v) {
        if (s.type == 6) {
            bullets.insert(s.slot);
            if (lastBx_ >= 0 && now > lastBt_) {
                int d = std::max(std::abs(s.x - lastBx_), std::abs(s.y - lastBy_));
                if (d > 0 && d < 200) {
                    double sp = (double)d / (double)(now - lastBt_);
                    bulletSpeed_ = 0.7 * bulletSpeed_ + 0.3 * sp;
                }
            }
            lastBx_ = s.x; lastBy_ = s.y; lastBt_ = now;
            continue;
        }
        if (s.type != 2 && s.type != 3) continue;
        if (s.kind < 0 || s.kind > 6) continue;
        // follow the same animal from frame to frame
        int best = -1, bestD = 1 << 30;
        for (size_t i = 0; i < huntTracks_.size(); i++) {
            if (matched[i]) continue;
            const HuntSprite& t = huntTracks_[i];
            if (t.kind != s.kind || std::abs(t.y - s.y) > 6) continue;
            int d = std::abs(t.x - s.x);
            if (d < 90 && d < bestD) { bestD = d; best = (int)i; }
        }
        if (best >= 0) {
            matched[best] = true;
            HuntSprite& t = huntTracks_[best];
            if (t.type == 2 && s.type == 3) {
                killed = true;
                queueText(std::string("Hit! You got the ") + kAnimal[s.kind] + ".", -1, false);
            }
            int id = t.track;
            double vel = t.vel;
            int hx = t.hx;
            uint64_t ht = t.ht;
            t = s;
            t.track = id;
            t.seen = now;
            t.vel = vel; t.hx = hx; t.ht = ht;
            if (now - t.ht >= 300) {
                double nv = (double)(s.x - t.hx) / (double)(now - t.ht);
                t.vel = t.ht == 0 ? nv : 0.5 * t.vel + 0.5 * nv;
                t.hx = s.x; t.ht = now;
            }
            if (s.type == 2) { live[s.slot] = t; }
        } else if (s.type == 2) {
            HuntSprite t = s;
            t.track = nextTrack_++;
            t.seen = now;
            t.hx = s.x; t.ht = now;
            // until measured: nominal speed, slowed by the game's frame rate
            double nominal = s.interval > 0 ? (double)s.speed / (double)std::max<long>(s.interval, 1) : 0;
            t.vel = (s.dir == 1 ? 1 : s.dir == 2 ? -1 : 0) * nominal * 0.7;
            huntTracks_.push_back(t);
            matched.push_back(true);
            live[s.slot] = t;
            queueText(huntDescribe(t) + ".", -1, false);
        }
    }
    // forget animals that left the field
    huntTracks_.erase(std::remove_if(huntTracks_.begin(), huntTracks_.end(),
                                     [&](const HuntSprite& t) { return now - t.seen > 400; }),
                      huntTracks_.end());
    // aim assist: steer the bullet's impact point onto the animal it was fired at
    if (!bullets.empty() && aimTrack_ >= 0 && cfg_.getBool("hunting.aim_assist", true)) {
        const HuntSprite* target = nullptr;
        for (auto& kv : live) if (kv.second.track == aimTrack_) target = &kv.second;
        if (target) {
            int ax = (int)std::lround(target->x + target->w / 2.0 + target->vel * 40.0);
            for (auto& b : v)
                if (b.type == 6 && !b.ptr.null() && b.tx != ax) g_.wr16(b.ptr.lin() + 0x2A, (uint16_t)ax);
        }
    }
    if (bullets.empty()) { lastBx_ = -1; aimTrack_ = -1; }
    if (!huntBullets_.empty() && bullets.empty() && !killed && !autoHunt_) queueText("Missed.", -1, false);
    huntBullets_ = bullets;
    huntLive_ = live;
    // keep the chosen target by identity
    if (huntTarget_ >= 0) {
        bool found = false;
        for (auto& kv : live)
            if (kv.second.track == huntTarget_) { found = true; huntTargetX_ = kv.second.x; }
        if (!found) {
            // the tracker lost it (it crossed another animal or a bush): pick up
            // the nearest animal of the same kind, otherwise say it is gone
            int best = -1, bestD = 160;
            for (auto& kv : live)
                if (kv.second.kind == huntTargetKind_ && std::abs(kv.second.x - huntTargetX_) < bestD) {
                    bestD = std::abs(kv.second.x - huntTargetX_);
                    best = kv.second.track;
                }
            LOGF("hunt: target track %d lost, %s", huntTarget_, best >= 0 ? "re-acquired" : "gone");
            huntTarget_ = best;
            // (not when it was just shot: "Hit!" already said what happened)
            if (best < 0 && !autoHunt_ && !killed && nowMs() - lastShot_ > 1500 && huntTargetKind_ >= 0 && huntTargetKind_ < 7) queueText(std::string("Your ") + kAnimal[huntTargetKind_] + " is gone.", -1, false);
        }
    }
    if (autoHunt_ && bullets.empty() && now - lastShot_ > 450 && !live.empty()) {
        if (st_.valid && st_.bullets <= 0) {
            autoHunt_ = false;
            say("Out of bullets. Automatic hunting off.", false);
            return;
        }
        int best = -1, bestVal = -1;
        bool small = cfg_.getBool("hunting.auto_small_game", false);
        for (auto& kv : live) {
            int cx = kv.second.x + kv.second.w / 2;
            if (cx < 70 || cx > 570) continue;
            if (!small && (kv.second.kind == 2 || kv.second.kind == 3)) continue;
            int val = kAnimalValue[kv.second.kind] * 10 - std::abs(cx - 317) / 40;
            if (val > bestVal) { bestVal = val; best = kv.first; }
        }
        if (best >= 0) huntShoot(best);
    }
}

bool Mod::huntShoot(int slot) {
    auto it = huntLive_.find(slot);
    if (it == huntLive_.end()) return false;
    const HuntSprite& s = it->second;
    const double gunX = 317, gunY = 339;
    double bs = std::max(0.15, bulletSpeed_);
    double va = s.vel;
    double cx = s.x + s.w / 2.0, cy = s.y + s.h * 0.55;
    // time from our click to the bullet leaving: input is delivered on the next
    // timer tick, then the game's loop picks it up
    double latency = cfg_.getInt("hunting.latency_ms", 60);
    double tx = cx;
    for (int i = 0; i < 5; i++) {
        double dist = std::max(std::fabs(tx - gunX), std::fabs(cy - gunY));
        double t = dist / bs + latency;
        tx = cx + va * t;
    }
    int ix = (int)std::lround(tx), iy = (int)std::lround(cy);
    if (ix < 53 || ix > 583) {
        LOGF("hunt: target would be off the field (%d)", ix);
        if (ix < 53) ix = 53;
        if (ix > 583) ix = 583;
    }
    if (iy < 133) iy = 133;
    if (iy > 331) iy = 331;
    clickAt(ix, iy);
    lastShot_ = nowMs();
    aimTrack_ = s.track;
    LOGF("hunt shot at %d,%d kind %d x=%d w=%d vel=%.3f bullet=%.3f", ix, iy, s.kind, s.x, s.w, va, bs);
    return true;
}

// =====================================================================
// rafting
// =====================================================================
struct RaftObj { int slot, x, y, w, h, active; FarPtr p; };

// Raft objects (o10:057F): +0x20 x, +0x22 y, +0x24 height, +0x26 width,
// +0x30 active.  The game moves the raft 8 px per interval (+0x18 ms) to the
// right while the cursor is right of its right edge, and to the left while the
// cursor is left of its left edge; a cursor inside the raft keeps it still.
// Raft x is limited to 0x50 .. 0x1CC - width.
void Mod::raftTick() {
    uint64_t now = nowMs();
    if (now - lastRaftPoll_ < 40) return;
    lastRaftPoll_ = now;
    raftPtr_ = g_.mbFar(MB::RAFT);
    if (raftPtr_.null()) return;
    std::vector<RaftObj> objs;
    for (int i = 0; i < 8; i++) {
        FarPtr p = g_.rdFar(raftPtr_.lin() + 4 * i);
        if (p.null()) break;
        uint8_t b[0x34];
        if (!g_.read(p.lin(), b, sizeof b)) break;
        auto w = [&](int off) { return (int)(int16_t)(b[off] | (b[off + 1] << 8)); };
        RaftObj o{i, w(0x20), w(0x22), w(0x26), w(0x24), w(0x30), p};
        objs.push_back(o);
    }
    if (objs.size() < 2) return;
    const RaftObj& raft = objs[0];
    if (raft.w <= 0 || raft.w > 200 || raft.y < 100 || raft.y > 400) return;
    // ---- keep the player informed: river sound, and progress now and then
    if (!raftSound_) {
        raftSound_ = true;
        raftStart_ = now;
        lastRaftProgress_ = now;
        cueLoop("water", true);
        LOGF("rafting run started");
    }
    if (objs.size() > 4) {
        // the map marker climbs from about y 336 to y 72 over the run
        int pct = (int)std::lround((336 - objs[4].y) * 100.0 / (336 - 72));
        pct = std::max(0, std::min(100, pct));
        raftPct_ = pct;
        if (now - lastRaftProgress_ > 15000) {
            lastRaftProgress_ = now;
            say("Rafting, " + std::to_string(pct) + " percent of the way" +
                (autoSteer_ ? "." : ". You are steering."), false);
        }
    }
    const int minX = 0x50, maxX = 0x1CC - raft.w;
    int raftC = raft.x + raft.w / 2;

    // measure raft speed while it is being driven
    if (lastRaftX_ != INT32_MIN && raft.x != lastRaftX_ && now > lastRaftXT_) {
        double v = std::abs(raft.x - lastRaftX_) / (double)(now - lastRaftXT_);
        if (v > 0.01 && v < 1.0) raftV_ = raftV_ * 0.7 + v * 0.3;
    }
    if (raft.x != lastRaftX_) { lastRaftX_ = raft.x; lastRaftXT_ = now; }

    // rocks: entries 1..3 (entry 4 is the map marker)
    struct Rock { RaftObj o; double vx, vy; bool measured; };
    std::vector<Rock> rocks;
    for (size_t i = 1; i < objs.size() && i <= 3; i++) {
        const RaftObj& r = objs[i];
        if (!r.active || r.w <= 0 || r.w > 200 || r.y > 400) { rockTrack_.erase(r.slot); continue; }
        auto it = rockTrack_.find(r.slot);
        if (it == rockTrack_.end() || r.y < it->second.y - 20) {
            rockTrack_[r.slot] = RockTrack{r.x, r.y, now, r.x, r.y, now};
            rockWarned_.erase(r.slot);
            it = rockTrack_.find(r.slot);
        }
        RockTrack& t = it->second;
        t.x = r.x; t.y = r.y; t.t = now;
        Rock k{r, 0.0, rockVy_, false};
        if (r.y - t.y0 >= 12 && now > t.t0) {
            k.vy = (r.y - t.y0) / (double)(now - t.t0);
            k.vx = (r.x - t.x0) / (double)(now - t.t0);
            k.measured = true;
            if (k.vy > 0.005 && k.vy < 0.5) rockVy_ = rockVy_ * 0.9 + k.vy * 0.1;
        }
        rocks.push_back(k);
    }

    // ---- spoken cues (manual steering)
    if (!autoSteer_) {
        for (auto& k : rocks) {
            const RaftObj& r = k.o;
            if (rockWarned_.count(r.slot) || r.y + r.h < 100 || r.y > raft.y + raft.h) continue;
            rockWarned_.insert(r.slot);
            double ta = std::max(0.0, (raft.y - (r.y + r.h)) / std::max(k.vy, 0.005));
            int px = (int)(r.x + k.vx * ta);
            int rc = px + r.w / 2;
            bool danger = px < raft.x + raft.w && px + r.w > raft.x;
            std::string side = rc < raftC - 45 ? "on your left" : rc > raftC + 45 ? "on your right" : "dead ahead";
            say(std::string("Rock ") + side + (danger ? ", steer " + std::string(rc < raftC ? "right!" : "left!") : "."), true);
        }
    }

    // ---- choose where the raft should be
    int target = raft.x;
    if (autoSteer_) {
        const int margin = 10;
        auto clearance = [&](int tx) {
            // smallest horizontal gap to any approaching rock while the raft
            // travels from its current x toward tx
            double worst = 1e9;
            for (auto& k : rocks) {
                const RaftObj& r = k.o;
                if (r.y > raft.y + raft.h) continue;          // already past
                double vy = std::max(k.vy, 0.005);
                double t1 = std::max(0.0, (raft.y - (r.y + r.h)) / vy);
                double t2 = std::max(0.0, (raft.y + raft.h - r.y) / vy);
                if (t1 > 9000) continue;
                for (int s = 0; s <= 6; s++) {
                    double t = t1 + (t2 - t1) * s / 6.0;
                    double moved = std::min((double)std::abs(tx - raft.x), raftV_ * t);
                    double rx = raft.x + (tx > raft.x ? moved : -moved);
                    double kx = r.x + k.vx * t;
                    double extra = k.measured ? 0 : 12;
                    double gap = std::max(kx - (rx + raft.w), rx - (kx + r.w)) - extra;
                    worst = std::min(worst, gap);
                }
            }
            return worst;
        };
        double here = clearance(raft.x);
        bool wasAvoiding = raftAvoiding_;
        raftAvoiding_ = false;
        if (here < margin) {
            raftAvoiding_ = wasAvoiding;
            int best = raft.x; double bestScore = -1e18;
            for (int x = minX; x <= maxX; x += 4) {
                double c = clearance(x);
                double score = (c >= margin ? 100000.0 : c * 100.0) - std::abs(x - raft.x);
                if (score > bestScore) { bestScore = score; best = x; }
            }
            target = best;
            if (!raftAvoiding_ && std::abs(best - raft.x) > 12 && now - lastRaftCue_ > 2000) {
                lastRaftCue_ = now;
                say(best < raft.x ? "Rock ahead, steering left." : "Rock ahead, steering right.", false);
            }
            raftHoldTarget_ = best;
            raftHoldUntil_ = now + 400;
            raftAvoiding_ = true;
        } else if (now < raftHoldUntil_ && clearance(raftHoldTarget_) >= margin) {
            target = raftHoldTarget_;
            raftAvoiding_ = wasAvoiding;
        } else if (rocks.empty() || here > 120) {
            // drift toward the middle of the river when nothing is near
            int mid = (minX + maxX) / 2;
            if (std::abs(raft.x - mid) > 40 && clearance(mid) >= margin) target = mid;
        }
    } else if (manualTarget_ >= 0) {
        target = std::max(minX, std::min(maxX, manualTarget_));
        if (std::abs(target - raft.x) <= 4) manualTarget_ = -1;
    }

    // ---- drive the game cursor
    int cursor;
    if (target > raft.x + 4) cursor = std::min(raft.x + raft.w + 16, 630);
    else if (target < raft.x - 4) cursor = std::max(raft.x - 16, 4);
    else cursor = raftC;
    if (g_.rds16(g_.dgLin(0x5C68)) != cursor) g_.wr16(g_.dgLin(0x5C68), (uint16_t)cursor);
    steerX_ = target;
}

void Mod::raftSteer(int dx) {
    if (!g_.attached()) return;
    if (autoSteer_) { say("Automatic steering is on. Press A to steer yourself.", true); return; }
    int base = manualTarget_ >= 0 ? manualTarget_ : (lastRaftX_ != INT32_MIN ? lastRaftX_ : 240);
    int x = base + (dx > 0 ? 40 : -40);
    if (x < 0x50) x = 0x50;
    if (x > 0x1CC - 72) x = 0x1CC - 72;
    manualTarget_ = x;
    say(x < 170 ? "far left" : x < 230 ? "left" : x <= 300 ? "center" : x < 350 ? "right" : "far right", true);
}

// =====================================================================
// keys
// =====================================================================
void Mod::toggleSetting(const char* key, const char* on, const char* off) {
    bool v = !cfg_.getBool(key, false);
    cfg_.setBool(key, v);
    cfg_.save();
    say(v ? on : off, true);
}

void Mod::help() {
    std::string s =
        "Keys. Up and down arrows, or Tab, move between the choices on the screen. Enter or Space chooses. "
        "Escape cancels. Letters jump to a choice; on the trail screen they are shortcuts: C continue or time out, "
        "S status, M map, G guidebook, R rest, A food rations, P pace, B buy, T trade, K talk, H hunt, O options. "
        "On the trail, number keys read the status one part at a time: 1 date and weather, 2 health, 3 food, "
        "4 oxen, 5 clothing and bullets, 6 spare parts, 7 money, 8 pace and rations, 9 distance, 0 everything. "
        "F2 conditions summary. F3 party health. F4 supplies and money. F5 distance to the next landmark. "
        "F6 reads everything on the screen. F7 reads the latest trail log messages. "
        "F8 pauses or resumes travel, or advances one day in day by day mode. "
        "F9 changes the travel mode. F11 turns daily reports on or off. F12 speaks faster and Shift F12 slower. F10 switches to the speech history and back. "
        "Control silences speech. Control R repeats the last thing said. Shift F1 lists every choice on this screen.";
    if (hunting_) s = "Hunting keys. Space fires at the selected animal, leading it automatically. Left and right arrows "
                      "choose an animal. A toggles automatic hunting. L lists the animals in view. M moves to a new "
                      "area. E ends hunting. " + s;
    if (rafting_) s = "Rafting keys. Left and right arrows steer. A toggles automatic steering. " + s;
    say(s, true);
}

static const char* keyName(KeyCode c) {
    static const char* n[] = {"none", "Up", "Down", "Left", "Right", "Tab", "Enter", "Escape", "Space", "Backspace",
                              "Home", "End", "PageUp", "PageDown", "Delete", "F1", "F2", "F3", "F4", "F5", "F6",
                              "F7", "F8", "F9", "F10", "F11", "F12", "char", "Ctrl"};
    return (c >= 0 && c <= KEY_CTRL_ONLY) ? n[c] : "?";
}

void Mod::onKey(const KeyEvent& k) {
    {
        Dialog* ad = attached_ ? activeDialog() : nullptr;
        LOGF("key %s%s%s%s%s screen=%s focus=%d typing=%d", k.shift ? "Shift+" : "", k.ctrl ? "Ctrl+" : "",
             keyName(k.code), k.code == KEY_CHAR ? " " : "", k.code == KEY_CHAR ? std::string(1, k.ch).c_str() : "",
             ad ? ad->ctr.c_str() : "-", focus_, (int)typing_);
    }
    if (!attached_) {
        if (k.code == KEY_F1) say("Waiting for the game to start.", true);
        return;
    }
    if (k.code == KEY_CTRL_ONLY) { sp_.silence(); return; }
    lastAnnouncedScreen_.clear();
    if (k.ctrl && k.code == KEY_CHAR && (k.ch == 'r' || k.ch == 'R')) { say(lastSpoken_, true); return; }

    // ---- typing into a game text field
    if (typing_) {
        switch (k.code) {
        case KEY_CHAR: {
            uint16_t w = biosKeyForChar(k.ch);
            if (w) { sendKey(w); say(std::string(1, k.ch), true); }
            return;
        }
        case KEY_SPACE: sendKey(BK_SPACE); say("space", true); return;
        case KEY_BACKSPACE: sendKey(BK_BACKSPACE); say("backspace", true); return;
        case KEY_ENTER:
        case KEY_ESCAPE: {
            typing_ = false;
            cue("field_off");
            refreshControls(false);
            if (focus_ >= 0 && focus_ < (int)controls_.size()) {
                // let the game take the last keys (and recalculate store costs) first
                storeRowAt_ = nowMs() + (inStore() ? 450 : 250);
                storeRowIdx_ = focus_;
                // a pop-up with just this one box (Rest): Enter also confirms it
                if (k.code == KEY_ENTER && singleFieldDialog()) {
                    for (auto& b : controls_) {
                        std::string l = lower(b.label);
                        if (b.type == 8 && !b.fromTrail && (b.hotkey == 28 || l == "ok" || l == "okay")) {
                            pendingClickX_ = b.cx(); pendingClickY_ = b.cy(); pendingClickAt_ = nowMs() + 300;
                            storeRowAt_ = 0;
                            break;
                        }
                    }
                }
                return;
            }
            say("Done editing.", true);
            return;
        }
        case KEY_TAB: {
            sendKey(k.shift ? BK_SHIFTTAB : BK_TAB);
            refreshControls(false);
            int n = (int)controls_.size();
            for (int step = 1; step <= n; step++) {
                int i = ((focus_ < 0 ? 0 : focus_) + (k.shift ? -step : step) % n + n) % n;
                if (controls_[i].type == 11) {
                    focus_ = i;
                    if (inStore()) say(storeRow(controls_[i]), true);
                    else say("Editing " + controls_[i].label + ". " + fieldValue(controls_[i]), true);
                    return;
                }
            }
            return;
        }
        default: break;
        }
        // other keys fall through (F keys etc.)
    }

    // ---- hunting
    if (hunting_ && !typing_) {
        if (k.code == KEY_SPACE) {
            if (huntLive_.empty()) { say("No animals in sight.", true); return; }
            int slot = -1;
            for (auto& kv : huntLive_) if (kv.second.track == huntTarget_) slot = kv.first;
            if (slot < 0) {
                if (huntTargetKind_ >= 0 && huntTarget_ < 0) LOGF("hunt: no target chosen, firing at the best animal");
                int bestVal = -1;
                for (auto& kv : huntLive_) {
                    int cx = kv.second.x + kv.second.w / 2;
                    int v = kAnimalValue[kv.second.kind] * 10 - std::abs(cx - 317) / 40;
                    if (v > bestVal) { bestVal = v; slot = kv.first; }
                }
            }
            huntShoot(slot);
            return;
        }
        if (k.code == KEY_LEFT || k.code == KEY_RIGHT) {
            if (huntLive_.empty()) { say("No animals in sight.", true); return; }
            std::vector<int> slots;
            for (auto& kv : huntLive_) slots.push_back(kv.first);
            std::sort(slots.begin(), slots.end(), [&](int a, int b) { return huntLive_[a].x < huntLive_[b].x; });
            int pos = -1;
            for (size_t i = 0; i < slots.size(); i++) if (huntLive_[slots[i]].track == huntTarget_) pos = (int)i;
            pos = k.code == KEY_RIGHT ? pos + 1 : pos - 1;
            if (pos < 0) pos = (int)slots.size() - 1;
            if (pos >= (int)slots.size()) pos = 0;
            huntTarget_ = huntLive_[slots[pos]].track;
            huntTargetKind_ = huntLive_[slots[pos]].kind;
            huntTargetX_ = huntLive_[slots[pos]].x;
            LOGF("hunt: target %s track %d x %d", kAnimal[huntTargetKind_], huntTarget_, huntTargetX_);
            say("Target: " + huntDescribe(huntLive_[slots[pos]]), true);
            return;
        }
        if (k.code == KEY_CHAR && (k.ch == 'a' || k.ch == 'A')) {
            autoHunt_ = !autoHunt_;
            say(autoHunt_ ? "Automatic hunting on." : "Automatic hunting off.", true);
            return;
        }
        if (k.code == KEY_CHAR && (k.ch == 'l' || k.ch == 'L')) {
            if (huntLive_.empty()) { say("No animals in sight.", true); return; }
            std::string s;
            for (auto& kv : huntLive_) s += huntDescribe(kv.second) + ". ";
            say(s, true);
            return;
        }
    }

    // ---- rafting
    if (rafting_ && !typing_) {
        if (k.code == KEY_LEFT) { raftSteer(-24); return; }
        if (k.code == KEY_RIGHT) { raftSteer(24); return; }
        if (k.code == KEY_CHAR && (k.ch == 'a' || k.ch == 'A')) {
            autoSteer_ = !autoSteer_;
            cfg_.setBool("rafting.auto_steer", autoSteer_);
            cfg_.save();
            say(autoSteer_ ? "Automatic steering on." : "Automatic steering off. Use left and right arrows.", true);
            return;
        }
    }

    switch (k.code) {
    case KEY_UP: moveFocus(-1); return;
    case KEY_DOWN: moveFocus(1); return;
    case KEY_TAB: tabFocus(k.shift ? -1 : 1); return;
    case KEY_LEFT: moveFocus(-1); return;
    case KEY_RIGHT: moveFocus(1); return;
    case KEY_HOME: refreshControls(false); focusControl(0, true); return;
    case KEY_END: refreshControls(false); focusControl((int)controls_.size() - 1, true); return;
    case KEY_ENTER: activate(focus_, true); return;
    case KEY_SPACE: activate(focus_, false); return;
    case KEY_ESCAPE: {
        refreshControls(false);
        for (auto& c : controls_) if (c.type == 8 && c.hotkey == 1) { clickAt(c.cx(), c.cy()); return; }
        for (auto& c : controls_) {
            std::string l = lower(c.label);
            if (c.type == 8 && (l == "cancel" || l == "done" || l == "no" || l == "end hunting")) {
                clickAt(c.cx(), c.cy());
                return;
            }
        }
        {
            // a message or help page with a single OK closes with Escape too
            const Control* only = nullptr;
            int buttons = 0;
            for (auto& c : controls_) if (c.type == 8 && !c.fromTrail) { buttons++; only = &c; }
            if (buttons == 1 && only) {
                std::string l = lower(only->label);
                if (l == "ok" || l == "okay" || l == "done") { clickAt(only->cx(), only->cy()); return; }
            }
        }
        say("Nothing to cancel.", true);
        return;
    }
    case KEY_F1:
        if (k.shift) announceActiveDialog();
        else help();
        return;
    case KEY_F2:
        if (inStore()) { say(storeTotals(), true); return; }
        if (st_.valid) say(summaryText(st_), true);
        else say("No game in progress.", true);
        return;
    case KEY_F3:
        if (st_.valid) say(partyText(st_), true);
        else say("No game in progress.", true);
        return;
    case KEY_F4:
        if (st_.valid) say(suppliesText(st_), true);
        else say("No game in progress.", true);
        return;
    case KEY_F5:
        if (st_.valid) say(locationText(st_) + " Wagon " + wagonStatus(st_) + ".", true);
        else say("No game in progress.", true);
        return;
    case KEY_F6: {
        std::string s = readScreen();
        say(s.empty() ? "No text on the screen." : s, true);
        return;
    }
    case KEY_F7: {
        if (logHistory_.empty()) { say("The trail log is empty.", true); return; }
        size_t n = k.shift ? logHistory_.size() : std::min<size_t>(5, logHistory_.size());
        std::string s;
        for (size_t i = logHistory_.size() - n; i < logHistory_.size(); i++) s += logHistory_[i] + " ";
        say(s, true);
        return;
    }
    case KEY_F8: {
        if (holdByDay_) { holdByDay_ = false; say("Next day.", true); return; }
        if (holdByEvent_) { holdByEvent_ = false; holdByUser_ = false; say("Resumed.", true); return; }
        if (!traveling() && !holdByUser_) { say("The wagon is not moving.", true); return; }
        holdByUser_ = !holdByUser_;
        say(holdByUser_ ? "Travel paused. Press F8 to resume." : "Resumed.", true);
        return;
    }
    case KEY_F9: {
        int m = (cfg_.getInt("travel.mode", 0) + 1) % 3;
        cfg_.setInt("travel.mode", m);
        cfg_.setBool("travel.pause_on_events", m != 2);
        cfg_.save();
        static const char* names[] = {
            "Travel mode: continuous. Travel pauses while trail events are read.",
            "Travel mode: day by day. Travel stops after every day; press F8 for the next day.",
            "Travel mode: never pause."};
        say(names[m], true);
        return;
    }
    case KEY_F11:
        toggleSetting("travel.daily_reports", "Daily reports on.", "Daily reports off.");
        return;
    case KEY_CHAR: {
        char up = (char)toupper((unsigned char)k.ch);
        refreshControls(false);
        if (focus_ >= 0 && focus_ < (int)controls_.size() && controls_[focus_].type == 11 && k.ch >= '0' && k.ch <= '9') {
            // typing a number on a text box starts editing it, with that digit
            // (letters keep jumping between choices)
            activate(focus_);
            if (typing_) { sendKey(biosKeyForChar(k.ch)); say(std::string(1, k.ch), false); }
            return;
        }
        if (up == 'T' && inStore()) { say(storeTotals(), true); return; }   // no store item starts with T
        if (k.ch >= '0' && k.ch <= '9' && onTrail_) {
            // number keys: one part of the status at a time
            readState();
            if (!st_.valid) { say("No game in progress.", true); return; }
            say(statusDetail(k.ch - '0', st_), true);
            return;
        }
        refreshControls(false);
        bool inList = focus_ >= 0 && focus_ < (int)controls_.size() && controls_[focus_].type == 15;
        if (inList) {
            int n = (int)controls_.size();
            for (int step = 1; step <= n; step++) {
                int i = (focus_ + step) % n;
                if (controls_[i].type == 15 && !controls_[i].label.empty() &&
                    toupper((unsigned char)controls_[i].label[0]) == up) { focusControl(i, true); return; }
            }
        }
        if (panelMode_) {
            // a pop-up's own choices come before the trail shortcuts under it
            int n = (int)controls_.size();
            for (int step = 1; step <= n; step++) {
                int i = ((focus_ < 0 ? -1 : focus_) + step) % n;
                if (!controls_[i].fromTrail && !controls_[i].label.empty() &&
                    toupper((unsigned char)controls_[i].label[0]) == up) { focusControl(i, true); return; }
            }
        }
        for (size_t i = 0; i < controls_.size(); i++)
            if (controls_[i].accel == up) { activate((int)i); return; }
        // first-letter navigation (list rows only when already in the list,
        // or when nothing else matches)
        int n = (int)controls_.size();
        for (int pass = 0; pass < 2; pass++) {
            for (int step = 1; step <= n; step++) {
                int i = ((focus_ < 0 ? -1 : focus_) + step) % n;
                if (pass == 0 && controls_[i].type == 15 && !inList) continue;
                if (!controls_[i].label.empty() && toupper((unsigned char)controls_[i].label[0]) == up) {
                    focusControl(i, true);
                    return;
                }
            }
        }
        return;
    }
    default: return;
    }
}

// The accessibility layer: turns hook records into speech, presents the
// current screen's controls as a navigable list, and adds travel,
// hunting and rafting assistance.
#pragma once
#include "config.h"
#include "gamestate.h"
#include "guest.h"
#include "platform.h"
#include "resources.h"
#include <climits>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

// keys delivered by the platform layer
enum KeyCode {
    KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_TAB, KEY_ENTER, KEY_ESCAPE, KEY_SPACE,
    KEY_BACKSPACE, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_CHAR, KEY_CTRL_ONLY
};
struct KeyEvent {
    KeyCode code = KEY_NONE;
    char ch = 0;            // for KEY_CHAR
    bool shift = false, ctrl = false, alt = false;
};

struct TextItem {
    int x = 0, y = 0;
    std::string text;
    int dlg = -1;           // owning dialog serial (-1 none)
    uint64_t when = 0;
    uint32_t order = 0;
};

struct Control {
    int type = 0;           // 8 button, 9 radio, 11 text field, 14 list
    int id = 0, x = 0, y = 0, w = 0, h = 0, frame = 0, image = 0, hotkey = 0, value = 0, var = 0;
    FarPtr ptr, buf;
    int cursor = 0, maxlen = 0;
    bool focused = false;
    std::string label, imageName;
    char accel = 0;
    bool fromTrail = false;   // trail-screen button offered under a pop-up
    bool virt = false;        // list entry read from the game's memory
    bool actionable() const { return type == 8 || type == 9 || type == 11; }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

struct Dialog {
    int serial = 0;
    std::string ctr;            // upper-case file name, e.g. TRAIL.CTR
    FarPtr ptr;
    std::vector<std::string> images;
    int rx = 0, ry = 0, rw = 640, rh = 480;   // screen region
    bool run = false;           // entered DoDialog
    uint64_t when = 0;
    bool interactive = false;
    bool hidden = false;        // loaded ahead of time, not drawn yet
    std::string lastText;       // last passage read on this screen (offered as a choice)
};

struct HuntSprite {
    int slot = 0, type = 0, kind = 0, x = 0, y = 0, w = 0, h = 0, speed = 0, dir = 0;
    int track = 0;          // stable identity across frames
    uint64_t seen = 0;
    double vel = 0;         // measured px per ms (signed)
    int hx = 0;             // x at ht
    uint64_t ht = 0;
    long interval = 0;
    int tx = 0, ty = 0;     // bullets: target
    FarPtr ptr;
};

std::string cp437ToSpeech(const std::string& in);

class Mod {
public:
    Mod(Guest& g, Speech& sp, Config& cfg, const std::string& gameDir);
    // call ~30 times a second
    void poll();
    void onKey(const KeyEvent& k);
    void say(const std::string& text, bool interrupt = false);
    void setSound(Sound* s) { snd_ = s; }
    bool attached() const { return attached_; }
    std::string lastSpoken() const { return lastSpoken_; }
    // for the window title / logs
    std::string statusLine() const { return statusLine_; }

private:
    Guest& g_;
    Speech& sp_;
    Config& cfg_;
    Sound* snd_ = nullptr;
    void cue(const char* name) { if (snd_) snd_->play(name); LOGF("sound %s", name); }
    void cueLoop(const char* name, bool on) { if (snd_) snd_->loop(name, on); }
    std::string gameDir_;
    Gxl gxl_;
    bool attached_ = false;
    bool replaying_ = false;
    std::string lastAnnouncedScreen_;
    uint64_t lastAttachTry_ = 0;
    std::string lastSpoken_, statusLine_;

    // ---- records
    void handleRecord(const RingRecord& r);
    void onText(const RingRecord& r);
    void onCtr(const RingRecord& r);
    void onDlgRun(const RingRecord& r);
    void onDlgFree(const RingRecord& r);
    void onState(const RingRecord& r);

    // ---- dialogs & controls
    std::vector<Dialog> dialogs_;
    int nextSerial_ = 1;
    int lastActiveSerial_ = 0;
    std::vector<Control> controls_;
    int focus_ = -1;
    bool typing_ = false;
    int typingField_ = -1;
    uint64_t controlsDirty_ = 0;
    Dialog* topDialog();
    Dialog* activeDialog();
    Dialog* dialogBySerial(int s);
    int ownerForPoint(int x, int y, bool fromWrapper);
    void refreshControls(bool announceIfChanged);
    void readControls(const Dialog& d, std::vector<Control>& out);
    std::string labelFor(const Dialog& d, Control& c);
    std::string describe(const Control& c, bool withPosition);
    void focusControl(int idx, bool speak);
    void activate(int idx, bool openDefault = false);
    bool scrollList(const Control& li, int dir);
    bool readListItems(const Control& c, std::vector<std::string>& out);
    void selectListItem(const Control& li);
    void tabFocus(int delta);
    std::string noChoicesText();
    bool singleFieldDialog(int* idx = nullptr);
    bool panelMode_ = false;
    int labelRetries_ = 0;
    bool typingHintGiven_ = false, statusHintGiven_ = false;
    bool huntTimeSet_ = false;
    void reconcileDialogs();
    void showDialog(Dialog d);
    void onDlgDraw(const RingRecord& r);
    int lastRecType_ = 0;
    bool drawSeen_ = false;
    int listWant_ = -1; uint64_t listWantAt_ = 0;
    int pendingClickX_ = 0, pendingClickY_ = 0; uint64_t pendingClickAt_ = 0;
    void moveFocus(int delta);
    void announceActiveDialog();
    void clickAt(int x, int y);
    void sendKey(uint16_t biosWord);
    std::string fieldValue(const Control& c);
    bool inStore() ;
    uint64_t storeRowAt_ = 0, storeTotalsAt_ = 0;
    int storeRowIdx_ = -1;
    std::string storeRow(const Control& c);
    std::string storeTotals();

    // ---- text model and speech batching
    std::vector<TextItem> items_;
    uint32_t textOrder_ = 0;
    struct PendingText { std::string t; int y, x; bool wrap; long key, sub; };
    std::vector<PendingText> pending_;
    uint64_t freshUntil_ = 0;
    std::set<long> freshSpoken_;
    uint64_t pendingFirst_ = 0, pendingLast_ = 0;
    bool pendingInterrupt_ = false;
    int lastPendingY_ = -1000;
    void queueText(const std::string& t, int y, bool interrupt, int x = 0, bool wrap = false);
    void flushPending(bool force);
    bool suppressAutoSpeech(const Dialog* d, int x, int y, const std::string& t);
    std::string readScreen();

    // ---- trail log
    std::vector<std::pair<int, std::string>> logRender_;   // y,text of render in progress
    uint64_t logRenderLast_ = 0;
    std::vector<std::string> logShown_;    // last complete render (lines)
    std::vector<std::string> logHistory_;  // spoken messages
    std::vector<std::string> logAll_;      // every log line seen, in order
    std::string logDate_;
    bool dayPending_ = false, prevDayResting_ = false;
    uint64_t dayPendingAt_ = 0;
    long dayMiles_ = 0;
    int lastWeather_ = -1;
    std::string lastHealth_;
    void reportDay();
    std::set<std::string> logSpoken_;
    std::deque<std::string> logSpokenOrder_;
    void logLine(int y, const std::string& t);
    void finishLogRender();

    // ---- game state & travel
    FarPtr statePtr_;
    GameState st_, prevSt_;
    uint64_t lastStateRead_ = 0;
    bool onTrail_ = false;
    int lastDayKey_ = -1;
    bool holdByEvent_ = false, holdByDay_ = false, holdByUser_ = false;
    uint64_t holdUntilSpeechDone_ = 0;
    void readState();
    void travelTick();
    bool traveling() const;
    void applyHold();
    void releaseHolds();

    // ---- hunting
    FarPtr huntPtr_;
    bool hunting_ = false;
    std::map<int, HuntSprite> huntLive_;      // keyed by slot (current frame)
    std::vector<HuntSprite> huntTracks_;      // animals being followed
    int nextTrack_ = 1;
    double bulletSpeed_ = 0.6;              // measured, px per ms along the major axis
    int lastBx_ = -1, lastBy_ = -1;
    int aimTrack_ = -1;                     // animal the bullet in flight is meant for
    uint64_t lastBt_ = 0;
    std::vector<HuntSprite> huntSprites_;
    int huntTarget_ = -1;            // slot
    bool autoHunt_ = false;
    uint64_t lastShot_ = 0, lastHuntPoll_ = 0;
    int shotsOut_ = 0;
    std::set<int> huntBullets_;
    int huntTargetKind_ = -1, huntTargetX_ = 0;
    void huntTick();
    void readHuntSprites(std::vector<HuntSprite>& out);
    bool huntShoot(int slot);
    std::string huntDescribe(const HuntSprite& s);

    // ---- rafting
    FarPtr raftPtr_;
    bool rafting_ = false, autoSteer_ = true;
    int steerX_ = 0;
    uint64_t lastRaftPoll_ = 0, lastRaftCue_ = 0;
    struct RockTrack { int x0, y0; uint64_t t0; int x, y; uint64_t t; };
    std::map<int, RockTrack> rockTrack_;    // rock slot -> first/last sighting
    double rockVy_ = 0.04, raftV_ = 0.08;   // px per ms, measured
    int lastRaftX_ = INT32_MIN; uint64_t lastRaftXT_ = 0;
    int manualTarget_ = -1, raftHoldTarget_ = 0; uint64_t raftHoldUntil_ = 0;
    std::set<int> rockWarned_;
    int lastProgress_ = -1;
    bool raftSound_ = false, raftAvoiding_ = false;
    uint64_t raftStart_ = 0, lastRaftProgress_ = 0;
    int raftPct_ = 0;
    void raftTick();
    void raftSteer(int dx);

    // ---- misc
    void help();
    void toggleSetting(const char* key, const char* on, const char* off);
};

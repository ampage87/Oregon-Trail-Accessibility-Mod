// Windows front end of the Oregon Trail accessibility mod (OTAccess.exe).
//
//  * prepares the game folder (OREGONM.EXE, OTMOD.COM) and starts DOSBox
//    Staging with a generated configuration;
//  * finds the helper's mailbox inside DOSBox's memory and runs the Mod;
//  * speaks through SAPI (Windows voices);
//  * takes the keyboard while this window or the DOSBox window is in front
//    (a low-level keyboard hook), so the game can be played from either.
//
// Build (x64 Developer Command Prompt):
//   cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE /Fe:OTAccess.exe *.cpp
//      user32.lib gdi32.lib ole32.lib oleaut32.lib shell32.lib
// (platform_linux.cpp compiles to nothing on Windows.)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mod.h"
#include "patcher.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#endif

static const wchar_t* kAppTitle = L"Oregon Trail Accessibility";

// ---------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------
static auto gStart = std::chrono::steady_clock::now();
uint64_t nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStart).count();
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : p.substr(0, k);
}
static std::wstring envVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 2);
    return (n && n < MAX_PATH * 2) ? std::wstring(buf, n) : std::wstring();
}

// ---------------------------------------------------------------------
// process memory
// ---------------------------------------------------------------------
class WinMemory : public ProcessMemory {
public:
    explicit WinMemory(DWORD pid) : pid_(pid) {
        h_ = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION |
                             SYNCHRONIZE, FALSE, pid);
    }
    ~WinMemory() override { if (h_) CloseHandle(h_); }
    bool ok() const { return h_ != nullptr; }
    bool read(uint64_t a, void* b, size_t n) override {
        SIZE_T got = 0;
        return h_ && ReadProcessMemory(h_, (LPCVOID)(uintptr_t)a, b, n, &got) && got == n;
    }
    bool write(uint64_t a, const void* b, size_t n) override {
        SIZE_T put = 0;
        return h_ && WriteProcessMemory(h_, (LPVOID)(uintptr_t)a, b, n, &put) && put == n;
    }
    bool alive() override { return h_ && WaitForSingleObject(h_, 0) == WAIT_TIMEOUT; }
    void scan(const uint8_t* pat, size_t n, std::vector<uint64_t>& hits) override {
        if (!h_) return;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uint8_t* p = (uint8_t*)si.lpMinimumApplicationAddress;
        uint8_t* end = (uint8_t*)si.lpMaximumApplicationAddress;
        std::vector<uint8_t> buf;
        MEMORY_BASIC_INFORMATION mbi;
        while (p < end && VirtualQueryEx(h_, p, &mbi, sizeof mbi) == sizeof mbi) {
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            size_t size = mbi.RegionSize;
            bool rw = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                      (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
            if (rw && size <= ((size_t)1 << 30)) {
                const size_t chunk = (size_t)4 << 20;
                for (size_t off = 0; off < size;) {
                    size_t len = std::min(chunk, size - off);
                    buf.resize(len);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(h_, base + off, buf.data(), len, &got) || got < n) break;
                    for (size_t i = 0; i + n <= got; i++)
                        if (buf[i] == pat[0] && memcmp(&buf[i], pat, n) == 0) hits.push_back((uint64_t)(uintptr_t)(base + off + i));
                    if (off + len >= size) break;
                    off += len - n;
                }
            }
            p = base + size;
        }
    }
private:
    DWORD pid_;
    HANDLE h_ = nullptr;
};

// ---------------------------------------------------------------------
// speech
// ---------------------------------------------------------------------
// IDispatch helpers (SAPI.SpVoice is an automation object, so no SDK
// headers are needed)
static HRESULT dispInvoke(IDispatch* d, const wchar_t* name, WORD flags, VARIANT* args, UINT nargs, VARIANT* result) {
    if (!d) return E_POINTER;
    DISPID id;
    LPOLESTR nm = const_cast<LPOLESTR>(name);
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &nm, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    DISPPARAMS dp = {args, nullptr, nargs, 0};
    DISPID put = DISPID_PROPERTYPUT;
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) { dp.rgdispidNamedArgs = &put; dp.cNamedArgs = 1; }
    return d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, nullptr, nullptr);
}
static IDispatch* createAutomation(const wchar_t* progId) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(progId, &clsid))) return nullptr;
    IDispatch* d = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_IDispatch, (void**)&d))) return nullptr;
    return d;
}

// Speech goes straight to SAPI (the Windows voices).  Screen readers are
// not used: they would take the game's keys for their own commands.
class WinSpeech : public Speech {
public:
    explicit WinSpeech(Config& cfg) : cfg_(cfg) { rate_ = cfg.getInt("speech.rate", 2); }
    ~WinSpeech() override { if (d_) d_->Release(); }
    bool ready() {
        if (!d_) {
            d_ = createAutomation(L"SAPI.SpVoice");
            if (d_) { applyRate(); applyVoice(); LOGF("SAPI voice ready"); }
            else LOGF("SAPI.SpVoice could not be created");
        }
        return d_ != nullptr;
    }
    void speak(const std::string& text, bool interrupt) override {
        if (onSpeak) onSpeak(text);
        if (!ready()) return;
        std::wstring t = widen(text);
        VARIANT a[2];
        VariantInit(&a[0]); VariantInit(&a[1]);
        a[1].vt = VT_BSTR; a[1].bstrVal = SysAllocString(t.c_str());   // arguments are passed in reverse
        // SVSFlagsAsync | SVSFPurgeBeforeSpeak | SVSFIsNotXML
        a[0].vt = VT_I4; a[0].lVal = 1 | (interrupt ? 2 : 0) | 16;
        VARIANT r; VariantInit(&r);
        dispInvoke(d_, L"Speak", DISPATCH_METHOD, a, 2, &r);
        VariantClear(&a[1]); VariantClear(&r);
        estimateUntil_ = nowMs() + 250 + t.size() * 60;
    }
    void silence() override {
        if (!d_) return;
        VARIANT a[2];
        VariantInit(&a[0]); VariantInit(&a[1]);
        a[1].vt = VT_BSTR; a[1].bstrVal = SysAllocString(L"");
        a[0].vt = VT_I4; a[0].lVal = 1 | 2;
        VARIANT r; VariantInit(&r);
        dispInvoke(d_, L"Speak", DISPATCH_METHOD, a, 2, &r);
        VariantClear(&a[1]); VariantClear(&r);
        estimateUntil_ = 0;
    }
    bool isSpeaking() override {
        if (!d_) return false;
        VARIANT st; VariantInit(&st);
        if (FAILED(dispInvoke(d_, L"Status", DISPATCH_PROPERTYGET, nullptr, 0, &st)) || st.vt != VT_DISPATCH || !st.pdispVal) {
            VariantClear(&st);
            return nowMs() < estimateUntil_;
        }
        VARIANT rs; VariantInit(&rs);
        bool sp = false;
        if (SUCCEEDED(dispInvoke(st.pdispVal, L"RunningState", DISPATCH_PROPERTYGET, nullptr, 0, &rs)) &&
            SUCCEEDED(VariantChangeType(&rs, &rs, 0, VT_I4)))
            sp = rs.lVal == 2;   // SRSEIsSpeaking
        VariantClear(&rs);
        VariantClear(&st);
        return sp;
    }
    std::string name() override { return "SAPI"; }
    // F12 / Shift+F12
    void changeRate(int delta) {
        rate_ = std::max(-10, std::min(10, rate_ + delta));
        cfg_.setInt("speech.rate", rate_);
        cfg_.save();
        applyRate();
        speak("Speech rate " + std::to_string(rate_), true);
    }
    std::function<void(const std::string&)> onSpeak;
private:
    void applyRate() {
        if (!d_) return;
        VARIANT a; VariantInit(&a);
        a.vt = VT_I4; a.lVal = rate_;
        dispInvoke(d_, L"Rate", DISPATCH_PROPERTYPUT, &a, 1, nullptr);
    }
    // speech.voice in OTAccess.ini: part of a voice name, e.g. "Zira"
    void applyVoice() {
        std::wstring want = widen(cfg_.get("speech.voice", ""));
        if (want.empty() || !d_) return;
        VARIANT r; VariantInit(&r);
        VARIANT a[2];
        VariantInit(&a[0]); VariantInit(&a[1]);
        a[1].vt = VT_BSTR; a[1].bstrVal = SysAllocString(L"");
        a[0].vt = VT_BSTR; a[0].bstrVal = SysAllocString(L"");
        if (SUCCEEDED(dispInvoke(d_, L"GetVoices", DISPATCH_METHOD, a, 2, &r)) && r.vt == VT_DISPATCH && r.pdispVal) {
            VARIANT cnt; VariantInit(&cnt);
            if (SUCCEEDED(dispInvoke(r.pdispVal, L"Count", DISPATCH_PROPERTYGET, nullptr, 0, &cnt)) &&
                SUCCEEDED(VariantChangeType(&cnt, &cnt, 0, VT_I4))) {
                for (long i = 0; i < cnt.lVal; i++) {
                    VARIANT idx; VariantInit(&idx); idx.vt = VT_I4; idx.lVal = i;
                    VARIANT tok; VariantInit(&tok);
                    if (FAILED(dispInvoke(r.pdispVal, L"Item", DISPATCH_METHOD, &idx, 1, &tok)) || tok.vt != VT_DISPATCH) continue;
                    VARIANT desc; VariantInit(&desc);
                    VARIANT none; VariantInit(&none); none.vt = VT_I4; none.lVal = 0;
                    if (SUCCEEDED(dispInvoke(tok.pdispVal, L"GetDescription", DISPATCH_METHOD, &none, 1, &desc)) &&
                        desc.vt == VT_BSTR && desc.bstrVal && wcsstr(desc.bstrVal, want.c_str())) {
                        VARIANT v; VariantInit(&v); v.vt = VT_DISPATCH; v.pdispVal = tok.pdispVal;
                        dispInvoke(d_, L"Voice", DISPATCH_PROPERTYPUTREF, &v, 1, nullptr);
                        LOGF("voice: %s", narrow(desc.bstrVal).c_str());
                        VariantClear(&desc);
                        VariantClear(&tok);
                        break;
                    }
                    VariantClear(&desc);
                    VariantClear(&tok);
                }
            }
        }
        VariantClear(&a[0]); VariantClear(&a[1]); VariantClear(&r);
    }
    Config& cfg_;
    IDispatch* d_ = nullptr;
    int rate_ = 2;
    uint64_t estimateUntil_ = 0;
};

// ---------------------------------------------------------------------
// sound cues: generated in code (no sound files to ship), played with
// waveOut so the river loop and one-shot cues mix
// ---------------------------------------------------------------------
class WinSound : public Sound {
public:
    explicit WinSound(Config& cfg) : cfg_(cfg) {
        const int rate = kRate;
        // text box entered: two short rising notes; left: two falling notes
        buildTones(fieldOn_, {660.0, 990.0});
        buildTones(fieldOff_, {990.0, 660.0});
        // river: low-passed noise with a slow swell, crossfaded so it loops cleanly
        int n = rate * 4, fade = rate / 2;
        std::vector<double> mono((size_t)(n + fade));
        uint32_t seed = 12345;
        double lp1 = 0, lp2 = 0;
        for (size_t i = 0; i < mono.size(); i++) {
            seed = seed * 1664525u + 1013904223u;
            double white = ((seed >> 9) & 0x7FFF) / 16384.0 - 1.0;
            lp1 += 0.08 * (white - lp1);
            lp2 += 0.08 * (lp1 - lp2);
            double swell = 0.75 + 0.25 * std::sin(2 * 3.14159265 * (double)i / (rate * 2.0));
            mono[i] = lp2 * 3.2 * swell;
        }
        for (int i = 0; i < fade; i++) {
            double a = (double)i / fade;
            mono[(size_t)i] = mono[(size_t)i] * a + mono[(size_t)(n + i)] * (1 - a);
        }
        water_.resize((size_t)n * 2);
        double vol = cfg_.getInt("sound.water_volume", 35) / 100.0;
        for (int i = 0; i < n; i++) {
            double v = std::max(-1.0, std::min(1.0, mono[(size_t)i])) * vol;
            water_[(size_t)i * 2] = water_[(size_t)i * 2 + 1] = (int16_t)(v * 32000);
        }
    }
    ~WinSound() override {
        loop("water", false);
        for (auto& p : playing_) closeVoice(*p);
    }
    void play(const std::string& name) override {
        if (!cfg_.getBool("sound.cues", true)) return;
        reap();
        const std::vector<int16_t>* buf = name == "field_on" ? &fieldOn_ : name == "field_off" ? &fieldOff_ : nullptr;
        if (!buf) return;
        // waveOut keeps a pointer to the WAVEHDR until playback ends, so each
        // voice lives on the heap and is never moved or copied
        std::unique_ptr<Voice> v(new Voice());
        if (openVoice(*v, *buf, false)) playing_.push_back(std::move(v));
    }
    void loop(const std::string& name, bool on) override {
        if (name != "water") return;
        if (on && !loopOn_) {
            if (!cfg_.getBool("sound.water", true)) return;
            loopOn_ = openVoice(loopVoice_, water_, true);
        } else if (!on && loopOn_) {
            closeVoice(loopVoice_);
            loopOn_ = false;
        }
    }
private:
    static const int kRate = 22050;
    struct Voice {
        HWAVEOUT h = nullptr;
        WAVEHDR hdr = {};
        Voice() {}
        Voice(const Voice&) = delete;
        Voice& operator=(const Voice&) = delete;
    };
    void buildTones(std::vector<int16_t>& out, std::initializer_list<double> freqs) {
        int per = kRate * 55 / 1000, gap = kRate * 15 / 1000;
        double vol = cfg_.getInt("sound.cue_volume", 45) / 100.0;
        for (double f : freqs) {
            for (int i = 0; i < per; i++) {
                double env = std::min(1.0, std::min(i / (kRate * 0.005), (per - i) / (kRate * 0.02)));
                double v = std::sin(2 * 3.14159265 * f * i / kRate) * env * vol;
                out.push_back((int16_t)(v * 30000));
                out.push_back((int16_t)(v * 30000));
            }
            for (int i = 0; i < gap * 2; i++) out.push_back(0);
        }
    }
    bool openVoice(Voice& v, const std::vector<int16_t>& buf, bool looping) {
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = kRate;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = kRate * 4;
        if (waveOutOpen(&v.h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { v.h = nullptr; return false; }
        v.hdr = WAVEHDR();
        v.hdr.lpData = (LPSTR)buf.data();
        v.hdr.dwBufferLength = (DWORD)(buf.size() * sizeof(int16_t));
        if (looping) { v.hdr.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP; v.hdr.dwLoops = 0xFFFFFFFF; }
        waveOutPrepareHeader(v.h, &v.hdr, sizeof v.hdr);
        if (looping) v.hdr.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
        waveOutWrite(v.h, &v.hdr, sizeof v.hdr);
        return true;
    }
    void closeVoice(Voice& v) {
        if (!v.h) return;
        waveOutReset(v.h);
        waveOutUnprepareHeader(v.h, &v.hdr, sizeof v.hdr);
        waveOutClose(v.h);
        v.h = nullptr;
    }
    void reap() {
        for (size_t i = 0; i < playing_.size();) {
            if (playing_[i]->hdr.dwFlags & WHDR_DONE) { closeVoice(*playing_[i]); playing_.erase(playing_.begin() + (long)i); }
            else i++;
        }
    }
    Config& cfg_;
    std::vector<int16_t> fieldOn_, fieldOff_, water_;
    Voice loopVoice_;
    bool loopOn_ = false;
    std::vector<std::unique_ptr<Voice>> playing_;
};

// ---------------------------------------------------------------------
// DOSBox Staging
// ---------------------------------------------------------------------
static std::wstring findDosBox(Config& cfg) {
    std::wstring p = widen(cfg.get("dosbox.path", ""));
    if (!p.empty() && fileExists(p)) return p;
    std::vector<std::wstring> cands = {
        envVar(L"ProgramFiles") + L"\\DOSBox Staging\\dosbox.exe",
        envVar(L"ProgramFiles(x86)") + L"\\DOSBox Staging\\dosbox.exe",
        envVar(L"LOCALAPPDATA") + L"\\Programs\\DOSBox Staging\\dosbox.exe",
    };
    for (auto& c : cands) if (fileExists(c)) return c;
    // winget portable installs live under %LOCALAPPDATA%\Microsoft\WinGet\Packages\<id>\...
    std::wstring pk = envVar(L"LOCALAPPDATA") + L"\\Microsoft\\WinGet\\Packages";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((pk + L"\\DOSBoxStaging*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::wstring dir = pk + L"\\" + fd.cFileName;
            if (fileExists(dir + L"\\dosbox.exe")) { FindClose(h); return dir + L"\\dosbox.exe"; }
            WIN32_FIND_DATAW sd;
            HANDLE h2 = FindFirstFileW((dir + L"\\*").c_str(), &sd);
            if (h2 != INVALID_HANDLE_VALUE) {
                do {
                    if ((sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && sd.cFileName[0] != L'.') {
                        std::wstring c = dir + L"\\" + sd.cFileName + L"\\dosbox.exe";
                        if (fileExists(c)) { FindClose(h2); FindClose(h); return c; }
                    }
                } while (FindNextFileW(h2, &sd));
                FindClose(h2);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // on the PATH
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"dosbox.exe", nullptr, MAX_PATH, buf, nullptr)) return buf;
    return L"";
}

static bool writeDosBoxConf(const std::wstring& path, const std::wstring& gameDir, Config& cfg) {
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return false;
    std::string gd = narrow(gameDir);
    // dosbox.cycles in OTAccess.ini, e.g. 20000 (blank = DOSBox's own default)
    std::string cyc = cfg.get("dosbox.cycles", ""), cycles;
    if (!cyc.empty() && cyc != "auto") cycles = "\n[cpu]\ncpu_cycles = " + cyc + "\n";
    fprintf(f,
            "# Written by OTAccess.exe each time the game starts - edit OTAccess.ini instead.\n"
            "[sdl]\n"
            "fullscreen = false\n"
            "pause_when_inactive = false\n"
            "mute_when_inactive = false\n"
            "\n[dosbox]\n"
            "memsize = 16\n"
            "%s"
            "\n[mouse]\n"
            "mouse_capture = onclick\n"
            "\n[sblaster]\n"
            "sbtype = sb16\n"
            "sbbase = 220\n"
            "irq = 7\n"
            "dma = 1\n"
            "\n[autoexec]\n"
            "mount c \"%s\"\n"
            "c:\n"
            "OTMOD\n"
            "OREGONM\n"
            "exit\n",
            cycles.c_str(), gd.c_str());
    return fclose(f) == 0;
}

// ---------------------------------------------------------------------
// the app
// ---------------------------------------------------------------------
enum { WM_OT_KEY = WM_APP + 1, WM_OT_CTRL = WM_APP + 2 };
static const UINT_PTR kTimerPoll = 1;

struct App {
    std::wstring modDir, gameDir;
    Config cfg;
    std::unique_ptr<WinSpeech> speech;
    std::unique_ptr<WinSound> sound;
    std::unique_ptr<WinMemory> mem;
    std::unique_ptr<Guest> guest;
    std::unique_ptr<Mod> mod;
    PROCESS_INFORMATION pi = {};
    bool dosboxRunning = false;
    HWND hwnd = nullptr, edit = nullptr;
    HHOOK hook = nullptr;
    bool review = false;            // F10: keys go to the history box instead of the game
    bool ctrlAlone = false;
    uint64_t closingAt = 0;
    std::vector<DWORD> swallowed;   // virtual keys whose key-up must be eaten too
};
static App* gApp = nullptr;

class NullMemory : public ProcessMemory {
public:
    bool read(uint64_t, void*, size_t) override { return false; }
    bool write(uint64_t, const void*, size_t) override { return false; }
    void scan(const uint8_t*, size_t, std::vector<uint64_t>&) override {}
    bool alive() override { return false; }
};
static NullMemory gNullMem;

static void appendHistory(const std::string& text) {
    if (!gApp || !gApp->edit) return;
    std::wstring w = widen(text) + L"\r\n";
    int len = GetWindowTextLengthW(gApp->edit);
    if (len > 60000) {
        SendMessageW(gApp->edit, EM_SETSEL, 0, 20000);
        SendMessageW(gApp->edit, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(gApp->edit);
    }
    SendMessageW(gApp->edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(gApp->edit, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
}

static bool isDosBoxWindow(HWND w) {
    if (!gApp || !gApp->dosboxRunning || !w) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == gApp->pi.dwProcessId;
}
static bool ourWindowActive() {
    HWND f = GetForegroundWindow();
    return f && (f == gApp->hwnd || GetAncestor(f, GA_ROOT) == gApp->hwnd || isDosBoxWindow(f));
}

// Map a virtual key to a mod key.  Returns false for keys the game layer
// does not use (they are passed through).
static bool translateKey(DWORD vk, DWORD scan, bool shift, bool ctrl, KeyEvent& k) {
    k = KeyEvent();
    k.shift = shift;
    k.ctrl = ctrl;
    switch (vk) {
    case VK_UP: k.code = KEY_UP; return true;
    case VK_DOWN: k.code = KEY_DOWN; return true;
    case VK_LEFT: k.code = KEY_LEFT; return true;
    case VK_RIGHT: k.code = KEY_RIGHT; return true;
    case VK_TAB: k.code = KEY_TAB; return true;
    case VK_RETURN: k.code = KEY_ENTER; return true;
    case VK_ESCAPE: k.code = KEY_ESCAPE; return true;
    case VK_SPACE: k.code = KEY_SPACE; k.ch = ' '; return true;
    case VK_BACK: k.code = KEY_BACKSPACE; return true;
    case VK_HOME: k.code = KEY_HOME; return true;
    case VK_END: k.code = KEY_END; return true;
    case VK_PRIOR: k.code = KEY_PGUP; return true;
    case VK_NEXT: k.code = KEY_PGDN; return true;
    case VK_DELETE: k.code = KEY_DELETE; return true;
    case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5: case VK_F6:
    case VK_F7: case VK_F8: case VK_F9: case VK_F11: case VK_F12:
        k.code = (KeyCode)(KEY_F1 + (int)(vk - VK_F1));
        return true;
    default: break;
    }
    if (vk >= 'A' && vk <= 'Z') {
        k.code = KEY_CHAR;
        k.ch = (char)(shift ? vk : vk - 'A' + 'a');
        return true;
    }
    if (ctrl) return false;   // other control combinations belong to Windows / the screen reader
    BYTE ks[256] = {0};
    if (shift) ks[VK_SHIFT] = 0x80;
    wchar_t out[4] = {0};
    int n = ToUnicodeEx(vk, scan, ks, out, 4, 0x4, GetKeyboardLayout(0));
    if (n == 1 && out[0] >= 0x20 && out[0] < 0x7F) {
        k.code = KEY_CHAR;
        k.ch = (char)out[0];
        return true;
    }
    return false;
}

static LRESULT CALLBACK keyHook(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION || !gApp) return CallNextHookEx(nullptr, code, wp, lp);
    const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lp;
    bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
    DWORD vk = kb->vkCode;
    if (kb->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, wp, lp);

    // key-ups of keys we ate
    if (up) {
        auto it = std::find(gApp->swallowed.begin(), gApp->swallowed.end(), vk);
        bool eat = it != gApp->swallowed.end();
        if (eat) gApp->swallowed.erase(it);
        if ((vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) && gApp->ctrlAlone) {
            gApp->ctrlAlone = false;
            if (ourWindowActive()) PostMessageW(gApp->hwnd, WM_OT_CTRL, 0, 0);
        }
        if (eat) return 1;
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    if (!down) return CallNextHookEx(nullptr, code, wp, lp);

    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) {
        gApp->ctrlAlone = true;
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    gApp->ctrlAlone = false;
    if (!ourWindowActive()) return CallNextHookEx(nullptr, code, wp, lp);

    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    if (alt || win) return CallNextHookEx(nullptr, code, wp, lp);
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    if (vk == VK_F10) {
        gApp->swallowed.push_back(vk);
        PostMessageW(gApp->hwnd, WM_OT_KEY, (WPARAM)VK_F10, 0);
        return 1;
    }
    if (gApp->review) return CallNextHookEx(nullptr, code, wp, lp);
    if (!gApp->mod || !gApp->mod->attached()) {
        // before the game is up only our function keys are taken
        if (!(vk >= VK_F1 && vk <= VK_F12)) return CallNextHookEx(nullptr, code, wp, lp);
    }
    KeyEvent k;
    if (!translateKey(vk, kb->scanCode, shift, ctrl, k)) return CallNextHookEx(nullptr, code, wp, lp);
    if (ctrl && !(k.code == KEY_CHAR && (k.ch == 'r' || k.ch == 'R')))
        return CallNextHookEx(nullptr, code, wp, lp);   // only Ctrl+R is ours
    gApp->swallowed.push_back(vk);
    LPARAM packed = (LPARAM)((unsigned char)k.ch) | ((LPARAM)k.shift << 8) | ((LPARAM)k.ctrl << 9);
    PostMessageW(gApp->hwnd, WM_OT_KEY, (WPARAM)k.code | 0x10000, packed);
    return 1;
}

static void startGame() {
    App& a = *gApp;
    std::wstring db = findDosBox(a.cfg);
    if (db.empty()) {
        a.speech->speak("DOSBox Staging was not found. Run the deploy program again, or install it with winget install "
                        "DOSBoxStaging.DOSBoxStaging, then start the game again.", true);
        MessageBoxW(a.hwnd,
                    L"DOSBox Staging was not found.\n\nInstall it from a terminal with:\nwinget install DOSBoxStaging.DOSBoxStaging\n\n"
                    L"or set dosbox.path in OTAccess.ini.",
                    kAppTitle, MB_OK | MB_ICONERROR);
        a.closingAt = nowMs() + 100;
        return;
    }
    std::wstring conf = a.modDir + L"\\otaccess_dosbox.conf";
    if (!writeDosBoxConf(conf, a.gameDir, a.cfg)) {
        a.speech->speak("Could not write the DOSBox settings file.", true);
        a.closingAt = nowMs() + 3000;
        return;
    }
    std::wstring cmd = L"\"" + db + L"\" --noprimaryconf --nolocalconf -conf \"" + conf + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    if (!CreateProcessW(nullptr, cl.data(), nullptr, nullptr, FALSE, 0, nullptr, a.modDir.c_str(), &si, &a.pi)) {
        LOGF("CreateProcess failed: %lu", GetLastError());
        a.speech->speak("DOSBox could not be started.", true);
        a.closingAt = nowMs() + 3000;
        return;
    }
    a.dosboxRunning = true;
    LOGF("started DOSBox pid %lu: %s", a.pi.dwProcessId, narrow(cmd).c_str());
    a.mem.reset(new WinMemory(a.pi.dwProcessId));
    a.guest.reset(new Guest(a.mem.get()));
    a.mod.reset(new Mod(*a.guest, *a.speech, a.cfg, narrow(a.gameDir)));
    a.mod->setSound(a.sound.get());
}

static void handleKey(UINT code, LPARAM packed) {
    App& a = *gApp;
    if (code == VK_F10) {
        a.review = !a.review;
        if (a.review) {
            SetFocus(a.edit);
            a.speech->speak("Review mode. The game is not receiving keys. Use the arrow keys to read the speech history. "
                            "Press F10 to return to the game.", true);
        } else {
            a.speech->speak("Back to the game.", true);
        }
        return;
    }
    KeyEvent k;
    k.code = (KeyCode)(code & 0xFFFF);
    k.ch = (char)(packed & 0xFF);
    k.shift = (packed >> 8) & 1;
    k.ctrl = (packed >> 9) & 1;
    if (k.code == KEY_F12) { a.speech->changeRate(k.shift ? -1 : 1); return; }
    if (a.mod) a.mod->onKey(k);
    else if (k.code == KEY_F1) a.speech->speak("The game is starting.", true);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    App* a = gApp;
    switch (m) {
    case WM_CREATE: {
        a->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                  0, 0, 100, 100, h, (HMENU)1, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(a->edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetWindowTextW(a->edit, L"");
        return 0;
    }
    case WM_SIZE:
        if (a->edit) MoveWindow(a->edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        if (a->edit) SetFocus(a->edit);
        return 0;
    case WM_TIMER:
        if (wp == kTimerPoll) {
            if (a->closingAt && nowMs() >= a->closingAt) { DestroyWindow(h); return 0; }
            if (a->dosboxRunning) {
                if (WaitForSingleObject(a->pi.hProcess, 0) != WAIT_TIMEOUT) {
                    a->dosboxRunning = false;
                    a->speech->speak("The game has closed.", true);
                    a->closingAt = nowMs() + 1500;
                    return 0;
                }
                if (a->mod) a->mod->poll();
            }
        }
        return 0;
    case WM_OT_KEY:
        handleKey((UINT)wp, lp);
        return 0;
    case WM_OT_CTRL:
        if (a->speech) a->speech->silence();
        return 0;
    case WM_CLOSE:
        if (a->dosboxRunning) {
            // close the game with the window
            EnumWindows([](HWND w, LPARAM) -> BOOL {
                if (isDosBoxWindow(w)) PostMessageW(w, WM_CLOSE, 0, 0);
                return TRUE;
            }, 0);
            if (WaitForSingleObject(a->pi.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(a->pi.hProcess, 0);
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// Used by the deploy program:  OTAccess.exe --prepare
// Creates OREGONM.EXE and checks the setup.  Results go to Logs\prepare.log
// (a GUI program has no console for the batch file to read).
static int prepareOnly(App& a) {
    std::string out;
    int rc = 0;
    PatchResult r = ensurePatchedExe(narrow(a.gameDir));
    out += r.message + "\n";
    if (!r.ok) rc = 2;
    if (!rc && !fileExists(a.gameDir + L"\\OTMOD.COM")) { out += "OTMOD.COM is missing from the game folder.\n"; rc = 3; }
    if (!rc) {
        std::wstring db = findDosBox(a.cfg);
        if (db.empty()) { out += "DOSBox Staging was not found.\n"; rc = 4; }
        else out += "DOSBox Staging: " + narrow(db) + "\n";
    }
    FILE* f = _wfopen((a.modDir + L"\\Logs\\prepare.log").c_str(), L"w");
    if (f) { fputs(out.c_str(), f); fclose(f); }
    return rc;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    App app;
    gApp = &app;
    app.modDir = exeDir();
    CreateDirectoryW((app.modDir + L"\\Logs").c_str(), nullptr);
    // keep the previous session's log next to the new one
    MoveFileExW((app.modDir + L"\\Logs\\otaccess.log").c_str(), (app.modDir + L"\\Logs\\otaccess_previous.log").c_str(),
                MOVEFILE_REPLACE_EXISTING);
    gLog.open(narrow(app.modDir + L"\\Logs\\otaccess.log"));
    app.cfg.load(narrow(app.modDir + L"\\OTAccess.ini"));
    std::wstring gd = widen(app.cfg.get("game.folder", ""));
    app.gameDir = gd.empty() ? app.modDir + L"\\Game Files" : gd;

    std::wstring args = cmdLine ? cmdLine : L"";
    if (args.find(L"--prepare") != std::wstring::npos) {
        return prepareOnly(app);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    app.speech.reset(new WinSpeech(app.cfg));
    app.speech->onSpeak = appendHistory;
    app.sound.reset(new WinSound(app.cfg));

    // only one copy
    HANDLE single = CreateMutexW(nullptr, TRUE, L"OTAccessSingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        app.speech->speak("The Oregon Trail accessibility mod is already running.", true);
        Sleep(2500);
        return 0;
    }

    if (!dirExists(app.gameDir)) {
        std::wstring msg = L"The game folder was not found:\n" + app.gameDir;
        app.speech->speak("The game folder was not found.", true);
        MessageBoxW(nullptr, msg.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    PatchResult pr = ensurePatchedExe(narrow(app.gameDir));
    LOGF("patch: %s", pr.message.c_str());
    if (!pr.ok) {
        app.speech->speak(pr.message, true);
        MessageBoxW(nullptr, widen(pr.message).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!fileExists(app.gameDir + L"\\OTMOD.COM")) {
        const char* msg = "OTMOD.COM is missing from the game folder. Run the deploy program again.";
        app.speech->speak(msg, true);
        MessageBoxW(nullptr, widen(msg).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"OTAccessWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, kAppTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               520, 360, nullptr, nullptr, inst, nullptr);
    ShowWindow(app.hwnd, show);
    UpdateWindow(app.hwnd);
    app.hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyHook, inst, 0);
    if (!app.hook) LOGF("keyboard hook failed: %lu", GetLastError());

    app.speech->speak("Starting The Oregon Trail.", true);
    startGame();
    SetTimer(app.hwnd, kTimerPoll, 33, nullptr);

    // bring our window back in front once DOSBox has opened its window, so
    // the screen reader stays with us (either window can be used to play)
    bool refocused = false;
    uint64_t started = nowMs();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!refocused && app.dosboxRunning && nowMs() - started > 1500) {
            HWND f = GetForegroundWindow();
            if (isDosBoxWindow(f)) {
                refocused = true;
                if (app.cfg.getBool("window.keep_focus", true)) {
                    // the foreground lock allows this because DOSBox was started by us just now
                    DWORD fg = GetWindowThreadProcessId(f, nullptr), me = GetCurrentThreadId();
                    AttachThreadInput(me, fg, TRUE);
                    SetForegroundWindow(app.hwnd);
                    SetFocus(app.edit);
                    AttachThreadInput(me, fg, FALSE);
                }
            } else if (nowMs() - started > 15000) {
                refocused = true;
            }
        }
    }
    if (app.hook) UnhookWindowsHookEx(app.hook);
    app.mod.reset();
    app.guest.reset();
    app.mem.reset();
    if (app.pi.hProcess) CloseHandle(app.pi.hProcess);
    if (app.pi.hThread) CloseHandle(app.pi.hThread);
    app.sound.reset();
    app.speech.reset();
    CoUninitialize();
    gApp = nullptr;
    return 0;
}
#endif  // _WIN32

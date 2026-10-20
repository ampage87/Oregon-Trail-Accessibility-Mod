#include "resources.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

std::string upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool Gxl::open(const std::string& path) {
    entries_.clear();
    path_ = path;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> hdr(64 * 1024);
    size_t n = fread(hdr.data(), 1, hdr.size(), f);
    fclose(f);
    if (n < 0x100) return false;
    unsigned pos = 0x80, first = 0xFFFFFFFFu;
    while (pos + 26 <= n && pos < first) {
        char name[14] = {0};
        memcpy(name, &hdr[pos + 1], 13);
        name[13] = 0;
        std::string nm;
        for (int i = 0; i < 13 && name[i]; i++) if (name[i] != ' ') nm += name[i];
        if (nm.empty()) break;
        unsigned off = hdr[pos + 14] | (hdr[pos + 15] << 8) | (hdr[pos + 16] << 16) | ((unsigned)hdr[pos + 17] << 24);
        unsigned size = hdr[pos + 18] | (hdr[pos + 19] << 8) | (hdr[pos + 20] << 16) | ((unsigned)hdr[pos + 21] << 24);
        entries_[upper(nm)] = Entry{off, size};
        if (off < first) first = off;
        pos += 26;
    }
    return !entries_.empty();
}

bool Gxl::get(const std::string& name, std::string& data) const {
    auto it = entries_.find(upper(trim(name)));
    if (it == entries_.end()) return false;
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) return false;
    data.resize(it->second.size);
    fseek(f, (long)it->second.off, SEEK_SET);
    size_t n = fread(&data[0], 1, it->second.size, f);
    fclose(f);
    data.resize(n);
    return n == it->second.size;
}

std::vector<std::string> ctrImageList(const std::string& text) {
    std::vector<std::string> lines, out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(trim(cur)); cur.clear(); }
        else if (c != '\r' && c != 0x1A) cur += c;
    }
    if (!cur.empty()) lines.push_back(trim(cur));
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& l = lines[i];
        if (l.size() >= 3 && l[0] == '1' && l[1] == ',') {
            int n = atoi(l.c_str() + 2);
            for (int k = 0; k < n && i + 1 + k < lines.size(); k++) {
                std::string nm = upper(trim(lines[i + 1 + k]));
                // .PCC is resolved by the game to .PC8 / .PC4
                out.push_back(nm);
            }
            break;
        }
    }
    return out;
}

static std::string base(const std::string& img) {
    std::string s = upper(trim(img));
    size_t dot = s.find('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    return s;
}

std::string imageFrameLabel(const std::string& imageName, int frame) {
    static const std::map<std::string, std::vector<std::string>> t = {
        {"OKAY", {"OK", "Cancel", "OK"}},
        {"TITLEBTN", {"Introduction", "Options", "Quit", "Travel the Trail"}},
        {"LEGEND", {"Introduction", "Options", "Quit", "Travel the Trail"}},
        {"INTROBUT", {"Next", "Done", "Previous", "Done", "Previous, unavailable", "Next, unavailable", "Next"}},
        {"CONTBTN", {"Options", "Continue on the trail", "Time out, stop the wagon"}},
        {"LOADGAME", {"Load Game", "New Game"}},
        {"FILEBTN", {"Open", "Drive", "Load", "Cancel", "Open, unavailable", "Load, unavailable", "Save",
                     "Save, unavailable", "Replace", "Okay"}},
        {"YESNO", {"Yes", "No"}},
        {"EXIT", {"Yes", "No", "Cancel"}},
        {"CROSSBUT", {"Attempt to ford the river", "Caulk the wagon and float it", "Take the ferry",
                      "Hire an Indian guide"}},
        {"HLPBTN", {"Help"}},
        {"TRLBTN", {"Map", "Guidebook", "Status", "Buy supplies", "Trade", "Talk to people", "Rest", "Hunt for food"}},
        {"MONTH", {"March", "April", "May", "June", "July", "August"}},
        {"HUNTBUT", {"Move to a new hunting area", "End hunting"}},
        {"RAFTBTN", {"Get information", "Begin rafting"}},
        {"VALOK", {"OK"}},
        {"OPTBTN1", {"Export trail log", "Save game", "Quit to DOS", "Exit game", "Credits"}},
        {"OPTBTN2", {"Credits", "Exit", "Quit", "Done", "Delete", "Reset", "Configure"}},
        {"INDEX", {"Index"}},
        {"PACE", {"Steady pace", "Strenuous pace", "Grueling pace"}},
        {"RATION", {"Filling rations", "Meager rations", "Bare bones rations"}},
        {"SPLIT1", {"Take the shortcut", "Head to the fort"}},
        {"SPLIT2", {"Head to the fort", "Take the shortcut"}},
        {"STORE1", {"Buy", "Help"}},
        {"STORE2", {"Buy", "Cancel"}},
        {"SCROLL", {"Scroll up", "Scroll up", "Scroll down", "Scroll down", "Scroll"}},
    };
    auto it = t.find(base(imageName));
    if (it == t.end() || frame < 0 || frame >= (int)it->second.size()) return "";
    return it->second[frame];
}

std::string screenControlLabel(const std::string& ctr, int id, int frame, const std::string& imageName) {
    std::string c = base(ctr);
    if (c == "TRAIL") {
        switch (id) {
        case 10: return "Map";
        case 11: return "Guidebook";
        case 12: return "Status";
        case 13: return "Change food rations";
        case 14: return "Buy supplies";
        case 15: return "Trade";
        case 16: return "Talk to people";
        case 17: return "Rest";
        case 18: return "Change pace";
        case 19: return "Hunt for food";
        case 20: return frame == 2 ? "Time out, stop the wagon" : "Continue on the trail";
        case 21: return "Options";
        }
    }
    if (c == "OPTIONS" || c == "OPT1") {
        switch (id) {
        case 10: return "Export trail log";
        case 11: return "Save game";
        case 12: return "Credits";
        case 13: return "Exit game";
        case 14: return "Quit to DOS";
        case 15: return "OK";
        case 16: return "Delete legend";
        case 17: return "Reset legends";
        case 18: return "Cancel";
        case 5000: return "Instructions";
        }
    }
    if (c == "WHOAMI" && id == 5) return "OK";
    (void)imageName;
    return "";
}

char screenAccelerator(const std::string& ctr, int id) {
    std::string c = base(ctr);
    if (c == "TRAIL") {
        switch (id) {
        case 10: return 'M'; case 11: return 'G'; case 12: return 'S'; case 13: return 'A';
        case 14: return 'B'; case 15: return 'T'; case 16: return 'K'; case 17: return 'R';
        case 18: return 'P'; case 19: return 'H'; case 20: return 'C'; case 21: return 'O';
        }
    }
    if (c == "HUNT") { if (id == 1) return 'M'; if (id == 2) return 'E'; }
    if (c == "RAFTING") { if (id == 1) return 'I'; if (id == 2) return 'B'; }
    return 0;
}

std::string screenTitle(const std::string& ctr) {
    static const std::map<std::string, std::string> t = {
        {"TITLE", "Title screen"}, {"WELCOME", "Welcome"}, {"WHOAMI", "Party setup"},
        {"MONTH", "Starting date"}, {"TRAIL", "On the trail"}, {"STORE1", "General store"},
        {"STORE2", "General store"}, {"OPTIONS", "Options"}, {"OPT1", "Options"}, {"HUNT", "Hunting"},
        {"RAFTING", "Rafting"}, {"RIVER1", "River crossing"}, {"RIVER2", "River crossing"},
        {"RIVER3", "River crossing"}, {"PACE", "Change pace"}, {"RATION", "Change food rations"},
        {"REST", "Rest"}, {"TRADE", "Trade"}, {"GETFILE", "Load a saved game"}, {"PUTFILE1", "Export trail log"},
        {"PUTFILE2", "Save game"}, {"SPLIT1", "The trail divides"}, {"SPLIT2", "The trail divides"},
        {"INDEX", "Guidebook index"}, {"GUIDE", "Guidebook"}, {"LEGEND", "The Oregon Trail List of Legends"},
        {"SCORE1", "Score"}, {"SCORE2", "Score"}, {"VALLEY", "Congratulations! You have reached the Willamette Valley"}, {"CREDITS", "Credits"},
        {"INTRO1", "Introduction"}, {"INTRO2", "Introduction"}, {"INTRO3", "Introduction"},
        {"OTCONF", "Sound configuration"}, {"OPTEXT", "Exit game"}, {"EXIT", "Exit game"},
        {"RAFTINFO", "Rafting information"}, {"STOHELP", "Store help"}, {"WHOHELP", "Occupation help"},
        {"RIVHELP", "River crossing help"}, {"PACEHELP", "Pace help"}, {"RATHELP", "Rations help"},
        {"OPTHELP", "Options help"},
    };
    auto it = t.find(base(ctr));
    return it == t.end() ? "" : it->second;
}

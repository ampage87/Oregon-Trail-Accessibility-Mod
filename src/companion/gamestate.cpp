#include "gamestate.h"
#include <cstdio>

// state structure offsets (see Plan & Research Documents/RE_NOTES.md)
enum {
    S_NAMES = 0x00, S_OCC = 0x4B, S_COND = 0x4D, S_HEALTH = 0x63, S_TONGUES = 0x69, S_WHEELS = 0x6B,
    S_AXLES = 0x6D, S_BULLETS = 0x6F, S_CLOTHES = 0x71, S_FOOD = 0x73, S_OXEN = 0x75, S_MONEY = 0x79,
    S_PACE = 0x7D, S_RATIONS = 0x7F, S_YEAR = 0x81, S_MONTH = 0x83, S_DAY = 0x85, S_MILES = 0x87,
    S_LANDMARK = 0x8B, S_ROUTE = 0x8D, S_STOPPED = 0x8F, S_WEATHER = 0x91, S_TEMP = 0x93,
    S_SNOW = 0x95, S_RAIN = 0x99, S_RESTING = 0x9D, S_DELAYED = 0x9F, S_SIZE = 0xA5
};

static int16_t w16(const uint8_t* b, int off) { return (int16_t)(b[off] | (b[off + 1] << 8)); }
static int32_t w32(const uint8_t* b, int off) { return (int32_t)((uint16_t)w16(b, off) | ((uint32_t)(uint16_t)w16(b, off + 2) << 16)); }

bool GameState::read(Guest& g, FarPtr p) {
    valid = false;
    if (p.null()) return false;
    uint8_t b[S_SIZE];
    if (!g.read(p.lin(), b, sizeof b)) return false;
    for (int i = 0; i < 5; i++) {
        std::string n;
        for (int k = 0; k < 15 && b[S_NAMES + 15 * i + k]; k++) {
            char c = (char)b[S_NAMES + 15 * i + k];
            if (c < 32 || c > 126) { n.clear(); break; }
            n += c;
        }
        names[i] = n;
        cond[i] = w16(b, S_COND + 2 * i);
    }
    occupation = w16(b, S_OCC);
    health = w16(b, S_HEALTH);
    tongues = w16(b, S_TONGUES); wheels = w16(b, S_WHEELS); axles = w16(b, S_AXLES);
    bullets = w16(b, S_BULLETS); clothes = w16(b, S_CLOTHES); food = w16(b, S_FOOD); oxen = w16(b, S_OXEN);
    money = w32(b, S_MONEY);
    pace = w16(b, S_PACE); rations = w16(b, S_RATIONS);
    year = w16(b, S_YEAR); month = w16(b, S_MONTH); day = w16(b, S_DAY);
    miles = w32(b, S_MILES);
    landmark = w16(b, S_LANDMARK); route = w16(b, S_ROUTE);
    stopped = w16(b, S_STOPPED); resting = w16(b, S_RESTING); delayed = w16(b, S_DELAYED);
    weather = w16(b, S_WEATHER); temperature = w16(b, S_TEMP);
    snow = w32(b, S_SNOW); rain = w32(b, S_RAIN);
    // sanity: year and month must look right, otherwise the pointer is stale
    valid = (year >= 1848 && year <= 1850 && month >= 0 && month < 12 && day >= 1 && day <= 31 &&
             occupation >= 0 && occupation < 8);
    return valid;
}

int GameState::alive() const {
    int n = 0;
    for (int i = 0; i < 5; i++) if (!names[i].empty() && cond[i] != 10) n++;
    return n;
}

std::string plural(long n, const char* one, const char* many) {
    char b[96];
    snprintf(b, sizeof b, "%ld %s", n, n == 1 ? one : many);
    return b;
}

std::string fmtMoney(long cents) {
    char b[48];
    if (cents % 100 == 0) snprintf(b, sizeof b, "%ld dollars", cents / 100);
    else snprintf(b, sizeof b, "%ld dollars and %ld cents", cents / 100, cents % 100);
    return b;
}

static const char* kMonths[12] = {"January", "February", "March", "April", "May", "June", "July",
                                  "August", "September", "October", "November", "December"};
std::string fmtDate(const GameState& s) {
    char b[48];
    snprintf(b, sizeof b, "%s %d, %d", kMonths[(s.month % 12 + 12) % 12], s.day, s.year);
    return b;
}

std::string healthName(int h) {
    int i = h / 35;
    static const char* n[] = {"good", "fair", "poor", "very poor", "dying"};
    if (i < 0) i = 0;
    if (i > 4) i = 4;
    return n[i];
}

std::string conditionName(int c) {
    static const char* n[] = {"exhaustion", "typhoid fever", "cholera", "measles", "dysentery", "a fever",
                              "a broken leg", "a broken arm", "a snakebite", "healthy", "dead"};
    if (c < 0 || c > 10) return "unknown";
    return n[c];
}

std::string weatherName(int w) {
    static const char* n[] = {"sunny", "partly cloudy", "cloudy", "rainy", "very rainy", "snowy",
                              "very snowy", "a severe storm", "a severe blizzard", "a hail storm"};
    if (w < 0 || w > 9) return "unknown weather";
    return n[w];
}

std::string temperatureName(int t) {
    static const char* n[] = {"very cold", "cold", "cool", "warm", "hot", "very hot"};
    if (t < 0) t = 0;
    if (t > 5) t = 5;
    return n[t];
}

std::string paceName(int p) {
    static const char* n[] = {"steady", "strenuous", "grueling"};
    return (p >= 0 && p < 3) ? n[p] : "unknown";
}
std::string rationsName(int r) {
    static const char* n[] = {"filling", "meager", "bare bones"};
    return (r >= 0 && r < 3) ? n[r] : "unknown";
}
std::string occupationName(int o) {
    static const char* n[] = {"banker", "blacksmith", "carpenter", "doctor", "farmer", "merchant",
                              "saddlemaker", "teacher"};
    return (o >= 0 && o < 8) ? n[o] : "unknown";
}

static const char* kLandmarks[18] = {
    "Independence, Missouri", "the Kansas River crossing", "the Big Blue River crossing", "Fort Kearney",
    "Chimney Rock", "Fort Laramie", "Independence Rock", "South Pass", "Fort Bridger",
    "the Green River crossing", "Soda Springs", "Fort Hall", "the Snake River crossing", "Fort Boise",
    "the Grande Ronde in the Blue Mountains", "Fort Walla Walla", "The Dalles", "the Willamette Valley"};

std::string landmarkName(int idx) {
    if (idx < 0 || idx > 17) return "the next landmark";
    return kLandmarks[idx];
}

// DS:0C58 in OREGON.EXE - cumulative miles per route (0 = not on route)
static const int kMiles[4][19] = {
    {0, 102, 185, 304, 554, 640, 830, 932, 989, 1151, 1295, 1352, 1534, 1648, 1808, 1863, 1983, 2083, 0},
    {0, 102, 185, 304, 554, 640, 830, 932, 0, 1057, 1201, 1258, 1440, 1554, 1714, 1769, 1889, 1989, 0},
    {0, 102, 185, 304, 554, 640, 830, 932, 989, 1151, 1295, 1352, 1534, 1648, 1808, 0, 1933, 2033, 0},
    {0, 102, 185, 304, 554, 640, 830, 932, 0, 1057, 1201, 1258, 1440, 1554, 1714, 0, 1839, 1939, 0},
};

int landmarkMiles(int route, int idx) {
    if (route < 0 || route > 3 || idx < 0 || idx > 18) return -1;
    if (idx > 0 && kMiles[route][idx] == 0) return -1;
    return kMiles[route][idx];
}

int nextLandmarkIndex(const GameState& s) {
    int idx = s.landmark;
    while (idx <= 17 && landmarkMiles(s.route, idx) < 0) idx++;
    return idx;
}

std::string wagonStatus(const GameState& s) {
    if (s.stopped) return "stopped";
    if (s.resting) return "resting";
    if (s.delayed) return "delayed";
    return "moving";
}

std::string locationText(const GameState& s) {
    char b[256];
    int idx = nextLandmarkIndex(s);
    int m = landmarkMiles(s.route, idx);
    if (m >= 0 && idx <= 17) {
        long left = m - s.miles;
        if (left < 0) left = 0;
        if (left == 0)
            snprintf(b, sizeof b, "You are at %s. %ld miles traveled.", landmarkName(idx).c_str(), s.miles);
        else
            snprintf(b, sizeof b, "%ld miles to %s. %ld miles traveled.", left, landmarkName(idx).c_str(), s.miles);
    } else {
        snprintf(b, sizeof b, "%ld miles traveled.", s.miles);
    }
    return b;
}

std::string summaryText(const GameState& s) {
    char b[512];
    snprintf(b, sizeof b, "%s. Weather %s, %s. Health %s. %d pounds of food. Pace %s, rations %s. Wagon %s. %s",
             fmtDate(s).c_str(), weatherName(s.weather).c_str(), temperatureName(s.temperature).c_str(),
             healthName(s.health).c_str(), s.food, paceName(s.pace).c_str(), rationsName(s.rations).c_str(),
             wagonStatus(s).c_str(), locationText(s).c_str());
    return b;
}

std::string partyText(const GameState& s) {
    std::string out;
    for (int i = 0; i < 5; i++) {
        if (s.names[i].empty()) continue;
        out += s.names[i];
        if (s.cond[i] == 10) out += ", dead. ";
        else if (s.cond[i] == 9) out += ", no ailments. ";
        else out += ", has " + conditionName(s.cond[i]) + ". ";
    }
    out += "Overall health " + healthName(s.health) + ". ";
    out += "Occupation " + occupationName(s.occupation) + ".";
    return out;
}

std::string suppliesText(const GameState& s) {
    std::string out;
    out += plural(s.oxen, "ox", "oxen") + ", ";
    out += plural(s.clothes, "set of clothing", "sets of clothing") + ", ";
    out += plural(s.bullets, "bullet", "bullets") + ", ";
    out += plural(s.wheels, "wagon wheel", "wagon wheels") + ", ";
    out += plural(s.axles, "wagon axle", "wagon axles") + ", ";
    out += plural(s.tongues, "wagon tongue", "wagon tongues") + ", ";
    out += plural(s.food, "pound of food", "pounds of food") + ". ";
    out += "Money " + fmtMoney(s.money) + ".";
    return out;
}

std::string mapText(const GameState& s) {
    std::string out = "Map. " + locationText(s);
    int idx = nextLandmarkIndex(s);
    int listed = 0;
    std::string ahead;
    for (int i = idx + 1; i <= 17 && listed < 4; i++) {
        int m = landmarkMiles(s.route, i);
        if (m < 0) continue;
        long left = m - s.miles;
        if (left <= 0) continue;
        ahead += (listed ? ", " : " Then ") + landmarkName(i) + " in " + std::to_string(left) + " miles";
        listed++;
    }
    if (listed) out += ahead + ".";
    long total = landmarkMiles(s.route, 17) - s.miles;
    if (total > 0) out += " About " + std::to_string(total) + " miles to the Willamette Valley.";
    if (s.route == 0 && s.landmark <= 7) out += " The trail divides after South Pass and again after the Blue Mountains.";
    return out;
}

// Number keys 1..9 and 0 on the trail: one part of the status at a time.
std::string statusDetail(int key, const GameState& s) {
    switch (key) {
    case 1:
        return fmtDate(s) + ". Weather " + weatherName(s.weather) + ", " + temperatureName(s.temperature) + ".";
    case 2: {
        std::string out;
        for (int i = 0; i < 5; i++) {
            if (s.names[i].empty()) continue;
            out += s.names[i];
            if (s.cond[i] == 10) out += ", dead. ";
            else if (s.cond[i] == 9) out += ", well. ";
            else out += ", " + conditionName(s.cond[i]) + ". ";
        }
        return out + "Overall health " + healthName(s.health) + ".";
    }
    case 3: {
        std::string out = plural(s.food, "pound of food", "pounds of food") + ", rations " + rationsName(s.rations) + ".";
        int people = s.alive();
        int perDay = people * (s.rations == 0 ? 3 : s.rations == 1 ? 2 : 1);
        if (perDay > 0) out += " About " + plural(s.food / perDay, "day", "days") + " at this rate.";
        return out;
    }
    case 4: return plural(s.oxen, "ox", "oxen") + ".";
    case 5: return plural(s.clothes, "set of clothing", "sets of clothing") + ", " + plural(s.bullets, "bullet", "bullets") + ".";
    case 6:
        return "Spare parts: " + plural(s.wheels, "wheel", "wheels") + ", " + plural(s.axles, "axle", "axles") + ", " +
               plural(s.tongues, "tongue", "tongues") + ".";
    case 7: return "Money " + fmtMoney(s.money) + ".";
    case 8:
        return "Pace " + paceName(s.pace) + ", rations " + rationsName(s.rations) + ", health " + healthName(s.health) +
               ". Wagon " + wagonStatus(s) + ".";
    case 9: return locationText(s);
    default: return summaryText(s) + " " + suppliesText(s) + " " + partyText(s);
    }
}

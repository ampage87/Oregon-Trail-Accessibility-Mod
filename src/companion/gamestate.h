// Decoding of the game's party / wagon state structure.
#pragma once
#include "guest.h"
#include <string>

struct GameState {
    bool valid = false;
    std::string names[5];
    int occupation = 0;
    int cond[5] = {9, 9, 9, 9, 9};   // 0..8 ailment, 9 none, 10 deceased
    int health = 0;                  // 0..34 good ... 140+ dying (lower is better)
    int tongues = 0, wheels = 0, axles = 0, bullets = 0, clothes = 0, food = 0, oxen = 0;
    long money = 0;                  // cents
    int pace = 0, rations = 0;
    int year = 1848, month = 0, day = 1;
    long miles = 0;
    int landmark = 0, route = 0;
    int stopped = 0, resting = 0, delayed = 0;
    int weather = 0, temperature = 0;
    long snow = 0, rain = 0;

    bool read(Guest& g, FarPtr p);
    int alive() const;
};

// formatting helpers (all return plain sentences for speech)
std::string fmtMoney(long cents);
std::string fmtDate(const GameState& s);
std::string healthName(int health);
std::string conditionName(int c);
std::string weatherName(int w);
std::string temperatureName(int t);
std::string paceName(int p);
std::string rationsName(int r);
std::string occupationName(int o);
std::string landmarkName(int idx);         // e.g. "the Kansas River Crossing"
int landmarkMiles(int route, int idx);      // cumulative miles, -1 if not on route
int nextLandmarkIndex(const GameState& s);  // next landmark actually on this route
std::string wagonStatus(const GameState& s);

std::string summaryText(const GameState& s);    // date, weather, health, food, next landmark
std::string partyText(const GameState& s);
std::string suppliesText(const GameState& s);
std::string locationText(const GameState& s);
std::string mapText(const GameState& s);   // spoken when the Map button is used
std::string statusDetail(int key, const GameState& s);  // number keys 1-9, 0 = everything
std::string plural(long n, const char* one, const char* many);

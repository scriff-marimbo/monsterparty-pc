// Monster Party native port: leaderboards (not in the original game).
//
// A finished game (the final boss beaten, with no password, trainer or other cheat) is logged by
// Cpu::lb_final_boss (nes.cpp) with its time and its score. There's one leaderboard per
//     ROM (US / Japanese prototype) + difficulty + Remix Mode on or off
// ("US - Nasty", "JP - Normal (Remix)"), and each keeps two top-10 lists: the fastest times and the
// highest scores. They're stored in leaderboards.ini next to settings.ini. This file has no
// emulator in it, so the launcher can show the lists without a game running.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

constexpr int LB_MAX = 10;   // places kept in each list

// which leaderboard
struct LbCategory {
    bool proto = false;      // the Japanese prototype (else the US release)
    int difficulty = 1;      // 0 Casual, 1 Normal, 2 Nasty
    bool remix = false;
    std::string key() const;     // "us.nasty.0": the name used in the file
    std::string title() const;   // "US - Nasty (Remix)": for the launcher
    // the short form on the game's own screen, in the game's font (capitals, digits, '-'): "JP - NORMAL - REMIX"
    std::string screen_text() const;
    bool operator==(const LbCategory& o) const { return proto == o.proto && difficulty == o.difficulty && remix == o.remix; }
};

// one finished game
struct LbEntry {
    uint32_t frames = 0;     // NES frames from the start of Round 1 to the final boss's defeat
    uint32_t score = 0;      // the run's best score (the game shows scores as 7 digits, the last always 0)
    int64_t when = 0;        // unix time it was logged
};

// where a new entry landed: 1-based, 0 = didn't make the top 10
struct LbPlace { int time = 0, score = 0; };

struct LbBoard {
    std::vector<LbEntry> by_time;     // fastest first, at most LB_MAX
    std::vector<LbEntry> by_score;    // highest first, at most LB_MAX
};

class Leaderboards {
public:
    // Reads the file; a missing file is just empty. Bad lines are skipped. Returns false only if the file
    // exists but can't be read.
    bool load(const std::string& path);
    // Writes the whole file (to a temporary file first, then moved over the old one).
    bool save(const std::string& path) const;
    // Adds a game to a leaderboard. An entry that ties with one already there goes below it.
    LbPlace add(const LbCategory& c, const LbEntry& e);
    const LbBoard& get(const LbCategory& c) const;   // an empty board if there's none yet
private:
    std::map<std::string, LbBoard> boards_;
};

// Logs a finished game: reads path, adds it, writes path back. An empty path only works out the place
// (nothing is read or written). ok is false if the file couldn't be written (the place is still right).
LbPlace lb_log(const std::string& path, const LbCategory& c, const LbEntry& e, bool* ok = nullptr);

// ---- formatting
// NES frames as seconds: the NTSC NES runs 60.0988 frames a second
double lb_seconds(uint32_t frames);
std::string lb_clock(uint32_t frames);        // "02:03:46" (hours capped at 99), as shown in the game
std::string lb_clock_cs(uint32_t frames);     // "2:03:46.12": with hundredths, for the launcher
std::string lb_score_digits(uint32_t score);  // "0840200": 7 digits, as the game shows it
std::string lb_date(int64_t when);            // "2026-10-25" in local time, "" if unknown

// A score as the game keeps it: 6 digits, least significant first (the score at RAM $8A-$8F, HIGH at $90-$95);
// the game shows them as 7 digits with a 0 after them. Returns the score in points (200,000 = 200000).
uint32_t lb_score_from_digits(const uint8_t digits[6]);

// Leaderboards: the lists and their file (quick), and the game side (the run, the result screen).
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "leaderboard.h"
#include "nes.h"
#include "ram_symbols.h"
#include "support.h"

using namespace mptest;

static std::string temp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("mp_test_") + name)).string();
}

TEST_CASE("leaderboard categories", "[leaderboards]") {
    CHECK(LbCategory{false, 2, false}.key() == "us.nasty.0");
    CHECK(LbCategory{true, 1, true}.key() == "jp.normal.1");
    CHECK(LbCategory{false, 2, false}.title() == "US - Nasty");
    CHECK(LbCategory{true, 1, true}.title() == "JP - Normal (Remix)");
    CHECK(LbCategory{true, 1, true}.screen_text() == "JP - NORMAL - REMIX");
    CHECK(LbCategory{false, 0, false}.screen_text() == "US - CASUAL");
}

TEST_CASE("leaderboard formatting", "[leaderboards]") {
    CHECK(lb_clock(0) == "00:00:00");
    CHECK(lb_clock(uint32_t(2 * 3600 * 60.0988139 + 3 * 60 * 60.0988139 + 46 * 60.0988139 + 5)) == "02:03:46");
    CHECK(lb_score_digits(840200) == "0840200");
    CHECK(lb_score_digits(200000) == "0200000");
    const uint8_t d[6] = {0, 2, 0, 4, 8, 0};   // least significant first: the screen shows 084020 and a 0 (0840200)
    CHECK(lb_score_from_digits(d) == 840200);
    CHECK(lb_date(0) == "");
}

TEST_CASE("leaderboard places", "[leaderboards]") {
    Leaderboards lb;
    const LbCategory c{false, 1, false};
    CHECK(lb.add(c, {1000, 300000, 1}).time == 1);
    LbPlace p = lb.add(c, {900, 200000, 2});
    CHECK(p.time == 1);
    CHECK(p.score == 2);
    p = lb.add(c, {900, 300000, 3});   // ties go below the older entry
    CHECK(p.time == 2);
    CHECK(p.score == 2);
    CHECK(lb.get(c).by_time[0].when == 2);
    // other boards are separate
    CHECK(lb.get(LbCategory{true, 1, false}).by_time.empty());
    // only 10 are kept; one that doesn't make it has no place
    for (int i = 0; i < 20; i++) lb.add(c, {uint32_t(100 + i), uint32_t(500000 + i * 10), 10});
    CHECK(lb.get(c).by_time.size() == size_t(LB_MAX));
    CHECK(lb.get(c).by_score.size() == size_t(LB_MAX));
    p = lb.add(c, {99999, 1000, 11});
    CHECK(p.time == 0);
    CHECK(p.score == 0);
    CHECK(lb.get(c).by_time.size() == size_t(LB_MAX));
}

TEST_CASE("leaderboard file", "[leaderboards]") {
    const std::string path = temp_file("lb_file.ini");
    std::remove(path.c_str());
    const LbCategory a{false, 2, false}, b{true, 1, true};
    bool ok = false;
    CHECK(lb_log(path, a, {5000, 400000, 11}, &ok).time == 1);
    CHECK(ok);
    CHECK(lb_log(path, a, {4000, 300000, 12}).time == 1);
    CHECK(lb_log(path, b, {7000, 200000, 13}).score == 1);
    Leaderboards lb;
    CHECK(lb.load(path));
    CHECK(lb.get(a).by_time.size() == 2);
    CHECK(lb.get(a).by_time[0].frames == 4000);
    CHECK(lb.get(a).by_score[0].score == 400000);
    CHECK(lb.get(b).by_time.size() == 1);
    // junk lines are skipped
    { std::ofstream f(path, std::ios::app); f << "garbage\ntime us.nasty.0 x\n"; }
    CHECK(lb.load(path));
    CHECK(lb.get(a).by_time.size() == 2);
    // a file that can't be written is reported, and the place is still worked out
    CHECK(lb_log("/nonexistent-dir/x.ini", a, {1, 1, 1}, &ok).time == 1);
    CHECK_FALSE(ok);
    std::remove(path.c_str());
}

// The game side, without running the game: the hooks are plain methods on the CPU.
TEST_CASE("a finished run is logged", "[leaderboards]") {
    const std::string path = temp_file("lb_run.ini");
    std::remove(path.c_str());
    Nes nes;
    nes.lb.enabled = true;
    nes.lb.path = path;
    nes.lb.category = LbCategory{false, 1, false};
    nes.cpu.nes = &nes;   // (no ROM loaded: reset() isn't used)
    Cpu& c = nes.cpu;
    nes.is_cheating = false;
    nes.lb_new_run();
    REQUIRE(nes.lb.run);
    nes.frame_count = 100;
    c.lb_round_start();
    nes.frame_count = 600;
    c.lb_round_start();                      // a continue comes back to ROUND START: the clock goes on
    c.ram[BossesLeft] = 2; c.ram[RoundNumber] = 8;
    c.lb_boss_reward();                      // not the last boss of the round
    CHECK_FALSE(nes.lb.killed);
    c.ram[BossesLeft] = 1;
    nes.frame_count = 5100;
    c.lb_boss_reward();
    CHECK(nes.lb.killed);
    nes.frame_count = 5400;                  // (the game waits a little after the kill)
    // the score: the run's own best, kept over a continue (which clears the score); not HIGH
    auto set_score = [&](uint16_t at, const std::array<uint8_t, 6>& d) { for (int i = 0; i < 6; i++) c.ram[at + i] = d[i]; };
    set_score(HiScoreDigits, {0, 0, 0, 0, 9, 0});   // HIGH 900,000 from an earlier game: ignored
    set_score(ScoreDigits, {0, 2, 0, 4, 8, 0});     // 840,200 ...
    c.lb_track_score();
    set_score(ScoreDigits, {0, 0, 0, 0, 0, 0});     // ... then a Game Over and a continue
    c.lb_track_score();
    set_score(ScoreDigits, {0, 0, 0, 0, 3, 0});     // 300,000 when the final boss goes down
    c.lb_final_boss();
    REQUIRE(nes.lb.finished);
    CHECK(nes.lb.result.frames == 5000);
    CHECK(nes.lb.result.score == 840200);
    CHECK(nes.lb.place.time == 1);
    CHECK(nes.lb.place.score == 1);
    Leaderboards lb;
    lb.load(path);
    CHECK(lb.get(nes.lb.category).by_time.size() == 1);
    // the credits' last page: nothing until the text is done and a second has passed, then A or Start
    c.ram[JoyPressed] = 0x90;
    c.ram[StoryTextFlags] = 0x83;             // the credits are still being typed: no
    for (int i = 0; i < 300; i++) CHECK_FALSE(c.lb_credits_idle());
    c.ram[StoryTextFlags] = 0x43;             // the text has reached its end (bit 6), as the game leaves it
    for (int i = 0; i < 58; i++) CHECK_FALSE(c.lb_credits_idle());
    CHECK(c.lb_credits_idle() == false);
    c.ram[JoyPressed] = 0;
    CHECK_FALSE(c.lb_credits_idle());
    c.ram[JoyPressed] = 0x10;
    CHECK(c.lb_credits_idle());
    CHECK(c.ram[ScreenId] == 6);
    CHECK(nes.lb.showing);
    // the result screen's text is drawn over the GAME OVER screen's room
    nes.chr.assign(0x20000, 0);
    nes.lb_draw_screen();
    auto row = [&](int r, int from, int n) {
        std::string s;
        for (int i = 0; i < n; i++) s += char(nes.vram[nes.nt_index(uint16_t(0x2000 + r * 32 + from + i))]);
        return s;
    };
    auto tiles = [](const std::string& text) {
        std::string s;
        for (char ch : text) s += char(ch >= '0' && ch <= '9' ? 0x04 + ch - '0' : ch >= 'A' && ch <= 'Z' ? 0x0E + ch - 'A' : ch == ':' ? 0xF0 : ch == '#' ? 0xF1 : 0x01);
        return s;
    };
    CHECK(row(17, 8, 13) == tiles("TIME 00:01:23 #1").substr(0, 13));
    CHECK(row(19, 8, 14) == tiles("SCORE 0840200 #1").substr(0, 14));
    nes.lb_restore_tiles();
    CHECK_FALSE(nes.lb.patched);
    std::remove(path.c_str());
}

TEST_CASE("leaderboards off, or a cheating run: nothing is logged", "[leaderboards]") {
    Nes nes;
    nes.cpu.nes = &nes;
    nes.lb.enabled = false;
    nes.lb_new_run();
    CHECK_FALSE(nes.lb.run);
    nes.lb.enabled = true;
    nes.is_cheating = true;
    nes.lb_new_run();
    CHECK_FALSE(nes.lb.run);
    nes.cpu.lb_round_start();
    nes.cpu.lb_final_boss();
    CHECK_FALSE(nes.lb.finished);
    CHECK_FALSE(nes.cpu.lb_credits_idle());
    CHECK_FALSE(nes.cpu.lb_screen_frame());
}

// ---- the game side, running the game

TEST_CASE("a normal new game is timed from round 1", "[game][leaderboards]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.leaderboards_path = "/nonexistent-dir/leaderboards.ini";
        Result r = run(g, start_game(1500), o);
        CHECK_FALSE(r.is_cheating);
        CHECK(r.nes->lb.run);
        CHECK(r.nes->lb.started);
        CHECK(r.nes->lb.start_frame >= 800);   // when ROUND 1 START is left (Start at frame 800)
        CHECK(r.nes->lb.start_frame < 900);
    }
}

TEST_CASE("after the credits, A goes on to the result screen", "[game][leaderboards]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.ending = true;                       // straight to the ending story
        o.leaderboards_path = "/nonexistent-dir/leaderboards.ini";
        Script s;
        s.act = [](Nes& n) {                   // once the title is done: as if the final boss had just been beaten
            if (n.frame_count != 300) return;
            n.lb.finished = true;
            n.lb.result = LbEntry{uint32_t(2 * 3600 * 60.0988139 + 3 * 60 * 60.0988139 + 46 * 60.0988139 + 30), 840200, 1};
            n.lb.place = LbPlace{2, 0};
        };
        s.frames = 7500;
        s.presses.push_back({200, BTN_START});
        for (uint64_t f = 300; f < 7400; f += 40) s.presses.push_back({f, BTN_A});   // through the story pages and the credits
        bool showing_early = false;
        s.watch = [&](const Nes& n) { if (n.lb.showing && n.frame_count < 5000) showing_early = true; };
        Result r = run(g, s, o);
        const Nes& n = *r.nes;
        INFO((g == Game::Proto ? "JP" : "US"));
        CHECK_FALSE(showing_early);            // not while the credits are still running
        REQUIRE(n.lb.showing);
        CHECK(r.ram[ScreenId] == 6);
        auto row = [&](int rr, int from, int len) {
            std::string t;
            for (int i = 0; i < len; i++) t += char(n.vram[n.nt_index(uint16_t(0x2000 + rr * 32 + from + i))]);
            return t;
        };
        auto tiles = [](const std::string& text) {
            std::string t;
            for (char ch : text) t += char(ch >= '0' && ch <= '9' ? 0x04 + ch - '0' : ch >= 'A' && ch <= 'Z' ? 0x0E + ch - 'A' : ch == ':' ? 0xF0 : ch == '#' ? 0xF1 : 0x01);
            return t;
        };
        CHECK(row(17, 8, 16) == tiles("TIME 02:03:46 #2"));
        CHECK(row(19, 8, 13) == tiles("SCORE 0840200"));
        for (int i = 0; i < 64; i++) CHECK(n.oam[i * 4] >= 0xEF);   // no menu cursor
    }
}

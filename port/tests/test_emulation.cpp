// The translated game: the original game must keep running exactly as before. A scripted run's RAM
// is compared with values recorded in tests/golden.txt (the translation, the CPU helpers, the
// MMC1, the scheduler hooks...), and with itself.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "ram_symbols.h"
#include "support.h"

using namespace mptest;

static const char* name(Game g) { return g == Game::Proto ? "proto" : "us"; }

TEST_CASE("the original game reaches round 1", "[game]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        INFO(name(g));
        Result r = run(g, start_game());
        CHECK(r.frames == 1500);
        CHECK(r.ram[GameState] == 5);          // in game
        CHECK(r.ram[RoundNumber] == 0);
        CHECK(r.ram[PlayerHealth] == 0x0C);
        CHECK_FALSE(r.is_cheating);
    }
}

TEST_CASE("the original game plays the same as recorded", "[game][golden]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        INFO(name(g));
        Result start = run(g, start_game());
        CHECK(start.ram_hash == golden(std::string(name(g)) + ".start_game", start.ram_hash));
        Result play = run(g, play_round1());
        CHECK(play.ram_hash == golden(std::string(name(g)) + ".play_round1", play.ram_hash));
    }
}

TEST_CASE("a run is repeatable", "[game]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        INFO(name(g));
        CHECK(run(g, play_round1()).ram_hash == run(g, play_round1()).ram_hash);
    }
}

TEST_CASE("the game makes sound", "[game]") {
    REQUIRE_ROM(Game::US);
    Options o;
    o.collect_audio = true;
    Result r = run(Game::US, start_game(1000), o);
    CHECK(r.audio_rms > 100);                  // the title music (about 2700 RMS)
}

TEST_CASE("Reset goes back to the title screen", "[game]") {
    REQUIRE_ROM(Game::US);
    // the game started, then Reset at frame 1200 (as F10): the title comes back
    Script s = start_game(1600);
    Result plain = run(Game::US, s);
    auto nes = std::make_unique<Nes>();
    // (run() has no reset input; do the same by hand)
    std::string err;
    REQUIRE(nes->load(rom_path(Game::US), Game::US, err));
    nes->reset();
    static Script script; script = s;
    static uint64_t reset_at; reset_at = 1200;
    nes->on_frame = [](Nes& n) {
        n.buttons = 0;
        for (auto& p : script.presses) if (n.frame_count >= p.frame && n.frame_count < p.frame + 8) n.buttons |= p.buttons;
        if (n.frame_count == reset_at) n.reset_requested = true;
        return n.frame_count < script.frames;
    };
    nes->run();
    CHECK(plain.ram[GameState] == 5);
    CHECK(nes->cpu.ram[GameState] != 5);       // back on the title (or on its way there)
}

TEST_CASE("a game told to stop stops, even while a speech bubble waits for A", "[game]") {
    // the bubble's wait loop never leaves the fixed bank, where Nes::run doesn't look at quit
    REQUIRE_ROM(Game::US);
    Script s = enter_first_boss_door();
    s.presses.erase(std::remove_if(s.presses.begin(), s.presses.end(), [](const Press& p) { return p.frame >= 1380; }), s.presses.end());
    s.frames = 1700;                           // the bubble is up from about frame 1400
    uint8_t bubble = 0;
    s.watch = [&](const Nes& n) { if (n.frame_count == 1650) bubble = n.cpu.ram[GameState]; };
    Result r = run(Game::US, s);
    CHECK(r.frames == 1700);
    CHECK(bubble == 5);
}

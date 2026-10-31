// Widescreen drawing on the screens that aren't rooms: their margins repeat the screen's outer
// 32 px, with exceptions (the title's mouth stays black; the password and game over screens'
// right-hand skeleton becomes the floor). The frames are compared with recorded hashes, so a
// change in how they're drawn shows up (the game over screen's skeleton came back once).
#include <catch2/catch_test_macros.hpp>
#include <string>
#include "ram_symbols.h"
#include "settings.h"
#include "support.h"

using namespace mptest;

namespace {
const char* name(Game g) { return g == Game::Proto ? "proto" : "us"; }

// Runs the script in widescreen and returns the hash of the frame drawn `after` frames into
// ScreenId `screen` (0 if it never got there).
uint32_t screen_frame_hash(Game g, Script s, uint8_t screen, uint64_t after) {
    uint64_t entered = 0;
    uint32_t hash = 0;
    s.watch = [&](const Nes& n) {
        if (!entered && n.cpu.ram[ScreenId] == screen) entered = n.frame_count;
        if (entered && n.frame_count == entered + after)
            hash = fnv1a(reinterpret_cast<const uint8_t*>(n.frame), size_t(n.frame_w) * 240 * 4);
    };
    Options o;
    o.wide_margin = wide_margin(false);
    o.wide_dim = true;
    run(g, s, o);
    return hash;
}
}  // namespace

TEST_CASE("widescreen: the title screen", "[game][golden][widescreen]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Script s;
        s.frames = 190;
        const uint32_t h = screen_frame_hash(g, s, 1, 150);   // (the monster parade under way)
        REQUIRE(h != 0);
        CHECK(h == golden(std::string(name(g)) + ".widescreen.title", h));
    }
}

TEST_CASE("widescreen: the password screen", "[game][golden][widescreen]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Script s;
        s.frames = 400;
        s.presses = {{150, BTN_DOWN}, {200, BTN_START}};   // PASSWORD on the title
        const uint32_t h = screen_frame_hash(g, s, 2, 60);
        REQUIRE(h != 0);
        CHECK(h == golden(std::string(name(g)) + ".widescreen.password", h));
    }
}

TEST_CASE("widescreen: the game over screen", "[game][golden][widescreen]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        // round 1 with 1 health, walking right into the first enemy
        Script s = play_round1(2000);                   // (game over at about frame 1730)
        s.pokes.push_back({1000, PlayerHealth, 1});
        const uint32_t h = screen_frame_hash(g, s, 6, 60);
        REQUIRE(h != 0);
        CHECK(h == golden(std::string(name(g)) + ".widescreen.game_over", h));
    }
}


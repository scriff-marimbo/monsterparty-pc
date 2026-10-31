// Options that must not change how the game plays: the RAM after a run is exactly the original's.
#include <catch2/catch_test_macros.hpp>
#include "ram_symbols.h"
#include "settings.h"
#include "support.h"

using namespace mptest;

TEST_CASE("widescreen is drawing only", "[game][options]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const uint32_t plain = run(g, play_round1()).ram_hash;
        for (bool aspect_8_7 : {false, true}) {
            Options o;
            o.wide_margin = wide_margin(aspect_8_7);
            Result r = run(g, play_round1(), o);
            CHECK(r.ram_hash == plain);
            CHECK(r.nes->frame_w == 256 + 2 * o.wide_margin);
        }
    }
}

TEST_CASE("listening to the music for MIDI doesn't change the game", "[game][options]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.record_midi = true;
        CHECK(run(g, play_round1(), o).ram_hash == run(g, play_round1()).ram_hash);
    }
}

TEST_CASE("difficulty Normal is the original game", "[game][options]") {
    REQUIRE_ROM(Game::US);
    Options o;
    o.difficulty = Difficulty::Normal;
    CHECK(run(Game::US, play_round1(), o).ram_hash == run(Game::US, play_round1()).ram_hash);
}

TEST_CASE("Casual and Nasty do change the game", "[game][options]") {
    REQUIRE_ROM(Game::US);
    const uint32_t normal = run(Game::US, play_round1()).ram_hash;
    for (Difficulty d : {Difficulty::Casual, Difficulty::Nasty}) {
        Options o;
        o.difficulty = d;
        Result r = run(Game::US, play_round1(), o);
        CHECK(r.ram[GameState] == 5);
        CHECK(r.ram_hash != normal);
    }
}

TEST_CASE("the remix run-time part does nothing when nothing is remixed", "[game][options][remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.remix_runtime = true;
        CHECK(run(g, play_round1(), o).ram_hash == run(g, play_round1()).ram_hash);
    }
}

TEST_CASE("the Bosses options don't change round 1", "[game][options][bosses]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.totem_pole = o.fix_round7 = o.totem_finished = true;
        CHECK(run(g, play_round1(), o).ram_hash == run(g, play_round1()).ram_hash);
    }
}

TEST_CASE("trainers all off is the original game", "[game][options][trainers]") {
    REQUIRE_ROM(Game::US);
    Options o;
    o.trainers = Trainers{};
    Result r = run(Game::US, play_round1(), o);
    CHECK(r.ram_hash == run(Game::US, play_round1()).ram_hash);
    CHECK_FALSE(r.is_cheating);
}

TEST_CASE("leaderboards on don't change the game", "[game][options][leaderboards]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.leaderboards_path = "/nonexistent-dir/leaderboards.ini";   // never written: no game is finished here
        Result r = run(g, play_round1(), o);
        CHECK(r.ram_hash == run(g, play_round1()).ram_hash);
        CHECK(r.nes->lb.run);
    }
}

TEST_CASE("No music silences the music and leaves the game alone", "[game][options]") {
    REQUIRE_ROM(Game::US);
    Options on, off;
    on.collect_audio = off.collect_audio = true;
    off.music_off = true;
    Result a = run(Game::US, start_game(600), on), b = run(Game::US, start_game(600), off);
    CHECK(a.audio_rms > 0);                       // the title music
    CHECK(b.audio_rms < a.audio_rms * 0.1);
    CHECK(a.ram_hash == b.ram_hash);              // only the sound differs
}

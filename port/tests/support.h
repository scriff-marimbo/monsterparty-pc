// Monster Party port: unit test helpers. A game run here is the same as the headless mode in
// main.cpp: load a ROM, apply the options, reset, and run the translated code for a number of
// frames with a scripted controller (buttons held for 8 frames from a given frame) and RAM pokes.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "nes.h"
#include "remix.h"

namespace mptest {

// The ROMs, from the repository's roms/ (MP_TEST_ROM_DIR). Tests that need a ROM that isn't there
// are skipped.
std::string rom_path(Game g);
bool have_rom(Game g);
#define REQUIRE_ROM(g) do { if (!mptest::have_rom(g)) SKIP("ROM not in roms/: " << rom_info(g).file); } while (0)

// The 128 KB PRG of a ROM, as Nes::load reads it (for the data-editing functions).
std::vector<uint8_t> load_prg(Game g);

struct Press { uint64_t frame; uint8_t buttons; };
struct Poke { uint64_t frame; uint16_t addr; uint8_t value; };
struct Script {
    uint64_t frames = 0;
    std::vector<Press> presses;
    std::vector<Poke> pokes;
    std::function<void(const Nes&)> watch;     // called at the end of every frame, to look at RAM
    std::function<void(Nes&)> act;             // called at the end of every frame, may change things
};
// Start pressed at frames 200, 500 and 800: title -> intro -> round 1 (GameState 5).
Script start_game(uint64_t frames = 1500);
// start_game, then a minute or so of play in round 1: walking right, swinging the bat, jumping.
Script play_round1(uint64_t frames = 3000);
// start_game, then through round 1's first boss door (as --enter-boss-door does).
Script enter_first_boss_door();

// What to set up before a run (everything off = the original game).
struct Options {
    Difficulty difficulty = Difficulty::Normal;
    int wide_margin = 0;                       // widescreen (drawing only)
    bool mode3d = false;                       // 3D mode's scene (drawing only; as main: wide_margin if set, else a 16 px one)
    bool wide_dim = false;                     // widescreen: dim the extra sides
    bool record_midi = false;                  // MIDI hooks listening (read-only)
    bool remix = false; uint32_t remix_seed = 0;
    bool totem_pole = false, fix_round7 = false, totem_finished = false;
    bool music_off = false;                    // "No music"
    bool ending = false;                       // --ending
    int boss_door = -1;                        // --boss-door
    bool remix_runtime = false;                // the run-time part on with nothing remixed
    Trainers trainers;
    bool collect_audio = false;
    std::string leaderboards_path;             // non-empty: leaderboards on, logging to this file (US, Normal, no remix)
    Language language = Language::Default;     // the Language setting (needs the other game's ROM for its font)
};

struct Result {
    uint32_t ram_hash = 0;                     // FNV-1a of the 2 KB of RAM (as --dump prints it)
    uint8_t ram[0x800] = {};
    uint64_t frames = 0;
    bool is_cheating = false;
    double audio_rms = 0;
    int8_t totem_obj = -1;
    std::unique_ptr<Nes> nes;                  // the machine as it ended (frame, palette, ...)
    std::unique_ptr<Scene3D> scene;            // with mode3d: the last frame's 3D scene
};
Result run(Game g, const Script& script, const Options& opt = {});

uint32_t fnv1a(const uint8_t* p, size_t n);

// Expected values kept in tests/golden.txt ("name = 0x12345678"). With MP_RECORD_GOLDEN=1 in the
// environment, the actual value is written there instead (when the port's behaviour has changed
// on purpose).
uint32_t golden(const std::string& name, uint32_t actual);
bool recording_golden();

}  // namespace mptest

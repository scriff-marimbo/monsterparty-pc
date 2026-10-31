// Monster Party native port: Remix Mode (not in the original game).
//
// Shuffles parts of the game once per session. Most of it edits the loaded ROM data in memory (the
// ROM file is untouched):
//   - Boss doors: within each round, which door leads to which boss (or to an empty boss room) is
//     shuffled. All of a round's boss doors lead to the round's arena room; the door's flags byte
//     says which boss (bits 4-6), so the flags bytes are shuffled among the round's boss doors.
//   - Enemy spawns: each spawn entry's enemy type is re-picked from enemies of the same kind
//     (walkers, fliers, swimmers). Hazards and set pieces (drips, stalactites, blocks,
//     constellations) stay as they are.
//   - Round 6's doors: the round is one maze of doors that warp to each other (27 pairs, a one-way door
//     and the boss door). They are paired up again and the boss door moves to another door, but only
//     into a layout where the boss door and then the exit (the lower right corner) can be reached
//     from the start, with no place where you get stuck (check_round6_doors).
//
// Remix Mode mixes rounds ("cross"): it also takes bosses and enemies from other rounds. The game keeps enemy and
// boss graphics, animation frames and sprite colours per round and only has the current round's
// loaded, so this needs help at run time (RemixRuntime, hooks in translate.py REMIX_HOOKS):
//   - Enemies: each kind's types are dealt out to the rounds again (each round keeps its number of
//     types of each kind, now from other rounds). An enemy from another round
//     ("foreign") runs with RoundNumber set to its home round while its task runs (the game picks
//     per-round behaviour and metasprites by RoundNumber), so do the tasks it starts (shots,
//     drops). When it's drawn, DrawObject uses its home round's metasprites, and the renderer
//     draws its sprite pieces with the home round's sprite CHR bank and sprite palettes.
//   - Bosses: boss slot s (1-3, and 0 = empty room) of each round is shuffled with slot s of the
//     other rounds, keeping the slot number (the game marks a boss beaten by its slot). The door
//     leads to the boss's own round's arena (8 + home round), which brings that arena's graphics,
//     palette and music; in there, the boss's tasks run with RoundNumber = the arena's round, and
//     BossId / the music / the speech bubble use the arena's round too. The status bar, the
//     player and the round's progress keep the real round.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class Game;

struct RemixRuntime {
    bool cross = false;                // bosses and enemies from other rounds (Remix Mode; --boss-door)
    int8_t type_home[64];              // enemy type -> its original round (0-7), -1 = unknown
    // per RoomId (0-7: the rounds' main rooms, 8-15: their boss arenas)
    uint8_t sprite_bank[16] = {};      // sprite CHR bank
    uint8_t sprite_pal[16][12] = {};   // the 4 sprite palettes (3 colours each)
    uint8_t bg_bank[16] = {};          // background CHR bank
    uint8_t bg_pal[16][13] = {};       // background colour, then the 4 background palettes (3 colours each)
    struct Layout { uint8_t bank; uint16_t columns, screens, attrs, metatiles; };
    Layout layout[16] = {};            // where the room's map data is (as Room_Init sets it up)
    int8_t door_id[8][8] = {};         // per round, per boss slot: the boss door's DoorId (flags low nibble)
    bool has_slot[8][8] = {};          // per round: which boss slots (0 = empty room) it has doors for
    bool tables = false;               // the tables above have been read
    // at run time
    int8_t slot_home[12];              // per task slot: home round of a foreign task, -1 = none
    int saved_round = -1;              // the real RoundNumber while a foreign task runs (-1 = not overridden)
    int8_t draw_home = -1;             // DrawObject: home round of the object being drawn, -1 = not foreign
    int8_t piece_home[64];             // per OAM slot: draw_home when the piece was written
    uint32_t piece_gen[64] = {};
    int8_t oam_home[64];               // latched with OAM DMA, for the renderer
    // Home values are RoomIds: 0-7 a round's main room, 8-15 a round's arena (graphics from that
    // room; the round for the game's code is home & 7).
    int8_t force_home = -1;            // StartTask gives the new task this home (the Totem Pole's shots)
    bool force_custom = false;         // ... and has it drawn as the port's own sprite (custom_sprites.h)
    bool slot_custom[12] = {};         // per task slot: drawn as the port's Totem Pole head
    bool draw_custom = false;          // DrawObject: the object being drawn is drawn by the port instead
    uint8_t obj_custom_kind[16] = {};  // per object, when drawn by the port: 1 = the head shot, 2 = the Totem Pole's 2nd frame
    int8_t totem_obj = -1;             // the Totem Pole's object (from his handler)
    uint64_t totem_seen = 0;           // ... in this frame (frame_count)
    int8_t piece_custom[64];           // per OAM slot: the object it's part of, if drawn as the head; -1 = no
    int8_t oam_custom[64];             // latched with OAM DMA, for the renderer
    int16_t custom_last_x[16] = {};    // per object: the head's centre X last frame (which way it flies)
    bool custom_flip[16] = {};         // per object: the head faces left
    // where each object's sprite pieces were drawn (for the Totem Pole's bubbles)
    int8_t draw_obj = -1;              // DrawObject: the object being drawn, -1 = not an object
    uint8_t obj_top[16] = {};          // per object: the top of its pieces (OAM Y)
    uint32_t obj_top_gen[16] = {};     // ... in which OAM frame
    // the 'finished' Totem Pole (Cpu::totem_fire): head shots and a 2nd animation frame
    bool totem_attack = false;
    bool totem_returning = false;
    uint8_t totem_saved[4] = {};       // A, X, Y, the object's ObjY
    RemixRuntime() { reset(); }
    void reset();
};

// The launcher's "Bosses" options, with or without Remix (call before apply_remix): edits prg (the
// 128 KB PRG ROM as loaded). totem_pole (prototype only): the unused Totem Pole gets round 7's 3rd
// boss door. fix_round7: round 7 needs all 3 of its bosses (fixes the key-steal bug: beating the
// 3rd boss after getting the key takes the key away). Returns what changed ("" = nothing).
std::string apply_bosses(std::vector<uint8_t>& prg, Game game, bool totem_pole, bool fix_round7);

// Applies the remix for `seed` to prg and fills rt. cross = mix rounds (Remix Mode always does;
// without it, everything stays within its round). Returns a short description of what changed
// (one line per round).
std::string apply_remix(std::vector<uint8_t>& prg, Game game, uint32_t seed, bool cross, RemixRuntime& rt);

// Round 6's door maze, read back from prg (original or remixed): whether it can be beaten.
struct MazeCheck {
    bool valid = false;                // the data is the maze we know (false: problem says what isn't)
    bool beatable = false;             // boss door and exit reachable, no place with no way on
    int hops_to_boss = 0;              // door hops: start -> boss door, boss door -> exit (fewest)
    int hops_to_exit = 0;
    int boss = -1, boss_row = 0, boss_col = 0;
    std::vector<int> to;               // per door (in the door table's order): the door it leads to, -1 = the boss arena
    std::string problem;
};
MazeCheck check_round6_doors(const std::vector<uint8_t>& prg, Game game);

// Switches the run-time part on without remixing anything (reads its tables from prg if needed).
// With nothing foreign it changes nothing; the Totem Pole's bubbles (from round 1's arena) need it.
void enable_remix_runtime(const std::vector<uint8_t>& prg, Game game, RemixRuntime& rt);

// Debug (--boss-door): every round's first boss door leads to `boss` (BossId $00-$1F, in its own
// round's arena), and the run-time part is switched on so another round's arena works.
std::string debug_boss_door(std::vector<uint8_t>& prg, Game game, uint8_t boss, RemixRuntime& rt);

// The kind Remix Mode swaps an enemy type within: 0 hazards, 1 walker,
// 2 flier, 3 swimmer, 4 fixed (creatures that don't really move)
int remix_enemy_kind(Game game, uint8_t type);

uint32_t new_remix_seed();                         // a random seed (from the clock and SDL's RNG)
std::string remix_seed_text(uint32_t seed);        // "1A2B-3C4D"
bool parse_remix_seed(const std::string& text, uint32_t& seed);   // 8 hex digits, a dash or spaces allowed

// Monster Party native port: the hardware the game expects (PPU, MMC1, controllers), in software.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "cpu.h"
#include "audio.h"
#include "music.h"
#include "remix.h"
#include "leaderboard.h"
#include "language.h"

enum Button : uint8_t { BTN_A = 0x01, BTN_B = 0x02, BTN_SELECT = 0x04, BTN_START = 0x08,
                        BTN_UP = 0x10, BTN_DOWN = 0x20, BTN_LEFT = 0x40, BTN_RIGHT = 0x80 };

// PPU registers that affect how a part of the screen is drawn
struct RasterState {
    uint8_t ctrl = 0, mask = 0;
    uint8_t scroll_x = 0, scroll_y = 0;
    uint8_t chr0 = 0, chr1 = 0;      // 4 KB CHR banks at PPU $0000 / $1000
};

// The two ROMs the port has translated code for. Each is checked by the CRC32 of its PRG + CHR
// (the file without its 16-byte header), so only those exact dumps are accepted.
enum class Game { US, Proto };
struct RomInfo { Game game; const char* file; const char* title; uint32_t crc; };
const RomInfo& rom_info(Game g);   // file name in roms/, window title, CRC
// Reads and checks a ROM file without loading it (for the launcher). err says why it's unusable.
bool check_rom(const std::string& path, Game g, std::string& err);
// Reads a checked ROM file's PRG and/or CHR (either may be null).
bool read_rom_file(const std::string& path, Game g, std::vector<uint8_t>* prg, std::vector<uint8_t>* chr, std::string& err);

// Not in the original game: the port's difficulty setting (see Cpu::diff_* in nes.cpp).
// Normal is the game exactly as released.
enum class Difficulty : uint8_t { Casual, Normal, Nasty };

// Not in the original game: the launcher's Trainers (cheats, like Game Genie codes; see Cpu::trainer_*
// in nes.cpp and translate.py TRAINER_HOOKS). All off = the game as released.
struct Trainers {
    bool infinite_life = false;   // hits don't take health
    bool full_life = false;       // a new game (and a continue) starts with full health
    int start_round = 0;          // a new game starts in this round (2-8); 0 = round 1 as usual
    bool super_jump = false;      // the player's jump goes twice as high
    bool moon_jump = false;       // much higher still, and the player rises and falls at half speed (wins over super_jump)
    bool always_bert = false;     // start as Bert (Baryuu in the prototype), and Bert's time never runs out
    bool mark_doors = false;      // round 6's doors the player has entered this run are drawn marked (graphics/r6_markeddoor.png)
    bool any() const { return infinite_life || full_life || start_round || super_jump || moon_jump || always_bert || mark_doors; }
};

// Mark Round 6 Doors: the door maze's room (RoomId 5 in both versions; 56 doors, see
// tools/formats/round6_doors.md)
constexpr uint8_t ROUND6_ROOM = 5;

// 3D mode (drawing only; render3d.cpp draws it): what the renderer hands over each frame. Screen
// coordinates are the widescreen frame's (x 0..w-1, y 0..239); the playfield is rows 0..split-1.
struct Scene3D {
    static constexpr int MAX_W = 512, CELL_COLS = MAX_W / 8 + 2;
    int w = 256;                       // frame width (the widescreen frame)
    int split = 240;                   // status bar from this row down
    bool in_room = false;              // a room is showing: blocks in front of a flat back wall; else just a flat picture
    // What to show (set by main, kept): the whole frame (widescreen on) or only the game's own 256
    // columns: play_x0.. for the playfield, bar_x0.. for the status bar and other screens.
    bool wide = false;
    int play_x0 = 0, bar_x0 = 0;
    uint32_t bg[MAX_W * 240];          // the background alone (sprites left out)
    uint32_t spr[MAX_W * 240];         // the sprites alone (0 = none; alpha 0xFF where drawn)
    // Room cells (8 x 8) covering the frame: cell c, row ty (0-23) is at screen x cell_x0 + 8c, y 8ty.
    // type = the game's collision type of its metatile (the table at RAM $68): 0 nothing, 4 solid,
    // 2 platform, 6 door, 7 exit, 1/3/5 others.
    // tx0 = the room tile column of cell 0 (even columns start a 16 x 16 metatile)
    int cell_x0 = 0, cells = 0, tx0 = 0;
    uint8_t type[24][CELL_COLS];
    uint32_t backdrop = 0xFF000000u;
};

struct Nes {
    Cpu cpu;
    std::vector<uint8_t> prg, chr;   // from the ROM file
    Game game = Game::US;            // which translated code runs

    // ---- MMC1
    uint8_t mmc1_shift = 0x10, mmc1_control = 0x0C, mmc1_chr0 = 0, mmc1_chr1 = 0, mmc1_prg = 0;
    void mmc1_write(uint16_t addr, uint8_t v);
    void mmc1_apply();

    // ---- PPU
    uint8_t ppu_ctrl = 0, ppu_mask = 0, oam_addr = 0;
    uint8_t scroll_x = 0, scroll_y = 0;
    uint16_t vaddr = 0; bool latch = false; uint8_t read_buf = 0;
    bool vblank_flag = true;
    uint8_t oam[256] = {}, vram[0x800] = {}, palette[32] = {};
    uint8_t ppu_read(uint16_t reg);
    void ppu_write(uint16_t reg, uint8_t v);
    uint8_t ppu_mem(uint16_t a);
    void ppu_mem_write(uint16_t a, uint8_t v);
    uint16_t nt_index(uint16_t a) const;
    RasterState current() const { return {ppu_ctrl, ppu_mask, scroll_x, scroll_y, mmc1_chr0, mmc1_chr1}; }

    // frame: top part (playfield) and, after the sprite-0 split, the status bar.
    // frame is frame_w x 240 (row stride frame_w): 256 normally, wider in widescreen mode.
    static constexpr int MAX_FRAME_W = 512;
    bool split_seen = false; RasterState top_state;
    int frame_w = 256;
    uint32_t frame[MAX_FRAME_W * 240] = {};
    void render_frame();
    uint32_t rgb[64];

    // ---- widescreen (experimental). wide_margin extra pixels are shown on each side of the
    // game's 256. In a room they're drawn from the room's own map data (not the nametables,
    // which only hold the visible screen plus the next column), and sprites the game culled as
    // off screen are drawn where they really are. Elsewhere the screen's edges are extended.
    int wide_margin = 0;                       // set before run(); 0 = off, max (MAX_FRAME_W - 256) / 2
    struct WidePiece { uint8_t y, x; int8_t hi; uint32_t gen; };
    WidePiece wide_piece[64] = {};             // per RAM OAM slot, from the $C597 hook
    struct WideOam { bool valid; int8_t hi; uint8_t y; };
    WideOam wide_oam[64] = {};                 // latched with OAM DMA, like oam[]
    uint32_t oam_gen = 1;
    struct RoomView {                          // the room as of the last OAM DMA (same frame as oam[] and the scroll)
        bool valid = false;
        int cam_x = 0, width = 256;            // camera's left edge in the room, room width (pixels)
        uint8_t door = 0, bank = 0;
        uint8_t id = 0;                        // RoomId
        uint16_t door_list = 0;                // the room's door list (bank 2; RAM $7B), for Mark Round 6 Doors
        uint16_t columns = 0, screens = 0, attrs = 0, metatiles = 0;
        uint16_t coll = 0;                     // collision type per metatile (RAM $68: bank 7, 256 bytes)
        // what the room on screen is (BossRoomFlag bit 7, and Remix Mode's other-round arena: see
        // remix_foreign_arena). Kept through the fade out of a door, a death or a round's end: the
        // door has already set BossRoomFlag (and the death GameMode) for what comes next
        bool boss = false, foreign = false;
        int8_t real_round = 0, arena_round = 0;
    } room;
    void latch_wide_state();                   // at OAM DMA
    // tile and palette of the room map at tile column tx (room coordinates), tile row ty (0-23)
    bool room_tile(int tx, int ty, uint8_t& tile, uint8_t& pal) const { return room_tile(room, tx, ty, tile, pal); }
    bool room_tile(const RoomView& r, int tx, int ty, uint8_t& tile, uint8_t& pal) const;
    // the metatile at tile column tx, tile row ty (room coordinates), -1 outside the room
    int room_metatile(const RoomView& r, int tx, int ty) const;
    // 3D mode (drawing only): set to a Scene3D to have render_frame fill it every frame (main sets
    // wide_margin to the most it can be, so there's room map around the game's screen)
    Scene3D* scene3d = nullptr;
    void build_scene3d(int view_x, int play_x0, bool in_room, int split_line);
    // widescreen (main sets it with widescreen): dim the extra playfield either side of the game's
    // own screen while playing, outside boss rooms (so it's clear where enemies appear and vanish);
    // drawing only
    bool wide_dim = false;
    // Remix Mode, mixing rounds: in another round's arena, the round the player is really in and
    // the arena's round (else false)
    bool remix_foreign_arena(int& real_round, int& arena_round) const;

    // ---- APU (audio.h). frame_hook finishes each frame's audio;
    // on_frame then hands audio_out to the sound device (or a WAV file).
    NesAudio audio;
    MusicMidi music;                           // the music as MIDI (off unless a SoundFont is loaded or recording)
    const std::vector<int16_t>* audio_out = nullptr;

    // ---- Remix Mode (remix.h): the data was edited at load; this is the run-time part (mixing rounds)
    RemixRuntime remix;

    // ---- the Language setting (language.h): set up after load by apply_language; drawing only
    LangRuntime lang;

    // ---- difficulty
    Difficulty difficulty = Difficulty::Normal;
    uint32_t diff_rng = 0x9E3779B9;             // our own random numbers, so the game's RNG is untouched
    uint8_t diff_health_before = 0;             // PlayerHealth when the current hit started
    uint32_t diff_random(uint32_t n);           // 0 .. n-1

    // ---- trainers, and whether this run is "cheating"
    Trainers trainers;
    bool music_off = false;                     // "No music": the music channels are written silent (sound effects play as usual)
    bool debug_ending = false;                  // --ending: a new game goes straight to the ending story (screen 9) instead of the intro
    bool debug_used = false;                    // a command-line debug option changes the game (--boss-door, --poke, ...)
    // is_cheating: this game run uses a password, a trainer or a debug option. Set when a game
    // starts from the title screen (and true from power-on if a trainer or debug option is on).
    bool is_cheating = false;
    bool session_cheats() const { return trainers.any() || debug_used; }
    bool moon_hold = false;                     // Moon Jump: this frame repeats the jump table step
    // Mark Round 6 Doors: the doors of round 6's room entered this run (bit n = entry n of the room's
    // door list). Cleared when a game starts from the title; kept through continues. Drawing only.
    uint64_t doors_entered = 0;

    // ---- leaderboards (leaderboard.h; hooks Cpu::lb_*, translate.py LB_HOOKS). A new game from the
    // title with is_cheating false is timed from the end of ROUND 1 START; when the final boss dies
    // its time and HIGH score are logged, and after the credits a button shows them on a screen of
    // the game's own (the GAME OVER screen's room and skeletons, in the same nametable).
    struct LbState {
        // set up by main before run() (all off = the game as released, and the hooks do nothing)
        bool enabled = false;                   // the launcher's setting, and nothing about this session rules a ranked game out
        LbCategory category;                    // which leaderboard this game's results go in
        std::string path;                       // the leaderboards file; empty = keep nothing
        // the game in progress
        bool run = false;                       // a new game from the title, enabled, is_cheating false
        bool started = false;                   // ROUND 1 START is over: the clock is running
        uint64_t start_frame = 0;               // frame_count then
        bool killed = false;                    // the final boss was just defeated ...
        uint64_t kill_frame = 0;                // ... at this frame
        uint32_t best = 0;                      // the run's own best score so far (a continue clears the score, not this)
        bool finished = false;                  // the final boss is dead and the result is logged
        LbEntry result;                         // time (frames) and score
        LbPlace place;                          // where they landed (0 = not in the top 10)
        bool log_failed = false;                // the file couldn't be written
        int idle = 0;                           // frames the credits' last page has been up
        bool showing = false;                   // the result screen is up (the GAME OVER screen's code runs it)
        bool patched = false;                   // the screen's own ':' and '#' tiles are in the CHR copy
        uint8_t saved_tiles[2][16] = {};        // what they replaced
    } lb;
    void lb_new_run();                          // a game starts from the title
    void lb_draw_screen();                      // writes the result screen's text into the nametable
    void lb_restore_tiles();                    // puts the CHR back as the ROM had it

    // ---- controllers
    uint8_t buttons = 0, pad_shift = 0; bool pad_strobe = false;

    // ---- frame loop glue
    uint64_t frame_count = 0;
    bool (*on_frame)(Nes&) = nullptr;   // set by main: present the frame, poll input; false = quit
    bool quit = false;
    bool reset_requested = false;       // set by main (F10); handled at the end of the frame
    void soft_reset();                  // like pressing the console's Reset button

    bool load(const std::string& path, Game g, std::string& err);   // checks the CRC, sets game
    void reset();
    void run();   // runs until quit
};

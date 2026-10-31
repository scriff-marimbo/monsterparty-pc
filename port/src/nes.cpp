// Monster Party native port: PPU, MMC1, controllers and the glue between the translated code and the frame loop.
#include "nes.h"
#include "palette.h"
#include "platform.h"
#include "custom_sprites.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include "ram_symbols.h"

// ---------------------------------------------------------------- ROM
static const RomInfo ROMS[] = {
    {Game::US, "Monster Party (USA).nes", "Monster Party", 0x02B9E7C2},
    {Game::Proto, "Parody World - Monster Party (Japan) (Proto).nes", "Parody World - Monster Party", 0x21E28F50},
};
const RomInfo& rom_info(Game g) { return ROMS[g == Game::Proto ? 1 : 0]; }

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

// reads the file and checks it's the expected dump; prg/chr get the contents
static bool read_rom(const std::string& path, Game g, std::vector<uint8_t>* prg, std::vector<uint8_t>* chr, std::string& err) {
    const RomInfo& info = rom_info(g);
    std::vector<uint8_t> d;
    if (!mp_read_file(path, d)) { err = "can't open " + path; return false; }
    const size_t size = 0x20000 + 0x20000;
    if (d.size() < 16 || std::memcmp(d.data(), "NES\x1a", 4) != 0) { err = path + " isn't an NES ROM file (iNES)"; return false; }
    if (d.size() != 16 + size) { err = path + " has the wrong size for " + info.file; return false; }
    uint32_t crc = crc32(d.data() + 16, size);
    if (crc != info.crc) {
        char b[64];
        std::snprintf(b, sizeof b, "CRC32 %08X, expected %08X", crc, info.crc);
        err = path + " isn't the expected " + info.file + " dump (" + b + ")";
        return false;
    }
    if (prg) prg->assign(d.begin() + 16, d.begin() + 16 + 0x20000);
    if (chr) chr->assign(d.begin() + 16 + 0x20000, d.end());
    return true;
}

bool check_rom(const std::string& path, Game g, std::string& err) { return read_rom(path, g, nullptr, nullptr, err); }
bool read_rom_file(const std::string& path, Game g, std::vector<uint8_t>* prg, std::vector<uint8_t>* chr, std::string& err) {
    return read_rom(path, g, prg, chr, err);
}

bool Nes::load(const std::string& path, Game g, std::string& err) {
    if (!read_rom(path, g, &prg, &chr, err)) return false;
    game = g;
    return true;
}

void Nes::reset() {
    diff_rng = 0x9E3779B9;
    default_palette(rgb);   // main may replace it with a palette file after reset()
    cpu = Cpu();
    cpu.nes = this;
    mmc1_control = 0x0C; mmc1_prg = 0; mmc1_apply();
    audio.reset();
    music.all_notes_off();
    // Remix Mode: the run-time state (not the remix itself, which lasts the whole session)
    remix.saved_round = -1; remix.draw_home = -1;
    for (auto& h : remix.slot_home) h = -1;
    remix.force_home = -1; remix.totem_returning = false;
    is_cheating = session_cheats();   // set again when a game starts from the title screen
    moon_hold = false;
    doors_entered = 0;
    lb_restore_tiles();
    lb = LbState{lb.enabled, lb.category, lb.path};
    lang.bubble_chr = lang.bubble_index = -1;   // (the Language set-up itself lasts the whole session)   // (a new game from the title starts the run)
}

// Reset button: RAM and CHR state survive (as on the console); the CPU restarts at the reset vector
// with the PPU and sound switched off, and the game's own reset code reinitialises everything else.
void Nes::soft_reset() {
    reset_requested = false;
    cpu.SP = uint8_t(cpu.SP - 3);
    cpu.I = true;
    ppu_ctrl = 0; ppu_mask = 0; latch = false; scroll_x = scroll_y = 0;
    mmc1_shift = 0x10; mmc1_control |= 0x0C; mmc1_apply();
    audio.reset();
    music.all_notes_off();
    lb_restore_tiles();
    lb = LbState{lb.enabled, lb.category, lb.path};
    lang.bubble_chr = lang.bubble_index = -1;   // (the Language set-up itself lasts the whole session)
}

// ---------------------------------------------------------------- CPU memory map
uint8_t Cpu::rd(uint16_t a) {
    if (a < 0x2000) return ram[a & 0x7FF];
    if (a < 0x4000) return nes->ppu_read(a & 7);
    if (a == 0x4016) { uint8_t v = nes->pad_shift & 1; if (!nes->pad_strobe) nes->pad_shift = 0x80 | nes->pad_shift >> 1; return v; }
    if (a < 0x8000) return a >= 0x6000 && nes->lang.active ? nes->lang.text[a - 0x6000] : 0;   // (Language: the story pages)
    if (a >= 0xC000) return nes->prg[0x1C000 + (a - 0xC000)];
    return nes->prg[prg_bank * 0x4000 + (a - 0x8000)];
}

void Cpu::wr(uint16_t a, uint8_t v) {
    if (a < 0x2000) { ram[a & 0x7FF] = v; return; }
    if (a < 0x4000) { nes->ppu_write(a & 7, v); return; }
    if (a == 0x4014) {
        for (int i = 0; i < 256; i++) nes->oam[uint8_t(nes->oam_addr + i)] = ram[(v << 8 | i) & 0x7FF];
        nes->latch_wide_state();
        cyc += 514;   // the OAM DMA stalls the CPU
        return;
    }
    if (a == 0x4016) { nes->pad_strobe = v & 1; if (nes->pad_strobe) nes->pad_shift = nes->buttons; return; }
    if (a < 0x4018) { nes->audio.write(a, v); return; }
    if (a >= 0x8000) nes->mmc1_write(a, v);
}

// ---------------------------------------------------------------- MMC1
void Nes::mmc1_write(uint16_t addr, uint8_t v) {
    if (v & 0x80) { mmc1_shift = 0x10; mmc1_control |= 0x0C; mmc1_apply(); return; }
    bool full = mmc1_shift & 1;
    mmc1_shift = uint8_t(mmc1_shift >> 1 | (v & 1) << 4);
    if (!full) return;
    uint8_t val = mmc1_shift & 0x1F;
    if (addr < 0xA000) mmc1_control = val;
    else if (addr < 0xC000) mmc1_chr0 = val;
    else if (addr < 0xE000) mmc1_chr1 = val;
    else mmc1_prg = val;
    mmc1_shift = 0x10;
    mmc1_apply();
}

void Nes::mmc1_apply() {
    // the game only uses PRG mode 3 (switchable $8000, last bank fixed at $C000) and 4 KB CHR mode
    cpu.prg_bank = mmc1_prg & 7;
}

// ---------------------------------------------------------------- PPU registers
uint16_t Nes::nt_index(uint16_t a) const {
    a &= 0x0FFF;
    switch (mmc1_control & 3) {
    case 0: return a & 0x3FF;                         // one screen, lower
    case 1: return 0x400 | (a & 0x3FF);               // one screen, upper
    case 2: return a & 0x7FF;                         // vertical mirroring
    default: return ((a >> 1) & 0x400) | (a & 0x3FF); // horizontal mirroring
    }
}

static inline uint16_t pal_index(uint16_t a) { a &= 0x1F; if ((a & 0x13) == 0x10) a &= 0x0F; return a; }

uint8_t Nes::ppu_mem(uint16_t a) {
    a &= 0x3FFF;
    if (a < 0x2000) { uint8_t bank = a < 0x1000 ? mmc1_chr0 : mmc1_chr1; return chr[(bank & 0x1F) * 0x1000 + (a & 0xFFF)]; }
    if (a < 0x3F00) return vram[nt_index(a)];
    return palette[pal_index(a)];
}

void Nes::ppu_mem_write(uint16_t a, uint8_t v) {
    a &= 0x3FFF;
    if (a < 0x2000) return;                           // CHR is ROM
    if (a < 0x3F00) { vram[nt_index(a)] = v; return; }
    palette[pal_index(a)] = v & 0x3F;
}

uint8_t Nes::ppu_read(uint16_t reg) {
    switch (reg) {
    case 2: {
        // The game polls the status register in busy-wait loops. Report vblank on every other read
        // (so "wait for vblank" and "wait for vblank to end" both finish) and the sprite-0 hit as set.
        uint8_t v = uint8_t((vblank_flag ? 0x80 : 0) | 0x40);
        vblank_flag = !vblank_flag; latch = false;
        return v;
    }
    case 4: return oam[oam_addr];
    case 7: {
        uint8_t v = read_buf; read_buf = ppu_mem(vaddr);
        if ((vaddr & 0x3FFF) >= 0x3F00) v = read_buf;
        vaddr += (ppu_ctrl & 4) ? 32 : 1;
        return v;
    }
    }
    return 0;
}

void Nes::ppu_write(uint16_t reg, uint8_t v) {
    switch (reg) {
    case 0: ppu_ctrl = v; break;
    case 1: ppu_mask = v; break;
    case 3: oam_addr = v; break;
    case 4: oam[oam_addr++] = v; break;
    case 5: if (!latch) scroll_x = v; else scroll_y = v; latch = !latch; break;
    case 6:
        if (!latch) vaddr = uint16_t((vaddr & 0x00FF) | (v & 0x3F) << 8);
        else vaddr = uint16_t((vaddr & 0xFF00) | v);
        latch = !latch; break;
    case 7: ppu_mem_write(vaddr, v); vaddr += (ppu_ctrl & 4) ? 32 : 1; break;
    }
}

// ---------------------------------------------------------------- widescreen state
void Cpu::wide_sprite_piece() {
    // DrawSpritePieces has just stored the piece's Y and the low byte of its X in RAM OAM slot X;
    // A is the high byte of its 16-bit screen X (0 = on screen; anything else gets hidden next).
    nes->wide_piece[X >> 2] = {ram[0x200 + X], ram[0x203 + X], int8_t(A), nes->oam_gen};
    // Remix Mode, mixing rounds: which round's graphics this piece is from (-1 = the current one's)
    nes->remix.piece_home[X >> 2] = nes->remix.draw_home;
    nes->remix.piece_gen[X >> 2] = nes->oam_gen;
    nes->remix.piece_custom[X >> 2] = nes->remix.draw_custom ? nes->remix.draw_obj : int8_t(-1);
    // the top of each object's pieces (the Totem Pole fires from his)
    if (const int o = nes->remix.draw_obj; o >= 0) {
        RemixRuntime& r = nes->remix;
        const uint8_t y = ram[0x200 + X];
        if (r.obj_top_gen[o] != nes->oam_gen || y < r.obj_top[o]) { r.obj_top[o] = y; r.obj_top_gen[o] = nes->oam_gen; }
    }
}

void Nes::latch_wide_state() {
    const uint8_t* ram = cpu.ram;
    // the fade out after a door, a death or a round's end (see below): the tasks are stopped, so
    // nothing is drawn and the sprites stay as they were in RAM OAM. Their pieces keep what was
    // noted about them (margin positions, Remix graphics), or they'd change look for the fade.
    const bool frozen = ram[GameMode] && !(ram[GameMode] & 0x80) && room.valid;
    for (int i = 0; i < 64; i++) {
        const WidePiece& p = wide_piece[i];
        if (frozen && p.gen != oam_gen && remix.piece_gen[i] != oam_gen) continue;
        // still the piece written this frame (hidden at Y $F0 by the game, X unchanged)?
        bool v = p.gen == oam_gen && p.hi != 0 && ram[0x200 + i * 4] == 0xF0 && ram[0x203 + i * 4] == p.x;
        wide_oam[i] = {v, p.hi, p.y};
        remix.oam_home[i] = remix.piece_gen[i] == oam_gen ? remix.piece_home[i] : int8_t(-1);
        remix.oam_custom[i] = remix.piece_gen[i] == oam_gen ? remix.piece_custom[i] : int8_t(-1);
    }
    oam_gen++;
    auto in_bank = [](uint16_t a) { return a >= 0x8000 && a < 0xC000; };
    RoomView r;
    r.columns = uint16_t(ram[LvlColumnsPtr] | ram[LvlColumnsPtr + 1] << 8);
    r.screens = uint16_t(ram[LvlScreensPtr] | ram[LvlScreensPtr + 1] << 8);
    r.attrs = uint16_t(ram[LvlAttrSetsPtr] | ram[LvlAttrSetsPtr + 1] << 8);
    r.metatiles = uint16_t(ram[LvlMetatilesPtr] | ram[LvlMetatilesPtr + 1] << 8);
    r.door = ram[DoorId];
    r.bank = ram[RoomDataBankCur];
    r.id = ram[RoomId];
    r.door_list = uint16_t(ram[0x7B] | ram[0x7C] << 8);
    // GameMode $08/$10/$40 = died / door / round complete: b4_GameFlow_ModeChange fades the room
    // out with its data and scroll untouched, so it's still a room (the margins keep coming from the
    // map instead of repeating the nametable's edges) until b4_ResetScreenState switches rendering
    // off (PpuMaskShadow). From then on it isn't, until the next room starts (GameMode 0): with
    // GameMode still $40 the ending runs inside b4_ShowScreen, and GameState is still 5 there.
    // Same in both versions.
    const bool fading = ram[GameMode] && !(ram[GameMode] & 0x80) && room.valid && (ram[PpuMaskShadow] & 0x08);
    r.valid = ram[GameState] == 5 && (ram[GameMode] == 0 || fading) && ram[RoomLastPage] < 16 && r.bank < 8 &&
              in_bank(r.columns) && in_bank(r.screens) && in_bank(r.attrs) && in_bank(r.metatiles);
    if (fading) {
        r.boss = room.boss; r.foreign = room.foreign;
        r.real_round = room.real_round; r.arena_round = room.arena_round;
    } else {
        int real = 0, arena = 0;
        r.boss = ram[BossRoomFlag] & 0x80;
        r.foreign = remix_foreign_arena(real, arena);
        r.real_round = int8_t(real); r.arena_round = int8_t(arena);
    }
    r.cam_x = int8_t(ram[ScreenPage]) * 256 + ram[ScrollX];   // page -1 while a room scrolls in
    r.width = (ram[RoomLastPage] + 1) * 256;
    r.coll = uint16_t(ram[0x68] | ram[0x69] << 8);              // (Room_Init sets it from a per-RoomId list)
    room = r;
}

int Nes::room_metatile(const RoomView& room, int tx, int ty) const {
    if (!room.valid || tx < 0 || tx >= room.width / 8 || ty < 0 || ty >= 24) return -1;
    auto rom = [&](uint16_t a) -> uint8_t {
        if (a >= 0xC000) return prg[0x1C000 + (a - 0xC000)];
        return a >= 0x8000 ? prg[room.bank * 0x4000 + (a - 0x8000)] : 0;
    };
    int page = tx >> 5, col = tx & 31;
    uint16_t screen = uint16_t(room.screens + ((page + room.door) & 0xFF) * 16);
    uint8_t column_id = rom(uint16_t(screen + (col >> 2) * 2));
    return rom(uint16_t(room.columns + column_id * 24 + ((col >> 1) & 1) * 12 + (ty >> 1)));
}

bool Nes::room_tile(const RoomView& room, int tx, int ty, uint8_t& tile, uint8_t& pal) const {
    if (!room.valid || tx < 0 || tx >= room.width / 8 || ty < 0 || ty >= 24) return false;
    auto rom = [&](uint16_t a) -> uint8_t {
        if (a >= 0xC000) return prg[0x1C000 + (a - 0xC000)];
        return a >= 0x8000 ? prg[room.bank * 0x4000 + (a - 0x8000)] : 0;
    };
    // the same lookups as Room_ScreenEntryPtr / Room_ColumnPtr / Room_BuildColumn / Room_AttrSetPtr
    int page = tx >> 5, col = tx & 31;
    uint16_t screen = uint16_t(room.screens + ((page + room.door) & 0xFF) * 16);
    uint8_t column_id = rom(uint16_t(screen + (col >> 2) * 2));
    uint8_t attr_id = rom(uint16_t(screen + (col >> 2) * 2 + 1));
    uint8_t metatile = rom(uint16_t(room.columns + column_id * 24 + ((col >> 1) & 1) * 12 + (ty >> 1)));
    tile = rom(uint16_t(room.metatiles + metatile * 4 + (ty & 1) * 2 + (col & 1)));
    uint8_t attr = rom(uint16_t(room.attrs + attr_id * 6 + (ty >> 2)));
    pal = (attr >> (((ty >> 1) & 1) * 4 + ((col >> 1) & 1) * 2)) & 3;
    return true;
}

// ---------------------------------------------------------------- rendering (whole frame at once)
void Nes::render_frame() {
    const int M = wide_margin, W = 256 + 2 * M;
    frame_w = W;
    RasterState bottom = current();
    RasterState top = split_seen ? top_state : bottom;
    int split_line = split_seen ? oam[0] + 3 : 240;      // sprite 0 sits at the top of the status bar

    // Widescreen in a room: a camera of its own (clamped to the room where the room is wide
    // enough) decides where the game's 256 columns go (play_x0); the rest comes from the room map.
    const bool in_room = M > 0 && room.valid && split_seen;   // (in_room implies widescreen)
    const uint8_t screen_id = cpu.ram[ScreenId];

    // Remix Mode, mixing rounds, in another round's arena: the arena keeps the look of the round the
    // player is in. The boss's own arena is what's loaded (so the fight plays as at home); cells
    // where the screen differs from that arena's empty-room layout are the boss's scenery and are
    // drawn as they are, the rest is drawn as this round's empty arena, in this round's colours
    // (it also shows through the scenery's blank pixels). The status bar takes this round's colours.
    // (latched with the room, so it lasts through the fade out when the player leaves)
    int real_round = 0, arena_round = 0;
    bool foreign = remix_foreign_arena(real_round, arena_round);
    if (room.valid) { foreign = room.foreign; real_round = room.real_round; arena_round = room.arena_round; }
    // (Chameleon Man, BossId $15, hides among the faces his arena's wall is made of: that wall is
    // his scenery, so his arena keeps its own look)
    const bool arena_look = foreign && cpu.ram[BossId] != 0x15;
    // Remix's own colours (this round's arena look, foreign sprites) go through the game's palette
    // fade like the real palette: brightness $044C (0 = normal, -$10 per step), as _func_c9de
    // (US $C9DE, JP $C9AA): $0F counts as $F0, below 0 -> $0F, $40 and up -> $30; bit 7 = not faded.
    const uint8_t bright = cpu.ram[0x044C];
    auto fade = [bright](uint8_t c) -> uint8_t {
        if (c & 0x80) return c & 0x3F;
        const uint8_t v = uint8_t((c == 0x0F ? 0xF0 : c) + bright);
        return (v & 0x80) ? 0x0F : v >= 0x40 ? 0x30 : v;
    };
    uint8_t look_pal_faded[13];
    static uint8_t look_home_tile[24][32], look_home_pal[24][32], look_cur_tile[24][32], look_cur_pal[24][32];
    static bool look_frame[24][32];   // the home arena's frame: the same in all its slots' layouts
    const uint8_t* look_pt = nullptr;
    const uint8_t* look_pal = nullptr;
    if (arena_look) {
        auto view = [&](int r, int door) {
            const RemixRuntime::Layout& l = remix.layout[r];
            RoomView v;
            v.valid = true; v.width = 256; v.door = uint8_t(door); v.bank = l.bank;
            v.columns = l.columns; v.screens = l.screens; v.attrs = l.attrs; v.metatiles = l.metatiles;
            return v;
        };
        const RoomView home_empty = view(8 + arena_round, remix.door_id[arena_round][0]);
        const RoomView cur_empty = view(8 + real_round, remix.door_id[real_round][0]);
        for (int ty = 0; ty < 24; ty++)
            for (int tx = 0; tx < 32; tx++) {
                room_tile(home_empty, tx, ty, look_home_tile[ty][tx], look_home_pal[ty][tx]);
                room_tile(cur_empty, tx, ty, look_cur_tile[ty][tx], look_cur_pal[ty][tx]);
                look_frame[ty][tx] = true;
            }
        for (int slot = 1; slot < 4; slot++) {
            if (!remix.has_slot[arena_round][slot]) continue;
            const RoomView v = view(8 + arena_round, remix.door_id[arena_round][slot]);
            for (int ty = 0; ty < 24; ty++)
                for (int tx = 0; tx < 32; tx++) {
                    uint8_t t, p;
                    room_tile(v, tx, ty, t, p);
                    if (t != look_home_tile[ty][tx]) look_frame[ty][tx] = false;
                }
        }
        look_pt = &chr[(remix.bg_bank[8 + real_round] & 0x1F) * 0x1000];
        for (int k = 0; k < 13; k++) look_pal_faded[k] = fade(remix.bg_pal[8 + real_round][k]);
        look_pal = look_pal_faded;
    }
    int view_x = 0, play_x0 = M;
    if (in_room) {
        view_x = room.width >= W ? std::clamp(room.cam_x - M, 0, room.width - W) : room.cam_x - M;
        play_x0 = room.cam_x - view_x;
    }

    // Mark Round 6 Doors (trainer, drawing only): the doors entered this run are drawn as
    // graphics/r6_markeddoor.png (the door, recoloured) in place of the room's tiles. A door list entry
    // is the tile row and column (page * 16 + x) of the player standing in the doorway; the door is
    // that 16 x 16 cell and the one above it (tools/formats/round6_doors.md). door_top[metatile column]
    // has a bit per metatile row where a marked door's top half is.
    static uint16_t door_top[256];
    const bool marks = trainers.mark_doors && doors_entered && room.valid && room.id == ROUND6_ROOM &&
                       room.door_list >= 0x8000 && room.door_list < 0xC000;
    if (marks) {
        std::memset(door_top, 0, sizeof door_top);
        const uint8_t* list = &prg[2 * 0x4000 + (room.door_list - 0x8000)];   // bank 2
        for (int k = 0; k < 64 && room.door_list - 0x8000 + 2 * k + 1 < 0x4000 && list[2 * k] != 0xFF; k++) {
            if (!(doors_entered >> k & 1)) continue;
            const int row = list[2 * k], col = list[2 * k + 1] - room.door * 16;   // (the door list counts pages from DoorId)
            if (row >= 1 && row < 12 && col >= 0 && col < 256) door_top[col] |= uint16_t(1 << (row - 1));
        }
    }
    // the door's pixel at room x rx, y py, if a marked door is there: true, with ci = 0 (the
    // backdrop) or its colour
    auto marked_door = [&](int rx, int py, uint8_t& colour) -> bool {
        if (rx < 0 || rx >= 256 * 16 || py >= 192) return false;
        const uint16_t tops = door_top[rx >> 4];
        if (!tops) return false;
        const int my = py >> 4;
        int iy;
        if (tops >> my & 1) iy = py & 15;
        else if (my >= 1 && (tops >> (my - 1) & 1)) iy = 16 + (py & 15);
        else return false;
        const custom_sprite::Sprite& sp = custom_sprite::R6_MARKED_DOOR;
        const char p = sp.rows[iy][rx & 15];
        colour = p == '.' ? 0xFF : fade(sp.colour(p));
        return true;
    };

    static uint8_t bg_opaque[MAX_FRAME_W * 240];
    for (int y = 0; y < 240; y++) {
        const bool playfield = y < split_line;
        const RasterState& s = playfield ? top : bottom;
        const int x0 = playfield ? play_x0 : M;
        uint32_t backdrop = rgb[(arena_look ? look_pal[0] : palette[0]) & 0x3F];
        uint32_t* row = &frame[y * W];
        if (!(s.mask & 0x08)) { for (int x = 0; x < W; x++) { row[x] = backdrop; bg_opaque[y * W + x] = 0; } continue; }
        int py = y + top.scroll_y;                          // vertical scroll can't change mid-frame
        int nt_y = (s.ctrl >> 1) & 1;
        if (py >= 240) { py -= 240; nt_y ^= 1; }
        uint8_t pt_bank = (s.ctrl & 0x10) ? s.chr1 : s.chr0;
        const uint8_t* pt = &chr[(pt_bank & 0x1F) * 0x1000];
        const TileOverride* lang_ov = lang.bg_override(pt_bank & 0x1F, screen_id);   // Language: the text's own tiles
        const bool map_row = in_room && playfield && py < 192;
        for (int x = 0; x < W; x++) {
            int gx = x - x0;                                // the game's own screen x
            uint8_t tile, pal;
            int tpx, tpy = py & 7;
            int look_tx = 0;                                // (arena look) the arena tile column this pixel comes from
            bool from_map = map_row && (gx < 0 || gx >= 256 || (gx < 8 && !(s.mask & 0x02)));
            if (marks && playfield && (from_map || ((gx >= 8 || (s.mask & 0x02)) && gx >= 0 && gx < 256))) {
                uint8_t colour;
                if (marked_door(room.cam_x + gx, py, colour)) {   // (the room x of this pixel, also in the margins: view_x + x)
                    bg_opaque[y * W + x] = colour != 0xFF;
                    row[x] = colour != 0xFF ? rgb[colour & 0x3F] : backdrop;
                    continue;
                }
            }
            if (from_map) {
                // outside a room narrower than the view, repeat its edge metatile column
                int wx = view_x + x, tx = wx >> 3, last = room.width / 8 - 2;
                if (tx < 0) tx = tx & 1;
                else if (tx > last + 1) tx = last + (tx & 1);
                room_tile(tx, py >> 3, tile, pal);
                tpx = wx & 7;
                look_tx = std::clamp(tx, 0, 31);
            } else {
                if (!from_map && gx < 8 && gx >= 0 && !(s.mask & 0x02) && M == 0) {
                    row[x] = backdrop; bg_opaque[y * W + x] = 0; continue;
                }
                // The nametable. Past the screen's edges: in a room (status bar, the strip under the
                // playfield) the edge tile column repeats; on other screens (title, password, round
                // start, story) the outer 32 pixels repeat, which continues their borders and drapes
                // (with a couple of exceptions, below).
                const int base = s.scroll_x + ((s.ctrl & 1) ? 256 : 0);
                int px = gx + base;
                tpx = px & 7;
                if (gx < 0) px = in_room ? base : base + (gx & 31);
                else if (gx > 255) px = in_room ? base + 255 : base + 224 + ((gx - 256) & 31);
                px &= 511;
                int nt_x = px >> 8; px &= 0xFF;
                look_tx = px >> 3;
                uint16_t nt = uint16_t(0x2000 | (nt_y << 11) | (nt_x << 10));
                tile = vram[nt_index(nt + (py >> 3) * 32 + (px >> 3))];
                if (!in_room && (gx < 0 || gx > 255)) {
                    // Exceptions, where the repeated strip holds part of a picture (same in both versions):
                    // the title card's mouth (title, tile rows 0-23) leaves the margins black, and the
                    // password and game over screens' right-hand skeleton (tiles $8D-$94) becomes the floor beside it.
                    if (screen_id == 1 && py < 192) {
                        row[x] = backdrop; bg_opaque[y * W + x] = 0; continue;
                    }
                    if ((screen_id == 2 || screen_id == 6) && tile >= 0x8D && tile <= 0x94)
                        tile = vram[nt_index(nt + (py >> 3) * 32 + ((px >> 3) | 1))];
                }
                uint8_t attr = vram[nt_index(nt + 0x3C0 + (py >> 5) * 8 + (px >> 5))];
                pal = (attr >> (((py >> 4) & 1) * 4 + ((px >> 4) & 1) * 2)) & 3;
            }
            const uint8_t* t = lang_ov && lang_ov->t[tile] ? lang_ov->t[tile] : pt + tile * 16;
            int bit = 7 - tpx;
            int ci = ((t[tpy] >> bit) & 1) | (((t[tpy + 8] >> bit) & 1) << 1);
            if (arena_look) {
                // (arenas are one screen at the start of the nametable; the widescreen margins repeat
                // the edge column, so they use the edge cell's look too)
                const int tx = look_tx, ty = py >> 3;
                if (!playfield) {
                    bg_opaque[y * W + x] = ci != 0;
                    row[x] = ci ? rgb[look_pal[1 + pal * 3 + ci - 1] & 0x3F] : backdrop;
                    continue;
                }
                // (tiles only: some bosses recolour parts of the arena through its attributes, e.g. Mad
                // Javelin Man, and that shouldn't turn the arena's own walls into scenery)
                const bool scenery = ty < 24 && tile != look_home_tile[ty][tx];
                // Scenery inside the arena lets the current arena show through its blank pixels (so
                // wall a boss's layout leaves out isn't a hole). In the frame (walls, floor, the exit
                // the game opens in the left wall once the boss is beaten) it's drawn as it is.
                if (!scenery || (!ci && !look_frame[ty][tx])) {
                    if (ty >= 24) { bg_opaque[y * W + x] = 0; row[x] = backdrop; continue; }
                    const uint8_t* ct = look_pt + look_cur_tile[ty][tx] * 16;
                    ci = ((ct[tpy] >> bit) & 1) | (((ct[tpy + 8] >> bit) & 1) << 1);
                    bg_opaque[y * W + x] = ci != 0;
                    row[x] = ci ? rgb[look_pal[1 + look_cur_pal[ty][tx] * 3 + ci - 1] & 0x3F] : backdrop;
                    continue;
                }
            }
            bg_opaque[y * W + x] = ci != 0;
            row[x] = ci ? rgb[palette[pal * 4 + ci] & 0x3F] : backdrop;
        }
    }
    // 3D mode: the background alone, before the sprites go on
    if (scene3d) {
        std::memcpy(scene3d->bg, frame, sizeof(uint32_t) * W * 240);
        std::memset(scene3d->spr, 0, sizeof(uint32_t) * W * 240);
    }
    // sprites: 8x8, drawn back to front so lower OAM indexes win
    struct Box { int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20), flipped = 0, pieces = 0; };
    Box custom[16];                                         // objects drawn by the port (custom_sprites.h)
    for (int i = 63; i >= 0; i--) {
        int sy = oam[i * 4] + 1, tile = oam[i * 4 + 1], at = oam[i * 4 + 2], sx = oam[i * 4 + 3];
        if (sy >= 240) {
            if (!M || !wide_oam[i].valid) continue;
            sy = wide_oam[i].y + 1;                         // culled by the game as off screen: put it back
            sx += wide_oam[i].hi * 256;
            if (sy >= 240) continue;
        }
        // drawn by the port instead of from CHR (the Totem Pole's head shots and 2nd frame): just
        // note where the object's pieces are; its own sprite goes there below
        if (const int o = remix.oam_custom[i]; o >= 0 && o < 16) {
            Box& b = custom[o];
            b.pieces++;
            b.flipped += (at & 0x40) != 0;
            b.x0 = std::min(b.x0, sx); b.y0 = std::min(b.y0, sy); b.x1 = std::max(b.x1, sx + 8); b.y1 = std::max(b.y1, sy + 8);
            continue;
        }
        for (int r = 0; r < 8; r++) {
            int y = sy + r; if (y >= 240) break;
            const bool playfield = y < split_line;
            const RasterState& s = playfield ? top : bottom;
            if (!(s.mask & 0x10)) continue;
            uint8_t bank = (s.ctrl & 0x08) ? s.chr1 : s.chr0;
            // Remix Mode, mixing rounds: an enemy from another round, in its own round's graphics and colours
            const int home = remix.oam_home[i];
            if (home >= 0) bank = remix.sprite_bank[home];
            int ty = (at & 0x80) ? 7 - r : r;
            const uint8_t* t = &chr[(bank & 0x1F) * 0x1000 + tile * 16];
            for (int c = 0; c < 8; c++) {
                int gx = sx + c;
                // left-column sprite clipping: in widescreen there is picture to the left, so it
                // only still hides sprite 0, the status-bar split marker the game parks there
                if (gx < 8 && gx >= 0 && !(s.mask & 0x04) && (!M || i == 0)) continue;
                int x = gx + (playfield ? play_x0 : M);
                if (x < 0 || x >= W) continue;
                if (!M && gx >= 256) break;
                int bit = (at & 0x40) ? c : 7 - c;
                int ci = ((t[ty] >> bit) & 1) | (((t[ty + 8] >> bit) & 1) << 1);
                if (!ci) continue;
                if ((at & 0x20) && bg_opaque[y * W + x]) continue;   // behind the background
                const uint8_t colour = home >= 0 ? fade(remix.sprite_pal[home][(at & 3) * 3 + ci - 1]) : palette[0x10 + (at & 3) * 4 + ci];
                frame[y * W + x] = rgb[colour & 0x3F];
                if (scene3d) scene3d->spr[y * W + x] = rgb[colour & 0x3F] | 0xFF000000u;
            }
        }
    }
    // Objects drawn by the port (prototype, the 'finished' Totem Pole): his 2nd frame bottom-aligned
    // and centred on his own sprite, facing the same way; the head shots centred, facing the way they
    // fly. Over the other sprites, the Totem Pole first.
    auto draw_custom = [&](const custom_sprite::Sprite& sp, int left, int top_y, bool flip) {
        for (int r = 0; r < sp.h; r++) {
            const int y = top_y + r;
            if (y < 0 || y >= 240) continue;
            const bool playfield = y < split_line;
            if (!((playfield ? top : bottom).mask & 0x10)) continue;
            for (int c = 0; c < sp.w; c++) {
                const char p = sp.rows[r][flip ? sp.w - 1 - c : c];
                if (p == '.') continue;
                const int gx = left + c;
                if (!M && (gx < 0 || gx >= 256)) continue;
                const int x = gx + (playfield ? play_x0 : M);
                if (x < 0 || x >= W) continue;
                frame[y * W + x] = rgb[sp.colour(p) & 0x3F];   // NES colours: the chosen palette, like the game's sprites
                if (scene3d) scene3d->spr[y * W + x] = rgb[sp.colour(p) & 0x3F] | 0xFF000000u;
            }
        }
    };
    for (int kind = 2; kind >= 1; kind--)
        for (int o = 0; o < 16; o++) {
            const Box& b = custom[o];
            if (!b.pieces || remix.obj_custom_kind[o] != kind) continue;
            const int cx = (b.x0 + b.x1) / 2;
            if (kind == 2) {
                const custom_sprite::Sprite& sp = custom_sprite::TOTEM_FRAME2;
                draw_custom(sp, cx - sp.w / 2, b.y1 - sp.h, b.flipped * 2 <= b.pieces);   // the PNG faces the other way to his unflipped pieces
            } else {
                const custom_sprite::Sprite& sp = custom_sprite::TOTEM_HEAD;
                const int cy = (b.y0 + b.y1) / 2;
                if (cx != remix.custom_last_x[o]) remix.custom_flip[o] = cx < remix.custom_last_x[o];
                remix.custom_last_x[o] = int16_t(cx);
                draw_custom(sp, cx - sp.w / 2, cy - sp.h / 2, remix.custom_flip[o]);
            }
        }
    // Widescreen while playing: the extra playfield either side of the game's own screen a little
    // darker, so it's clear that enemies appear and vanish at the original screen's edges (not in
    // boss rooms: an arena is one screen, and nothing appears at its edges). The title screen too,
    // where the monster parade walks on and off at the edges.
    const bool dim_play = in_room && !room.boss, dim_title = !in_room && screen_id == 1 && M > 0;
    if (wide_dim && (dim_play || dim_title)) {
        auto dim = [](uint32_t c) { return (c & 0xFF000000u) | ((c >> 2) & 0x3F3F3F) * 2 + ((c >> 3) & 0x1F1F1F); };   // x 5/8
        const int x0 = dim_play ? play_x0 : M;
        for (int y = 0; y < split_line && y < 240; y++)
            for (int x = 0; x < W; x++) {
                if (x >= x0 && x < x0 + 256) { x = x0 + 255; continue; }
                frame[y * W + x] = dim(frame[y * W + x]);
                if (scene3d) {
                    scene3d->bg[y * W + x] = dim(scene3d->bg[y * W + x]);
                    if (scene3d->spr[y * W + x]) scene3d->spr[y * W + x] = dim(scene3d->spr[y * W + x]);
                }
            }
    }
    if (scene3d) build_scene3d(view_x, play_x0, in_room, split_line);
}

// 3D mode (drawing only): which of the room's cells are solid, for render3d.cpp (the picture
// itself is the frame's own background and sprites).
void Nes::build_scene3d(int view_x, int play_x0, bool in_room, int split_line) {
    Scene3D& sc = *scene3d;
    const int W = frame_w;
    sc.w = W;
    sc.play_x0 = play_x0;
    sc.bar_x0 = wide_margin;
    sc.split = split_line;
    sc.in_room = in_room;
    sc.backdrop = rgb[palette[0] & 0x3F];
    if (!in_room) { sc.cells = 0; return; }
    auto rom = [&](uint16_t a) -> uint8_t { return a >= 0xC000 ? prg[0x1C000 + (a - 0xC000)] : 0; };
    const int last = room.width / 8 - 2;
    auto clamp_tx = [&](int tx) {                     // past the room's ends its edge metatile column repeats
        if (tx < 0) return tx & 1;
        if (tx > last + 1) return last + (tx & 1);
        return tx;
    };
    auto cell_type = [&](int tx, int ty) -> uint8_t {
        const int mt = room_metatile(room, clamp_tx(tx), ty);
        return mt < 0 || room.coll < 0xC000 ? 0 : rom(uint16_t(room.coll + mt));
    };
    // the cells covering the frame
    const int tx0 = view_x >= 0 ? view_x / 8 : -((7 - view_x) / 8);
    sc.cell_x0 = tx0 * 8 - view_x;
    sc.tx0 = tx0;
    sc.cells = std::min(Scene3D::CELL_COLS, (W - sc.cell_x0 + 7) / 8);
    for (int ty = 0; ty < 24; ty++)
        for (int c = 0; c < sc.cells; c++) sc.type[ty][c] = cell_type(tx0 + c, ty);
}

// ---------------------------------------------------------------- difficulty (not in the original game)
// Casual and Nasty scale three things; Normal leaves every one of these calls doing nothing.
//   - Damage to the player: unchanged on Casual. On Nasty, ordinary hits are doubled (1 -> 2) and a
//     boss's own hit costs 1 more (US 2 -> 3, prototype 3 -> 4).
//   - Enemy and boss starting HP: x0.5 or x1.5, rounded up. HP $FF (only an instant kill works) is left alone.
//   - Drops: Casual turns some "nothing" drops into small health or a Bert capsule; Nasty removes
//     half of the health and capsule drops. The "?" and Bert's weapon items are left alone.
//   - Reflected enemy shots (instant kills on Normal) only kill outright half the time on Nasty;
//     otherwise they do 3 damage.
static constexpr int CASUAL_HEALTH_PERCENT = 25, CASUAL_CAPSULE_PERCENT = 10;   // of "nothing" drops
static constexpr int NASTY_REMOVE_PERCENT = 50;                                 // of health and capsule drops
static constexpr int NASTY_REFLECT_KILL_PERCENT = 50, NASTY_REFLECT_DAMAGE = 3;

uint32_t Nes::diff_random(uint32_t n) {
    diff_rng ^= diff_rng << 13; diff_rng ^= diff_rng >> 17; diff_rng ^= diff_rng << 5;
    return diff_rng % n;
}

static uint8_t scale_hp(Difficulty d, uint8_t hp) {
    if (d == Difficulty::Normal || hp == 0 || hp == 0xFF) return hp;
    int v = d == Difficulty::Casual ? (hp + 1) / 2 : hp + (hp + 1) / 2;
    return uint8_t(std::clamp(v, 1, 0xFE));
}

void Cpu::diff_player_damage_begin() { nes->diff_health_before = ram[PlayerHealth]; }

void Cpu::diff_player_damage_end() {
    if (nes->trainers.infinite_life) { ram[PlayerHealth] = nes->diff_health_before; return; }   // trainer
    if (nes->difficulty != Difficulty::Nasty) return;
    const uint8_t before = nes->diff_health_before;
    const int taken = uint8_t(before - ram[PlayerHealth]);       // 1 normally, more from a boss
    if (taken <= 0 || taken > 8) return;
    // a boss's hit: the same test as b4_PlayerHit (boss room, and the player was hit by object 6)
    const bool boss_hit = (ram[BossRoomFlag] & 0x80) && ram[ObjHitBy] == 6;
    const int nasty = boss_hit ? taken + 1 : taken * 2;
    ram[PlayerHealth] = uint8_t(before - nasty);                  // the game clamps it at 0 next
}

uint8_t Cpu::diff_enemy_hp(uint8_t hp) { return scale_hp(nes->difficulty, hp); }
uint8_t Cpu::diff_boss_hp(uint8_t hp) { return scale_hp(nes->difficulty, hp); }

void Cpu::diff_enemy_hit() {
    // On Normal, a shot the player knocked back into an enemy (objects 3-5) does damage $FF
    if (nes->difficulty != Difficulty::Nasty || ram[0x10] != 0xFF || (ram[PlayerForm] & 0x80)) return;
    const uint8_t by = ram[ObjHitBy + ram[CurrentTaskSlotId]];
    if (by < 3 || by > 5) return;
    if (ram[EnemyHealth + ram[CurrentTaskSlot]] == 0xFF) return;   // can only be killed this way
    if (nes->diff_random(100) >= unsigned(NASTY_REFLECT_KILL_PERCENT)) ram[0x10] = NASTY_REFLECT_DAMAGE;
}

void Cpu::diff_drop() {
    if (nes->difficulty == Difficulty::Normal) return;
    uint8_t& kind = ram[TaskDropKind + X];
    if (kind > 7) return;
    const bool bert = ram[PlayerForm] & 0x80;
    // drop kinds (b5_DropTableMark / b5_DropTableBert): 0 nothing, 3 "?", 5-7 Bert's weapon items when Bert
    const bool health = kind == 1 || (!bert && kind >= 4);
    const bool capsule = kind == 2 || (bert && kind == 4);
    if (nes->difficulty == Difficulty::Casual) {
        if (kind != 0) return;
        uint32_t r = nes->diff_random(100);
        if (r < unsigned(CASUAL_HEALTH_PERCENT)) kind = 1;
        else if (r < unsigned(CASUAL_HEALTH_PERCENT + CASUAL_CAPSULE_PERCENT)) kind = 2;
    } else if ((health || capsule) && nes->diff_random(100) < unsigned(NASTY_REMOVE_PERCENT)) {
        kind = 0;
    }
}

// ---------------------------------------------------------------- trainers (translate.py TRAINER_HOOKS)
uint8_t Cpu::trainer_start_health(uint8_t a) {
    const Trainers& t = nes->trainers;
    if (t.full_life) a = 0x2C;                         // the most health the game allows
    if (t.always_bert) { ram[PlayerForm] |= 0x80; ram[BertTimer] = 1; }
    return a;
}

void Cpu::trainer_title_done() {
    nes->doors_entered = 0;                            // Mark Round 6 Doors: a new run (new game or password)
    if (Y == 2) { nes->is_cheating = true; nes->lb_new_run(); return; }   // the password screen
    nes->is_cheating = nes->session_cheats();          // a new game
    nes->lb_new_run();
    if (nes->debug_ending) X = 9;   // --ending: `stx ScreenId` then shows the ending story (screen 9) instead of the intro (3)
    if (nes->trainers.start_round >= 2 && nes->trainers.start_round <= 8) ram[RoundNumber] = uint8_t(nes->trainers.start_round - 1);
}

// The jump is a table of per-frame movements up (TaskDropKind = the step; negative = falling).
// Super Jump doubles the rising; Moon Jump doubles it and takes each step for two frames, and
// halves the falling, so the player goes about four times as high and floats down.
void Cpu::trainer_jump_step() {
    if (!nes->trainers.moon_jump) return;
    nes->moon_hold = !nes->moon_hold;
    if (nes->moon_hold) ram[TaskDropKind]--;           // the inc that follows puts it back: same step again
}

// Mark Round 6 Doors: TryEnterDoor has found the door the player is entering (at the same point in
// both versions); Y is its offset in the room's door list (2 bytes per entry). Only noted; the
// renderer draws the marks.
void Cpu::trainer_door_entered() {
    if (nes->trainers.mark_doors && ram[RoomId] == ROUND6_ROOM && Y / 2 < 64) nes->doors_entered |= uint64_t(1) << (Y / 2);
}

uint8_t Cpu::trainer_jump(uint8_t a) {
    const Trainers& t = nes->trainers;
    if (a == 0x80 || !(t.super_jump || t.moon_jump)) return a;
    if (a < 0x80) return uint8_t(a * 2);               // rising (at most 4 pixels, so at most 8)
    if (t.moon_jump) return uint8_t(-((-int8_t(a) + 1) / 2));   // falling: -1 -1 -2 -2 for -1 -2 -3 -4
    return a;
}

// ---------------------------------------------------------------- leaderboards (translate.py LB_HOOKS)
// With nes->lb.enabled off every one of these does nothing, and the game plays as released.
// The run: trainer_title_done (a new game from the title, not cheating) -> the clock starts when
// ROUND 1 START is over -> the final boss's defeat logs the time (frames since then) and the run's
// best score -> the credits' last page waits for a button, then the GAME OVER screen's code shows the result.
void Nes::lb_new_run() {
    lb.run = lb.enabled && !is_cheating;
    lb.started = lb.killed = lb.finished = lb.showing = lb.log_failed = false;
    lb.best = 0;
    lb.idle = 0;
    lb.place = LbPlace{};
}

void Cpu::lb_round_start() {
    Nes::LbState& lb = nes->lb;
    if (lb.run && !lb.started) { lb.started = true; lb.start_frame = nes->frame_count; }   // (a continue comes back here: already started)
}

void Cpu::lb_boss_reward() {
    // The fight is over now (the game then waits a little before it counts the boss): the final one is the round 9 boss
    Nes::LbState& lb = nes->lb;
    if (lb.run && lb.started && !lb.killed && ram[BossesLeft] == 1 && ram[RoundNumber] >= 8) { lb.killed = true; lb.kill_frame = nes->frame_count; }
}

// The run's score is its own best: the highest the current score (ScoreDigits, $8A-$8F) got from round 1 on. Not the
// HIGH value: that's the session's (200,000 from power-on, kept over every game since).
void Cpu::lb_track_score() {
    Nes::LbState& lb = nes->lb;
    if (lb.run && lb.started && !lb.finished) lb.best = std::max(lb.best, lb_score_from_digits(&ram[ScoreDigits]));
}

void Cpu::lb_final_boss() {
    Nes::LbState& lb = nes->lb;
    if (!lb.run || !lb.started || lb.finished) return;
    const uint64_t end = lb.killed ? lb.kill_frame : nes->frame_count;
    lb.result.frames = uint32_t(std::min<uint64_t>(end - lb.start_frame, 0xFFFFFFFFu));
    lb_track_score();   // (the boss's points were just added)
    lb.result.score = lb.best;
    lb.result.when = int64_t(std::time(nullptr));
    bool ok = true;
    lb.place = lb_log(lb.path, lb.category, lb.result, &ok);
    lb.log_failed = !ok;
    if (!ok) std::fprintf(stderr, "leaderboards: can't write %s\n", lb.path.c_str());
    lb.finished = true;
    lb.idle = 0;
}

bool Cpu::lb_credits_idle() {
    Nes::LbState& lb = nes->lb;
    if (!lb.finished || lb.showing) return false;
    // This runs every frame of the ending's last scene, from the moment the credits start, so wait until the
    // credits text has reached its end (StoryTextFlags bit 6, set at the text's $FF; the flags settle at $43 with
    // just Bert in the doorway)
    if (!(ram[StoryTextFlags] & 0x40)) { lb.idle = 0; return false; }
    lb.idle++;
    // A or Start, once that has been up a second (the player may be pressing buttons through the credits)
    if (lb.idle < 60 || !(ram[JoyPressed] & 0x90)) return false;
    lb.showing = true;
    ram[ScreenId] = 6;   // the GAME OVER screen: its room and skeletons (b4_ShowScreen sets it up and runs its code)
    return true;
}

void Cpu::lb_screen_begin() { if (nes->lb.showing) nes->lb_draw_screen(); }

void Cpu::lb_screen_music() {
    // the password screen's music instead of GAME OVER's: PlaySoundRange A..A+3 (US $04, JP $34)
    if (nes->lb.showing) { A = nes->game == Game::Proto ? 0x34 : 0x04; Y = 4; }
}

bool Cpu::lb_screen_frame() {
    if (!nes->lb.showing) return false;
    for (int i = 0; i < 64; i++) ram[0x200 + i * 4] = 0xF0;   // no menu cursor
    return true;
}

// The result screen's own tiles: the font has no ':' or '#', so two tiles that are blank in CHR bank
// $1F (the GAME OVER screen's background tiles) get them, in this copy of the CHR only.
static constexpr int LB_TILE_COLON = 0xF0, LB_TILE_HASH = 0xF1, LB_CHR_BANK = 0x1F;
static const char* const LB_GLYPHS[2][8] = {   // 1 = the font's background, 3 = its white
    {"11111111", "11111111", "11331111", "11331111", "11111111", "11111111", "11331111", "11331111"},
    {"11111111", "11311311", "11311311", "13333331", "11311311", "13333331", "11311311", "11311311"}};

void Nes::lb_restore_tiles() {
    if (!lb.patched) return;
    for (int g = 0; g < 2; g++)
        std::memcpy(&chr[LB_CHR_BANK * 0x1000 + (LB_TILE_COLON + g) * 16], lb.saved_tiles[g], 16);
    lb.patched = false;
}

void Nes::lb_draw_screen() {
    if (chr.size() < 0x20000) return;
    if (!lb.patched) {
        for (int g = 0; g < 2; g++) {
            uint8_t* t = &chr[LB_CHR_BANK * 0x1000 + (LB_TILE_COLON + g) * 16];
            std::memcpy(lb.saved_tiles[g], t, 16);
            for (int y = 0; y < 8; y++) {
                uint8_t lo = 0, hi = 0;
                for (int x = 0; x < 8; x++) { const int v = LB_GLYPHS[g][y][x] - '0'; lo = uint8_t(lo << 1 | (v & 1)); hi = uint8_t(hi << 1 | (v >> 1)); }
                t[y] = lo; t[y + 8] = hi;
            }
        }
        lb.patched = true;
    }
    // the screen is the nametable at $2000, 32 tiles across (the GAME OVER layout is already in it)
    auto cell = [&](int row, int col) -> uint8_t& { return vram[nt_index(uint16_t(0x2000 + row * 32 + col))]; };
    auto tile_of = [](char ch) -> uint8_t {
        if (ch >= '0' && ch <= '9') return uint8_t(0x04 + ch - '0');
        if (ch >= 'A' && ch <= 'Z') return uint8_t(0x0E + ch - 'A');
        if (ch == '-') return 0x43;
        if (ch == ':') return LB_TILE_COLON;
        if (ch == '#') return LB_TILE_HASH;
        return 0x01;   // blank (black)
    };
    auto put = [&](int row, int col, const std::string& text) {
        for (size_t i = 0; i < text.size() && col + int(i) < 32; i++) cell(row, col + int(i)) = tile_of(text[i]);
    };
    // the room inside the walls: columns 6-25. Clear it, then a wider frame (tile $7F) than GAME OVER's
    for (int r = 9; r <= 21; r++) for (int c = 6; c <= 25; c++) cell(r, c) = 0x01;
    cell(10, 6) = cell(10, 25) = 0x7E;            // the skulls beside the heading
    put(10, 8, "CONGRATULATIONS");
    const std::string cat = lb.category.screen_text();
    put(13, 6 + (20 - int(cat.size())) / 2, cat);
    for (int c = 6; c <= 25; c++) cell(15, c) = cell(21, c) = 0x7F;
    for (int r = 15; r <= 21; r++) cell(r, 6) = cell(r, 25) = 0x7F;
    put(17, 8, "TIME " + lb_clock(lb.result.frames) + (lb.place.time ? " #" + std::to_string(lb.place.time) : ""));
    put(19, 8, "SCORE " + lb_score_digits(lb.result.score) + (lb.place.score ? " #" + std::to_string(lb.place.score) : ""));
}

// ---------------------------------------------------------------- hooks called by the translated code
// Frame timing for the UpdateRandom spin (NTSC): an NMI every 29780.5 CPU cycles (rendering on: odd frames are a
// dot short). The status bar's sprite 0 (Y $BD, X 0) is hit at the start of scanline 190, which is 21 + 190
// scanlines of 341/3 cycles after the NMI (at scanline 241).
static constexpr uint32_t SPRITE0_HIT_CYCLE = (21 + 190) * 341 / 3;

void Cpu::sprite0_split() {
    if (cyc < SPRITE0_HIT_CYCLE) cyc = SPRITE0_HIT_CYCLE;   // bit PPU_STATUS / bvc: the CPU waits for the hit
    nes->top_state = nes->current();   // NMI has set up the playfield; the status-bar settings come next
    nes->split_seen = true;
}

// UpdateRandom, as the ROM does it (the spin's later rounds; the first ran as translated code): a 64-bit shift
// register at $045F-$0466, the new bit = bit 1 of $045F xor bit 1 of $0460. Leaves $18, A, C, N and Z as it does.
void Cpu::update_random() {
    ram[0x18] = ram[0x45F] & 2;
    A = uint8_t((ram[0x460] & 2) ^ ram[0x18]);
    C = A != 0;
    cyc += A ? 84 : 83;   // jsr ... rts (the beq over the sec is taken when the bits match)
    for (int i = 0x45F; i <= 0x466; i++) ram[i] = ror(ram[i]);
}

uint16_t Cpu::frame_hook(uint16_t resume_pc) {
    // WaitNextFrame's last loop, `jsr UpdateRandom / lda NmiDoneFlag / beq` (the hook is on the lda): a real NES goes
    // round it until the NMI, so the RNG moves on as many steps as there are cycles left in the frame (about 30-60 in
    // a room; it depends on how long the frame's work and the sound update took). That is the game's only source of
    // randomness: the title screen seeds the RNG with $5B in every byte.
    const uint32_t frame_len = (nes->frame_count & 1) ? 29780 : 29781;
    rng_steps = 1;
    while (cyc < frame_len) {
        rng_steps++;
        cyc += 3 + 3;   // lda NmiDoneFlag (still 0), beq taken
        update_random();
    }
    // the NMI came at frame_len: the next frame's cycles count from there (7 for the NMI itself). A frame whose work
    // ran over (a lag frame) carries the extra over; the port still runs the NMI here, after the work.
    cyc = (cyc - frame_len) % frame_len + 7;
    nes->render_frame();
    std::vector<int16_t>& samples = nes->audio.end_frame();
    if (nes->music.listening()) nes->music.end_frame(samples, NesAudio::SAMPLE_RATE);
    nes->audio_out = &samples;
    nes->split_seen = false;
    nes->frame_count++;
    // trainer Always Bert: keep Bert's time from running out (the game turns Bert back at $23)
    if (nes->trainers.always_bert && (ram[PlayerForm] & 0x80)) ram[BertTimer] = 1;
    lb_track_score();
    if (nes->on_frame && !nes->on_frame(*nes)) nes->quit = true;
    // Quitting: Nes::run only checks quit when the code moves between banks, and some waits (a speech bubble waiting
    // for A) never leave the fixed bank. Address 0 is in no bank, so run_bank7 returns straight to Nes::run.
    if (nes->quit) return 0x0000;
    if (nes->reset_requested) { nes->soft_reset(); return rd16(0xFFFC); }
    if (!(nes->ppu_ctrl & 0x80)) return resume_pc;          // NMI disabled: nothing to run
    // enter the NMI handler exactly like the hardware: push PC and P, jump through $FFFA
    push16(resume_pc);
    push(getP(false));
    I = true;
    nes->vblank_flag = true;
    return rd16(0xFFFA);
}

void Cpu::ram191_trampoline() {
    // InitSound builds `STA $40xx / RTS` at $0191 and _func_cef7 patches the low address byte
    if (ram[0x191] != 0x8D || ram[0x194] != 0x60) { std::fprintf(stderr, "fatal: unexpected code at $0191\n"); std::exit(2); }
    const uint16_t addr = uint16_t(ram[0x192] | ram[0x193] << 8);
    uint8_t v = A;
    // MIDI music playing, or music off: the music channels (logical channels 0-3, in X) keep the APU channel
    // set up (period, length) so the sound effects that take it over behave as before, but their
    // volume register goes in silenced: pulse/noise volume 0, triangle linear counter 0.
    if ((nes->music.playing() || nes->music_off) && X < 4 && (addr & 3) == 0 && addr <= 0x400C)
        v = (addr & 0xC) == 8 ? uint8_t(0x80) : uint8_t(v & 0xF0);
    wr(addr, v);
}

// ---------------------------------------------------------------- music as MIDI (music.h)
void Cpu::music_key_on() { if (X < 4 && nes->music.listening()) nes->music.key_on(X); }
void Cpu::music_volume() { if (X < 4 && nes->music.listening()) nes->music.volume(X, ram[0x1E]); }
void Cpu::music_period() { if (X < 4 && nes->music.listening()) nes->music.period(X, A, Y); }

// ---------------------------------------------------------------- main loop: run translated code bank by bank
void Nes::run() {
    uint16_t pc = cpu.rd16(0xFFFC);
    // the translated code for the loaded ROM
    auto b4 = game == Game::Proto ? jp::run_bank4 : run_bank4;
    auto b5 = game == Game::Proto ? jp::run_bank5 : run_bank5;
    auto b7 = game == Game::Proto ? jp::run_bank7 : run_bank7;
    while (!quit) {
        if (pc >= 0xC000) pc = b7(cpu, pc);
        else if (pc >= 0x8000 && cpu.prg_bank == 4) pc = b4(cpu, pc);
        else if (pc >= 0x8000 && cpu.prg_bank == 5) pc = b5(cpu, pc);
        else cpu.unknown_pc(pc, -1);
    }
}

// Monster Party native port: Remix Mode (see remix.h).
#include "remix.h"
#include "nes.h"
#include "ram_symbols.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <deque>
#include <climits>
#include <numeric>

namespace {
// Where the data is, per version. Bank 2: the door tables (one list per RoomId; rooms 0-7 are the
// rounds' main levels, 8-15 their boss arenas). Bank 7: the enemy spawn tables.
struct RemixAddrs {
    uint16_t door_pos;       // words per RoomId -> door positions, 2 bytes per door, $FF-terminated
    uint16_t door_dest;      // words per RoomId -> door destinations, 2 bytes per door (byte 0: RoomId)
    uint16_t door_flags;     // words per RoomId -> door flags, 2 bytes per door (byte 0: bit 7 boss door, bits 4-6 boss)
    uint16_t spawn_maps;     // words per RoomId -> 256-byte spawn map (0 = nothing, else 1-based entry)
    uint16_t spawn_lists;    // words per RoomId -> spawn entries, 2 bytes each: enemy type, Y/drop
    uint16_t room_chr0;      // bank 7, per RoomId: sprite CHR bank (RoomChr0Banks)
    uint16_t room_palette;   // bank 7, per RoomId: SetPalette id (RoomPalettes)
    uint16_t palettes;       // bank 6: 25 bytes per palette id (background colour, 4 BG palettes, 4 sprite palettes)
    uint16_t room_tileset;   // bank 7, per RoomId: tileset (RoomTileset)
    uint16_t room_data_bank; // bank 7, per RoomId: PRG bank of the map data (RoomDataBank)
    uint16_t tileset_columns, tileset_screens, tileset_attrs, tileset_metatiles;   // bank 7, words per tileset
    uint16_t bosses_per_round; // bank 4: b4_BossesPerRound (9 bytes, one per round)
    uint16_t room_collision;   // bank 7: words per RoomId -> 1 byte per metatile id: the tile type (4 solid, 6 door, 7 exit)
};
constexpr RemixAddrs US_ADDRS{0x98AE, 0x9A26, 0x9B8E, 0xED69, 0xED89, 0xDA9B, 0xDABB, 0xAEDA,
                              0xDA6B, 0xDA7B, 0xDAEB, 0xDB0B, 0xDB2B, 0xDACB, 0x945B, 0xDD49};
constexpr RemixAddrs JP_ADDRS{0x98AE, 0x9A20, 0x9B82, 0xEC28, 0xEC48, 0xD986, 0xD9A6, 0xAAC8,
                              0xD956, 0xD966, 0xD9D6, 0xD9F6, 0xDA16, 0xD9B6, 0x822C, 0xDC08};

// Enemy kinds: spawn types are only swapped for others of the same kind from the same round.
enum Kind : uint8_t { HAZARD, FIXED, WALKER, FLYING, WATER };

Kind kind_of(Game game, uint8_t type) {
    switch (type) {
    case 0x01: case 0x02: case 0x04:                       // ManFacedDog, PsiTeen, EyeballHorror
    case 0x05: case 0x08:                                  // Alligator,  Fishman
    case 0x0B: case 0x21:                                  // Skeleton, WarpSkeleton
    case 0x0D: case 0x0E: case 0x0F:                       // Snake, Scorpion, EgyptAlligator
    case 0x14: case 0x15: case 0x17:                       // HauntedTable, HauntedChair, HauntedPants
    case 0x19: case 0x1A:                                  // ElephantMan, Umpire
        return WALKER;
    case 0x22: case 0x03:                                  // TurretScorpion, StuckLegs
    case 0x06: case 0x07:                                  // AlienEgg, PukeMouth,
        return FIXED;
    case 0x18:                                             // Spirit: floats in the US; a hopping ghost in the prototype
        return game == Game::Proto ? WALKER : FLYING;
    case 0x09: case 0x0A:                                  // FlyingVamp, UmbrellaBat
    case 0x1B:                                             // FlyingImp
    case 0x1C: case 0x1D:                                  // BombBird, Witch
        return FLYING;
    case 0x11: case 0x12: case 0x13:                       // FakeShark, SeaSerpent, Fishbones
        return WATER;
    default:                                               // Stalact, Raindrop, BloodDrip, VerticalBlock, Constellations
        return HAZARD;
    }
}

// splitmix64: the same sequence for a seed on every platform (unlike std:: distributions)
struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint32_t below(uint32_t n) { return uint32_t(next() % n); }
};

// a CPU address in a PRG bank -> offset in prg
size_t at(int bank, uint16_t addr) { return size_t(bank) * 0x4000 + (addr & 0x3FFF); }
}

// ---------------------------------------------------------------- round 6's door maze
// Round 6 (RoomId 5) is one long level whose doors warp to each other: 56 doors, 27 pairs (both ways),
// one one-way door and one boss door. Remix shuffles who leads where. The map (tile types per
// 16 x 16 cell, 12 rows x 256 columns) is read from the ROM, and a shuffle is only used if the round
// can still be beaten: from the start the boss door can be reached, and from the boss door the exit
// (the lower right corner), and from no place that can be reached is either one out of reach.
namespace {
constexpr int MAZE_ROOM = 5;
constexpr int MAZE_ROWS = 12, MAZE_COLS = 256, MAZE_CELLS = MAZE_ROWS * MAZE_COLS;
constexpr int MAZE_START_ROW = 10, MAZE_START_COL = 8;   // (the prototype starts 2 cells to the right, on the same floor)
constexpr int MAZE_MIN_BOSS_HOPS = 1;                          // door hops: start -> boss, at least
constexpr int MAZE_MIN_EXIT_HOPS = 2;                          // door hops: boss -> exit, at least

// The destination byte of a door -> the camera's cell column (TryEnterDoor: asl, rol x3 gives the
// scroll's high nibble, one more rol the page)
int cam_tile(uint8_t b) {
    unsigned c = b >> 7, a = (b << 1) & 0xFF;
    for (int i = 0; i < 3; i++) { const unsigned nc = a >> 7; a = ((a << 1) | c) & 0xFF; c = nc; }
    const unsigned page = ((a << 1) | c) & 0x0F;
    return int(page * 16 + (a >> 4));
}
int cam_byte(int tile) {
    for (int b = 0; b < 256; b++) if (cam_tile(uint8_t(b)) == tile) return b;
    return 0;
}

struct Maze {
    struct Door { int row, col; size_t dest, flags; };   // dest, flags: offsets of the entry's 2 bytes in prg
    std::vector<Door> doors;
    uint8_t type[MAZE_ROWS][MAZE_COLS] = {};
    int exit_row = -1, exit_col = -1;
    int boss = -1;
    std::vector<int> to;                                   // per door: the door it leads to, -1 = the boss arena
    std::vector<std::vector<int>> fwd, rev;                // walking moves between cells (node = row * 256 + col)
    std::string problem;

    static int node(int row, int col) { return row * MAZE_COLS + col; }
    bool solid(int r, int c) const { return type[r][c] == 4; }
    // the player is 2 cells tall (the door is 2 cells), so a cell needs the one above it free as well
    bool open(int r, int c) const { return r >= 0 && r < MAZE_ROWS && c >= 0 && c < MAZE_COLS && !solid(r, c) && (r == 0 || !solid(r - 1, c)); }
    bool standing(int r, int c) const { return r == MAZE_ROWS - 1 || solid(r + 1, c); }

    bool load(const std::vector<uint8_t>& prg, Game game);
    // nullptr = beatable with these targets (target[i] = door i leads to door target[i]; the boss door's is ignored)
    const char* judge(const std::vector<int>& target, int boss_door, int& hops_boss, int& hops_exit) const;
};

bool Maze::load(const std::vector<uint8_t>& prg, Game game) {
    const RemixAddrs& A = game == Game::Proto ? JP_ADDRS : US_ADDRS;
    auto rd = [&](int bank, uint16_t a) { return prg[at(bank, a)]; };
    auto word = [&](int bank, uint16_t a) { return uint16_t(rd(bank, a) | rd(bank, uint16_t(a + 1)) << 8); };
    if (prg.size() < 0x20000) { problem = "no PRG"; return false; }
    const uint16_t pos = word(2, uint16_t(A.door_pos + 2 * MAZE_ROOM)), dest = word(2, uint16_t(A.door_dest + 2 * MAZE_ROOM)),
                   flags = word(2, uint16_t(A.door_flags + 2 * MAZE_ROOM));
    for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2)
        doors.push_back({rd(2, uint16_t(pos + y)), rd(2, uint16_t(pos + y + 1)), at(2, uint16_t(dest + y)), at(2, uint16_t(flags + y))});
    if (doors.size() < 4) { problem = "too few doors"; return false; }

    // the map: tile type per cell
    const int tileset = rd(7, uint16_t(A.room_tileset + MAZE_ROOM)) * 2, bank = rd(7, uint16_t(A.room_data_bank + MAZE_ROOM));
    const uint16_t columns = word(7, uint16_t(A.tileset_columns + tileset)), screens = word(7, uint16_t(A.tileset_screens + tileset));
    const uint16_t types = word(7, uint16_t(A.room_collision + 2 * MAZE_ROOM));
    for (int m = 0; m < MAZE_COLS; m++) {
        const int column = rd(bank, uint16_t(screens + (m >> 4) * 16 + ((m & 15) >> 1) * 2));
        for (int r = 0; r < MAZE_ROWS; r++) type[r][m] = rd(7, uint16_t(types + rd(bank, uint16_t(columns + column * 24 + (m & 1) * 12 + r))));
    }
    for (int r = 0; r < MAZE_ROWS; r++)
        for (int c = 0; c < MAZE_COLS; c++)
            if (type[r][c] == 7 && r > exit_row) { exit_row = r; exit_col = c; }
    if (exit_row < 0 || !open(exit_row, exit_col) || !standing(exit_row, exit_col)) { problem = "no exit"; return false; }
    if (!open(MAZE_START_ROW, MAZE_START_COL)) { problem = "no start"; return false; }

    // the doors: each one is on a door tile; where each leads is read back from its entry
    for (size_t i = 0; i < doors.size(); i++) {
        const Door& d = doors[i];
        if (d.row < 0 || d.row >= MAZE_ROWS - 1 || d.col < 0 || d.col >= MAZE_COLS || type[d.row][d.col] != 6 || !open(d.row, d.col)) { problem = "a door is not on a door tile"; return false; }
        for (size_t j = 0; j < i; j++) if (doors[j].row == d.row && doors[j].col == d.col) { problem = "two doors on one cell"; return false; }
    }
    to.assign(doors.size(), -1);
    for (size_t i = 0; i < doors.size(); i++) {
        const Door& d = doors[i];
        if (prg[d.flags] & 0x80) {
            if (boss >= 0) { problem = "more than one boss door"; return false; }
            boss = int(i);
            continue;
        }
        if (prg[d.dest] != MAZE_ROOM) { problem = "a door leaves the room"; return false; }
        const int col = cam_tile(prg[d.dest + 1]) + (prg[d.flags + 1] & 15), row = prg[d.flags + 1] >> 4;
        for (size_t j = 0; j < doors.size(); j++) if (doors[j].row == row && doors[j].col == col) to[i] = int(j);
        if (to[i] < 0) { problem = "a door leads to no door"; return false; }
    }
    if (boss < 0) { problem = "no boss door"; return false; }

    fwd.assign(MAZE_CELLS, {});
    rev.assign(MAZE_CELLS, {});
    for (int r = 0; r < MAZE_ROWS; r++)
        for (int c = 0; c < MAZE_COLS; c++) {
            if (!open(r, c)) continue;
            const int u = node(r, c);
            auto add = [&](int r2, int c2) { fwd[u].push_back(node(r2, c2)); rev[node(r2, c2)].push_back(u); };
            if (open(r, c - 1)) add(r, c - 1);                        // walk
            if (open(r, c + 1)) add(r, c + 1);
            if (open(r + 1, c)) add(r + 1, c);                        // fall
            if (standing(r, c) && open(r - 1, c)) add(r - 1, c);      // a jump: one cell up, then walk on or fall
        }
    return true;
}

const char* Maze::judge(const std::vector<int>& target, int boss_door, int& hops_boss, int& hops_exit) const {
    // A door works from the cell it is on when the player stands there (the one that hangs in the air
    // can only be arrived at). The boss door leads to the arena and back: no warp.
    std::vector<int> wf(MAZE_CELLS, -1);
    std::vector<std::vector<int>> wr(MAZE_CELLS);
    for (size_t i = 0; i < doors.size(); i++) {
        if (int(i) == boss_door || !standing(doors[i].row, doors[i].col)) continue;
        const int a = node(doors[i].row, doors[i].col), b = node(doors[target[i]].row, doors[target[i]].col);
        wf[a] = b;
        wr[b].push_back(a);
    }
    auto reach = [&](int from, bool backward) {
        std::vector<char> seen(MAZE_CELLS, 0);
        std::vector<int> q{from};
        seen[from] = 1;
        for (size_t h = 0; h < q.size(); h++) {
            const int u = q[h];
            auto visit = [&](int v) { if (!seen[v]) { seen[v] = 1; q.push_back(v); } };
            for (int v : backward ? rev[u] : fwd[u]) visit(v);
            if (backward) { for (int v : wr[u]) visit(v); }
            else if (wf[u] >= 0) visit(wf[u]);
        }
        return seen;
    };
    // fewest door hops (walking is free)
    auto hops = [&](int from, int goal) {
        std::vector<int> dist(MAZE_CELLS, INT_MAX);
        std::deque<int> dq{from};
        dist[from] = 0;
        while (!dq.empty()) {
            const int u = dq.front(), d = dist[u];
            dq.pop_front();
            for (int v : fwd[u]) if (dist[v] > d) { dist[v] = d; dq.push_front(v); }
            if (wf[u] >= 0 && dist[wf[u]] > d + 1) { dist[wf[u]] = d + 1; dq.push_back(wf[u]); }
        }
        return dist[goal];
    };
    const int start = node(MAZE_START_ROW, MAZE_START_COL), exit = node(exit_row, exit_col);
    const Door& bd = doors[boss_door];
    const int bn = node(bd.row, bd.col);
    if (!standing(bd.row, bd.col)) return "the boss door has no floor";
    const std::vector<char> from_start = reach(start, false);
    if (!from_start[bn]) return "the boss door can't be reached";
    const std::vector<char> to_boss = reach(bn, true);
    for (int i = 0; i < MAZE_CELLS; i++) if (from_start[i] && !to_boss[i]) return "a place before the boss door has no way on";
    const std::vector<char> from_boss = reach(bn, false);
    if (!from_boss[exit]) return "the exit can't be reached";
    const std::vector<char> to_exit = reach(exit, true);
    for (int i = 0; i < MAZE_CELLS; i++) if (from_boss[i] && !to_exit[i]) return "a place after the boss door has no way on";
    hops_boss = hops(start, bn);
    hops_exit = hops(bn, exit);
    return nullptr;
}

std::string shuffle_maze(std::vector<uint8_t>& prg, Game game, Rng& rng) {
    Maze m;
    int h1 = 0, h2 = 0;
    if (!m.load(prg, game) || m.judge(m.to, m.boss, h1, h2)) return "";   // not the maze we know: leave it
    const int n = int(m.doors.size());
    if ((n - 2) % 2) return "";
    const Maze::Door old_boss = m.doors[m.boss];
    const uint8_t arena[4] = {prg[old_boss.dest], prg[old_boss.dest + 1], prg[old_boss.flags], prg[old_boss.flags + 1]};
    std::vector<std::array<uint8_t, 4>> saved;
    for (const auto& d : m.doors) saved.push_back({prg[d.dest], prg[d.dest + 1], prg[d.flags], prg[d.flags + 1]});
    auto shuffle = [&](std::vector<int>& v) { for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[rng.below(uint32_t(i))]); };

    for (int attempt = 0; attempt < 5000; attempt++) {
        // the boss door, one one-way door (leading to any door), the rest in pairs
        const int b = int(rng.below(uint32_t(n)));
        std::vector<int> rest;
        for (int i = 0; i < n; i++) if (i != b) rest.push_back(i);
        const int s = rest[rng.below(uint32_t(rest.size()))];
        std::vector<int> paired;
        for (int i : rest) if (i != s) paired.push_back(i);
        shuffle(paired);
        std::vector<int> target(n, -1);
        for (size_t k = 0; k + 1 < paired.size(); k += 2) { target[paired[k]] = paired[k + 1]; target[paired[k + 1]] = paired[k]; }
        target[s] = paired[rng.below(uint32_t(paired.size()))];
        if (m.judge(target, b, h1, h2) || h1 < MAZE_MIN_BOSS_HOPS || h2 < MAZE_MIN_EXIT_HOPS) continue;

        // write the entries: a door to door t arrives on t's cell (camera: t's column - 8, kept in range)
        for (int i = 0; i < n; i++) {
            const Maze::Door& d = m.doors[i];
            if (i == b) { for (int k = 0; k < 4; k++) prg[(k < 2 ? d.dest : d.flags) + (k & 1)] = arena[k]; continue; }
            const Maze::Door& t = m.doors[target[i]];
            const int cam = std::min(std::max(t.col - 8, 0), 240);
            prg[d.dest] = MAZE_ROOM;
            prg[d.dest + 1] = uint8_t(cam_byte(cam));
            prg[d.flags] = 0;
            prg[d.flags + 1] = uint8_t(t.row << 4 | (t.col - cam));
        }
        // read it back: it must be this maze, and beatable
        Maze v;
        bool good = v.load(prg, game) && v.boss == b && !v.judge(v.to, v.boss, h1, h2);
        for (int i = 0; good && i < n; i++) if (i != b && v.to[i] != target[i]) good = false;
        if (!good) {
            for (int i = 0; i < n; i++) {
                const Maze::Door& d = m.doors[i];
                prg[d.dest] = saved[i][0]; prg[d.dest + 1] = saved[i][1]; prg[d.flags] = saved[i][2]; prg[d.flags + 1] = saved[i][3];
            }
            return "Round 6: the door shuffle failed its check, doors unchanged\n";
        }
        char line[160];
        std::snprintf(line, sizeof line, "Round 6: doors shuffled\n");
        return line;
    }
    return "Round 6: no door shuffle found, doors unchanged\n";
}
}  // namespace

MazeCheck check_round6_doors(const std::vector<uint8_t>& prg, Game game) {
    MazeCheck r;
    Maze m;
    if (!m.load(prg, game)) { r.problem = m.problem; return r; }
    r.valid = true;
    r.to = m.to;
    r.boss = m.boss;
    r.boss_row = m.doors[m.boss].row;
    r.boss_col = m.doors[m.boss].col;
    const char* why = m.judge(m.to, m.boss, r.hops_to_boss, r.hops_to_exit);
    r.beatable = !why;
    if (why) r.problem = why;
    return r;
}

void RemixRuntime::reset() {
    for (auto& t : type_home) t = -1;
    for (auto& h : slot_home) h = -1;
    for (auto& h : piece_home) h = -1;
    for (auto& h : oam_home) h = -1;
    for (auto& h : piece_custom) h = -1;
    for (auto& h : oam_custom) h = -1;
    saved_round = -1;
    draw_home = -1;
}

// What the run-time part needs to know, read from the (not yet remixed) ROM data: each round's
// sprite graphics and colours, each enemy type's round, and each round's DoorId per boss slot.
static void read_tables(const std::vector<uint8_t>& prg, Game game, RemixRuntime& rt) {
    const RemixAddrs& A = game == Game::Proto ? JP_ADDRS : US_ADDRS;
    auto rd = [&](int bank, uint16_t a) { return prg[at(bank, a)]; };
    auto word = [&](int bank, uint16_t a) { return uint16_t(rd(bank, a) | rd(bank, uint16_t(a + 1)) << 8); };
    for (int room = 0; room < 16; room++) {
        rt.sprite_bank[room] = rd(7, uint16_t(A.room_chr0 + room));
        rt.bg_bank[room] = rd(7, uint16_t(A.room_chr0 + 16 + room));     // RoomChr1Banks follows RoomChr0Banks
        const uint8_t pal = rd(7, uint16_t(A.room_palette + room));
        for (int k = 0; k < 13; k++) rt.bg_pal[room][k] = rd(6, uint16_t(A.palettes + pal * 25 + k));
        for (int k = 0; k < 12; k++) rt.sprite_pal[room][k] = rd(6, uint16_t(A.palettes + pal * 25 + 13 + k));
        const int tileset = rd(7, uint16_t(A.room_tileset + room)) * 2;
        rt.layout[room] = {rd(7, uint16_t(A.room_data_bank + room)), word(7, uint16_t(A.tileset_columns + tileset)),
                           word(7, uint16_t(A.tileset_screens + tileset)), word(7, uint16_t(A.tileset_attrs + tileset)),
                           word(7, uint16_t(A.tileset_metatiles + tileset))};
    }
    for (int round = 0; round < 8; round++) {
        const uint16_t map = word(7, uint16_t(A.spawn_maps + 2 * round)), list = word(7, uint16_t(A.spawn_lists + 2 * round));
        int entries = 0;
        for (int i = 0; i < 256; i++) entries = std::max(entries, int(rd(7, uint16_t(map + i))));
        for (int k = 0; k < entries; k++) {
            const uint8_t t = rd(7, uint16_t(list + 2 * k));
            if (t && t < 64 && rt.type_home[t] < 0) rt.type_home[t] = int8_t(round);
        }
        // DoorId per boss slot: the flags' low nibble (usually the slot; round 6's one boss door has 0)
        for (int slot = 0; slot < 8; slot++) rt.door_id[round][slot] = int8_t(slot);
        const uint16_t pos = word(2, uint16_t(A.door_pos + 2 * round)), flags = word(2, uint16_t(A.door_flags + 2 * round));
        for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2) {
            const uint8_t f = rd(2, uint16_t(flags + y));
            if (f & 0x80) { rt.door_id[round][(f >> 4) & 7] = int8_t(f & 0x0F); rt.has_slot[round][(f >> 4) & 7] = true; }
        }
    }
    rt.tables = true;
}

std::string apply_bosses(std::vector<uint8_t>& prg, Game game, bool totem_pole, bool fix_round7) {
    const RemixAddrs& A = game == Game::Proto ? JP_ADDRS : US_ADDRS;
    auto rd = [&](int bank, uint16_t a) { return prg[at(bank, a)]; };
    auto word = [&](int bank, uint16_t a) { return uint16_t(rd(bank, a) | rd(bank, uint16_t(a + 1)) << 8); };
    std::string out;
    const uint16_t pos = word(2, uint16_t(A.door_pos + 2 * 6)), flags = word(2, uint16_t(A.door_flags + 2 * 6));
    // The prototype's unused Totem Pole (BossId $1B, round 7's 3rd boss): no door leads to him. Round
    // 7 gets its 3rd boss door back: the door where the US version has its 3rd boss (an empty room in
    // the prototype) becomes a 3rd-boss door. (Prototype only.)
    if (totem_pole && game == Game::Proto) {
        for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2)
            if (rd(2, uint16_t(pos + y)) == 0x04 && rd(2, uint16_t(pos + y + 1)) == 0xC2 && rd(2, uint16_t(flags + y)) == 0x80) {
                prg[at(2, uint16_t(flags + y))] = 0xB3;     // boss door, slot 3, DoorId 3
                out += "Bosses: Totem Pole behind round 7's 3rd boss door\n";
            }
    }
    // Round 7's key-steal bug: round 7 has 3 boss doors but b4_BossesPerRound says 2 (both versions).
    // Each boss win does `dec BossesLeft` (b5_BossDefeated_Reward) with no floor, and the exit and the
    // key icon need exactly 0, so beating the 3rd boss after the key takes BossesLeft to $FF: the key
    // is gone until a Game Over. The fix makes the count 3 (all three bosses needed). Only when round
    // 7 really has a 3rd boss door (the prototype has none without the Totem Pole).
    if (fix_round7) {
        bool third = false;
        for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2)
            if ((rd(2, uint16_t(flags + y)) & 0xF0) == 0xB0) third = true;
        const uint16_t count = uint16_t(A.bosses_per_round + 6);
        if (third && rd(4, count) == 2) { prg[at(4, count)] = 3; out += "Bosses: round 7 needs all 3 bosses (key-steal fix)\n"; }
    }
    return out;
}

std::string apply_remix(std::vector<uint8_t>& prg, Game game, uint32_t seed, bool cross, RemixRuntime& rt) {
    const RemixAddrs& A = game == Game::Proto ? JP_ADDRS : US_ADDRS;
    auto rd = [&](int bank, uint16_t a) { return prg[at(bank, a)]; };
    auto word = [&](int bank, uint16_t a) { return uint16_t(rd(bank, a) | rd(bank, uint16_t(a + 1)) << 8); };
    Rng rng{(uint64_t(seed) << 32) ^ 0x4D6F6E7374657250ull ^ (cross ? 0x5A5A5A5Aull : 0)};
    rt = RemixRuntime();
    read_tables(prg, game, rt);
    rt.cross = cross;
    std::string out;

    // ---- round 6's door maze (its own random numbers, so the rest of a seed's remix stays as it was).
    // Before the boss doors below, which find round 6's boss door by its flags.
    {
        Rng maze{(uint64_t(seed) << 32) ^ 0x446F6F724D617A65ull};
        out += shuffle_maze(prg, game, maze);
    }

    // ---- the spawn tables, and each round's enemy types
    struct Spawns { uint16_t list; int entries; std::vector<uint8_t> types; };
    Spawns sp[8];
    for (int round = 0; round < 8; round++) {
        const uint16_t map = word(7, uint16_t(A.spawn_maps + 2 * round));
        sp[round].list = word(7, uint16_t(A.spawn_lists + 2 * round));
        sp[round].entries = 0;
        for (int i = 0; i < 256; i++) sp[round].entries = std::max(sp[round].entries, int(rd(7, uint16_t(map + i))));
        for (int k = 0; k < sp[round].entries; k++) {
            uint8_t t = rd(7, uint16_t(sp[round].list + 2 * k));
            if (!t || t >= 64) continue;
            if (std::find(sp[round].types.begin(), sp[round].types.end(), t) == sp[round].types.end()) sp[round].types.push_back(t);
        }
    }

    // ---- mix rounds: deal the enemy types out to the rounds again. For each kind, every round's
    // types of that kind are shuffled together and handed back, so a round keeps its number of
    // enemy types (of each kind) but gets types from other rounds; each type ends up in one round.
    std::vector<uint8_t> dealt[8];
    if (cross) {
        Rng deal{(uint64_t(seed) << 32) ^ 0x456E656D79446561ull};
        for (Kind kind : {WALKER, FLYING, WATER, FIXED}) {
            std::vector<uint8_t> all;
            for (int r = 0; r < 8; r++) for (uint8_t t : sp[r].types) if (kind_of(game, t) == kind) all.push_back(t);
            for (size_t i = all.size(); i > 1; i--) std::swap(all[i - 1], all[deal.below(uint32_t(i))]);
            size_t next = 0;
            for (int r = 0; r < 8; r++) for (uint8_t t : sp[r].types) if (kind_of(game, t) == kind) dealt[r].push_back(all[next++]);
        }
    }

    // ---- per round: shuffle the boss doors, then re-pick the enemies (in this order, so a seed gives
    // the same remix as before mixing rounds existed)
    struct Door { size_t flags, dest; };
    std::vector<Door> doors[8];
    int changed[8] = {};
    for (int round = 0; round < 8; round++) {
        const uint16_t pos = word(2, uint16_t(A.door_pos + 2 * round));
        const uint16_t dest = word(2, uint16_t(A.door_dest + 2 * round));
        const uint16_t flags = word(2, uint16_t(A.door_flags + 2 * round));
        for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2)
            if (rd(2, uint16_t(flags + y)) & 0x80) doors[round].push_back({at(2, uint16_t(flags + y)), at(2, uint16_t(dest + y))});
        // which boss is behind which door: the flags bytes, shuffled among the round's boss doors
        std::vector<uint8_t> vals;
        for (auto& d : doors[round]) vals.push_back(prg[d.flags]);
        for (size_t i = vals.size(); i > 1; i--) std::swap(vals[i - 1], vals[rng.below(uint32_t(i))]);
        for (size_t i = 0; i < vals.size(); i++) prg[doors[round][i].flags] = vals[i];

        // enemy spawns: each entry's type re-picked from the round's types of the same kind
        for (int k = 0; k < sp[round].entries; k++) {
            uint8_t& t = prg[at(7, uint16_t(sp[round].list + 2 * k))];
            const Kind kind = kind_of(game, t);
            if (kind == HAZARD) continue;                  // traps and set pieces stay where they are
            std::vector<uint8_t> pool;                     // the round's own types, or the ones dealt to it
            for (uint8_t c : cross ? dealt[round] : sp[round].types) if (kind_of(game, c) == kind) pool.push_back(c);
            const uint8_t pick = pool[rng.below(uint32_t(pool.size()))];
            if (pick != t) { t = pick; changed[round]++; }
        }
    }

    // ---- mix rounds: boss slot s of each round comes from another round's slot s, with its arena
    int home[8][8];                                    // [round][slot] -> home round
    for (int r = 0; r < 8; r++) for (int s = 0; s < 8; s++) home[r][s] = r;
    if (cross) {
        for (int slot = 0; slot < 4; slot++) {
            std::vector<int> rounds;
            for (int r = 0; r < 8; r++)
                for (auto& d : doors[r])
                    if (((prg[d.flags] >> 4) & 7) == slot) { rounds.push_back(r); break; }
            std::vector<int> perm = rounds;
            for (size_t i = perm.size(); i > 1; i--) std::swap(perm[i - 1], perm[rng.below(uint32_t(i))]);
            for (size_t i = 0; i < rounds.size(); i++) home[rounds[i]][slot] = perm[i];
        }
        // The flags' low nibble becomes DoorId, which picks the part of the arena room's layout (and
        // spawn map) to use. It's usually the boss slot, but not everywhere (round 6's one boss door
        // has 0), so the door takes the value its boss's own round uses for that slot.
        for (int r = 0; r < 8; r++)
            for (auto& d : doors[r]) {
                const int slot = (prg[d.flags] >> 4) & 7, from = home[r][slot];
                prg[d.dest] = uint8_t(8 + from);
                prg[d.flags] = uint8_t((prg[d.flags] & 0xF0) | rt.door_id[from][slot]);
            }
    }

    return out;
}

void enable_remix_runtime(const std::vector<uint8_t>& prg, Game game, RemixRuntime& rt) {
    if (!rt.tables) read_tables(prg, game, rt);
    rt.cross = true;
}

// ---------------------------------------------------------------- debug: a chosen boss behind the first boss door
std::string debug_boss_door(std::vector<uint8_t>& prg, Game game, uint8_t boss, RemixRuntime& rt) {
    const RemixAddrs& A = game == Game::Proto ? JP_ADDRS : US_ADDRS;
    auto rd = [&](int bank, uint16_t a) { return prg[at(bank, a)]; };
    auto word = [&](int bank, uint16_t a) { return uint16_t(rd(bank, a) | rd(bank, uint16_t(a + 1)) << 8); };
    const int home = boss >> 2, slot = boss & 3;
    if (home > 7) return "boss door: only bosses $00-$1F (rounds 1-8) are behind doors\n";
    if (!rt.tables) read_tables(prg, game, rt);
    rt.cross = true;                                   // other rounds' arenas need the run-time part
    std::string out;
    for (int round = 0; round < 8; round++) {
        const uint16_t pos = word(2, uint16_t(A.door_pos + 2 * round));
        const uint16_t dest = word(2, uint16_t(A.door_dest + 2 * round));
        const uint16_t flags = word(2, uint16_t(A.door_flags + 2 * round));
        for (int y = 0; y < 254 && rd(2, uint16_t(pos + y)) != 0xFF; y += 2) {
            if (!(rd(2, uint16_t(flags + y)) & 0x80)) continue;
            // the slot keeps whatever boss count the round has: slot numbers it doesn't have still
            // work (the defeat flag for that slot is just never needed)
            prg[at(2, uint16_t(flags + y))] = uint8_t(0x80 | slot << 4 | (rt.door_id[home][slot] & 0x0F));
            prg[at(2, uint16_t(dest + y))] = uint8_t(8 + home);
            char line[120];
            std::snprintf(line, sizeof line, "round %d: first boss door -> boss $%02X (round %d's slot %d)\n", round + 1, boss, home + 1, slot);
            out += line;
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------- run time (mix rounds)
// The Totem Pole's shots (Cpu::totem_fire): how often he fires (a power of 2: FrameCounter is 8
// bits), and the shot's hit box (ObjHitboxH/W, from the centre: 6 = about the 16 x 16 head).
constexpr int TOTEM_FIRE_FRAMES = 128;               // about 2 seconds
constexpr uint8_t TOTEM_HEAD_HITBOX = 6;
constexpr int TOTEM_ANIM_FRAMES = 16;                // his own frame and the 2nd one, this many frames each
namespace {
// The arena of another round, if that's where the player is: its round, else -1.
// Only while playing: RoomId (and GameState) stay as they were on the game over / continue / title
// screens, and the menu cursors there are objects too. GameMode $08 = the player died, $80 = title.
bool playing(const uint8_t* ram) { return ram[GameState] == 5 && !(ram[GameMode] & 0x88); }
int foreign_arena(const uint8_t* ram, int real_round) {
    const int room = ram[RoomId];
    if (!playing(ram) || !(ram[BossRoomFlag] & 0x80)) return -1;
    if (room == 15 && real_round >= 8) return -1;   // round 9's final arena is room 15 too (round 8's): there it's at home
    return room >= 8 && room < 16 && room - 8 != real_round ? room - 8 : -1;
}
int real_round(const Nes& n) { return n.remix.saved_round >= 0 ? n.remix.saved_round : n.cpu.ram[RoundNumber]; }
}

// Scheduler, before a task runs (X = slot): a foreign task runs with its home round in RoundNumber.
void Cpu::remix_task_begin() {
    RemixRuntime& r = nes->remix;
    if (!r.cross || X >= 12) return;
    int home = playing(ram) ? r.slot_home[X] : -1;
    if (!playing(ram)) for (auto& h : r.slot_home) h = -1;   // (a new game starts with nothing foreign)
    const int arena = foreign_arena(ram, real_round(*nes));
    // a boss from another round, and its shots (slots 3-5); not a shot with an arena of its own (the
    // Totem Pole's bubbles), and not Bert's shots (slots 1-2): the player's, so like the player
    // (slot 0, which fires them with this round's pattern) they keep this round's (_data_d3c3)
    if (arena >= 0 && X >= 3 && X <= 9 && home < 8) home = arena;
    if (home < 0 || (home & 7) == ram[RoundNumber]) return;
    if (r.saved_round < 0) r.saved_round = ram[RoundNumber];
    ram[RoundNumber] = uint8_t(home & 7);
}

// Scheduler, on to the next task: the real round back.
void Cpu::remix_task_end() {
    RemixRuntime& r = nes->remix;
    if (r.saved_round < 0) return;
    ram[RoundNumber] = uint8_t(r.saved_round);
    r.saved_round = -1;
}

// StartTask is about to set up slot Y. An enemy spawned by the room task gets its type's home
// round; a task started by a foreign task (a shot, a drop) inherits that task's home round.
void Cpu::remix_task_started() {
    RemixRuntime& r = nes->remix;
    if (!r.cross || Y >= 12) return;
    int8_t home = -1;
    r.slot_custom[Y] = r.force_home >= 0 && r.force_custom;
    if (r.force_home >= 0) { r.slot_home[Y] = r.force_home; return; }
    const int creator = ram[CurrentTaskSlot];
    const int round = real_round(*nes);
    if (creator == 10 && Y >= 6 && Y <= 9 && !(ram[BossRoomFlag] & 0x80)) {
        const uint8_t type = ram[0x18];
        if (type < 64 && r.type_home[type] >= 0 && r.type_home[type] != round) home = r.type_home[type];
    } else if (r.saved_round >= 0 && creator < 12) {
        home = int8_t(ram[RoundNumber]);                  // started by a task running as another round
    }
    r.slot_home[Y] = home;
}

// DrawObject (Y = object): A = RoundNumber, which picks the round's metasprite set. Foreign
// objects use their home round's set (and get their pieces tagged for the renderer).
uint8_t Cpu::remix_draw_round(uint8_t a) {
    RemixRuntime& r = nes->remix;
    r.draw_home = -1;
    r.draw_obj = int8_t(Y < 16 ? Y : -1);
    r.draw_custom = false;
    if (r.cross && Y < 12 && r.slot_custom[Y] && playing(ram)) {
        // a head shot: 16 x 16, so it hits like it (the bubble's own box is smaller)
        r.draw_custom = true;
        r.obj_custom_kind[Y] = 1;
        ram[ObjHitboxH + Y] = TOTEM_HEAD_HITBOX;
        ram[ObjHitboxW + Y] = TOTEM_HEAD_HITBOX;
    } else if (r.totem_attack && Y == r.totem_obj && nes->frame_count - r.totem_seen <= 2 &&
               (nes->frame_count / TOTEM_ANIM_FRAMES) & 1) {
        // the 'finished' Totem Pole, every other TOTEM_ANIM_FRAMES: the user's 2nd frame (only
        // while his handler runs, so not once he's beaten)
        r.draw_custom = true;
        r.obj_custom_kind[Y] = 2;
    }
    if (!r.cross) return a;
    const int round = real_round(*nes);
    const int arena = foreign_arena(ram, round);
    if (Y < 10 && r.slot_home[Y] >= 8 && playing(ram)) { r.draw_home = r.slot_home[Y]; return uint8_t(r.draw_home & 7); }   // the Totem Pole's bubbles
    if (arena >= 0) {
        // That arena's own graphics are loaded. The player keeps this round's arena colours (and
        // sprite bank), to go with the arena look the renderer gives the background.
        if (Y == 0) r.draw_home = int8_t(8 + round);
        return uint8_t(arena);
    }
    if (Y < 10 && r.slot_home[Y] >= 0 && playing(ram)) { r.draw_home = r.slot_home[Y]; return uint8_t(r.draw_home & 7); }
    return uint8_t(round);
}

void Cpu::remix_draw_untagged() { nes->remix.draw_home = -1; nes->remix.draw_obj = -1; nes->remix.draw_custom = false; }

// Prototype only (translate.py BOSS_HOOKS): the unused Totem Pole's handler, before its last call.
// Not in the original game: every TOTEM_FIRE_FRAMES frames he fires a shot at the player from the
// top of his sprite: the Man-Eating Plant's bubble (shot kind 0 or 1, as the plant picks them), so
// it moves and hits like one. The shot task is given round 1's arena as its home (force_home), so
// it runs as round 1's, and force_custom has the renderer draw it as the port's Totem Pole head
// (custom_sprites.h) instead of the bubble. Returns true to have the translated code jsr
// b5_BossFireProjectile; that returns to the same place, where this puts everything back.
bool Cpu::totem_fire() {
    RemixRuntime& r = nes->remix;
    const int obj = ram[CurrentTaskSlotId];
    r.totem_obj = int8_t(obj < 16 ? obj : -1);        // for his 2nd animation frame (remix_draw_round)
    r.totem_seen = nes->frame_count;
    if (r.totem_returning) {
        r.totem_returning = false;
        r.force_home = -1;
        r.force_custom = false;
        A = r.totem_saved[0]; X = r.totem_saved[1]; Y = r.totem_saved[2];
        if (obj < 16) ram[ObjY + obj] = r.totem_saved[3];
        return false;
    }
    if (!r.totem_attack || !r.cross || (ram[FrameCounter] & (TOTEM_FIRE_FRAMES - 1)) != TOTEM_FIRE_FRAMES / 2 || obj >= 16) return false;
    r.totem_saved[0] = A; r.totem_saved[1] = X; r.totem_saved[2] = Y; r.totem_saved[3] = ram[ObjY + obj];
    // b5_BossFireProjectile puts kind 0/1 shots 20 px above ObjY (and 12 px towards the facing
    // side), so aim from his top: ObjY for the call = the top of his pieces (last frame) + 20
    if (r.obj_top_gen[obj] + 2 >= nes->oam_gen && r.obj_top[obj] < 0xF0)
        ram[ObjY + obj] = uint8_t(std::min(0xEF, r.obj_top[obj] + 20 + 4));
    // as b5_Boss01_ManEatingPlant: facing the player ($0C = player to the left), kind 0 or 1 at random
    const uint8_t facing = ram[ObjX + obj] >= ram[ObjX] ? 0x0C : 0x04;
    A = uint8_t(((ram[RngState] & 0x10) + 0x30) | facing);
    X = uint8_t((A >> 4) - 3);
    Y = uint8_t(obj);
    r.force_home = 8;                                  // round 1's arena (the Man-Eating Plant's)
    r.force_custom = true;                             // drawn as his head
    r.totem_returning = true;
    return true;
}

bool Nes::remix_foreign_arena(int& real, int& arena) const {
    if (!remix.cross) return false;
    real = real_round(*this);
    if (real < 0 || real > 7) return false;
    arena = foreign_arena(cpu.ram, real);
    return arena >= 0;
}

// RoundNumber * 4 + BossIdInRound (BossId, the boss music, the speech bubble): in another
// round's arena, that round's.
uint8_t Cpu::remix_arena_round(uint8_t a) {
    if (!nes->remix.cross) return a;
    const int arena = foreign_arena(ram, real_round(*nes));
    return arena >= 0 ? uint8_t(arena) : a;
}

int remix_enemy_kind(Game game, uint8_t type) { return kind_of(game, type); }

uint32_t new_remix_seed() {
    uint32_t s = uint32_t(SDL_GetTicksNS() * 0x9E3779B97F4A7C15ull >> 32) ^ uint32_t(SDL_rand_bits());
    return s ? s : 1;
}

std::string remix_seed_text(uint32_t seed) {
    char b[16];
    std::snprintf(b, sizeof b, "%04X-%04X", seed >> 16, seed & 0xFFFF);
    return b;
}

bool parse_remix_seed(const std::string& text, uint32_t& seed) {
    uint32_t v = 0;
    int digits = 0;
    for (char c : text) {
        if (c == '-' || c == ' ') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c)) || ++digits > 8) return false;
        v = v << 4 | uint32_t(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::toupper(c) - 'A' + 10));
    }
    if (!digits) return false;
    seed = v;
    return true;
}

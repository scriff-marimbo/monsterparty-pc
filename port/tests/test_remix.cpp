// Remix Mode: the data it edits, and play with it on.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <map>
#include <set>
#include "ram_symbols.h"
#include "rom_data.h"
#include "support.h"

using namespace mptest;

namespace {
// every boss of rounds 1-8 behind a door: (destination arena, slot); and per round the slots it has
// (slot 0 = an empty boss room, which may lead to any arena)
using BossDoor = std::pair<int, int>;
std::multiset<BossDoor> boss_doors(const std::vector<uint8_t>& prg, Game g) {
    std::multiset<BossDoor> out;
    for (int r = 0; r < 8; r++)
        for (const Door& d : doors(prg, g, r))
            if ((d.flags & 0x80) && (d.flags & 0x70)) out.insert({d.dest, (d.flags >> 4) & 7});   // (not the empty rooms)
    return out;
}
std::multiset<int> round_slots(const std::vector<uint8_t>& prg, Game g, int r) {
    std::multiset<int> out;
    for (const Door& d : doors(prg, g, r)) if (d.flags & 0x80) out.insert((d.flags >> 4) & 7);
    return out;
}
std::vector<uint8_t> remixed(Game g, uint32_t seed, bool totem = false) {
    std::vector<uint8_t> prg = load_prg(g);
    apply_bosses(prg, g, totem, false);
    RemixRuntime rt;
    apply_remix(prg, g, seed, true, rt);
    return prg;
}
const uint32_t SEEDS[] = {0x1A2B3C4D, 0x00000001, 0xDEADBEEF, 0x12345678, 0xFFFFFFFF};
}  // namespace

TEST_CASE("remix seeds are written and read back", "[remix]") {
    for (uint32_t s : SEEDS) {
        uint32_t back = 0;
        REQUIRE(parse_remix_seed(remix_seed_text(s), back));
        CHECK(back == s);
    }
    CHECK(remix_seed_text(0x1A2B3C4D) == "1A2B-3C4D");
    uint32_t v;
    CHECK(parse_remix_seed("1a2b3c4d", v));
    CHECK(v == 0x1A2B3C4D);
    CHECK(parse_remix_seed("1A2B 3C4D", v));
    CHECK(parse_remix_seed("1A2B-3C4", v));    // fewer digits are read as a smaller number
    CHECK(v == 0x01A2B3C4);
    CHECK_FALSE(parse_remix_seed("1A2B-3C4D-5", v));
    CHECK_FALSE(parse_remix_seed("XYZ", v));
    CHECK_FALSE(parse_remix_seed("", v));
}

TEST_CASE("a seed always gives the same remix", "[remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        for (uint32_t s : SEEDS) CHECK(remixed(g, s) == remixed(g, s));
        CHECK(remixed(g, SEEDS[0]) != remixed(g, SEEDS[1]));
        CHECK(remixed(g, SEEDS[0]) != load_prg(g));
    }
}

TEST_CASE("remix edits only door destinations and flags, and spawn types", "[remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> orig = load_prg(g);
        const std::vector<uint8_t> mix = remixed(g, SEEDS[0]);
        // allowed bytes
        std::set<size_t> ok;
        const Addrs A = addrs(g);
        for (int r = 0; r < 8; r++) {
            const uint16_t dest = prg_word(orig, 2, uint16_t(A.door_dest + 2 * r));
            const uint16_t flags = prg_word(orig, 2, uint16_t(A.door_flags + 2 * r));
            for (size_t i = 0; i < doors(orig, g, r).size(); i++) {
                ok.insert(prg_at(2, uint16_t(dest + 2 * i)));
                ok.insert(prg_at(2, uint16_t(flags + 2 * i)));
                if (r == 5) {                      // round 6's door maze also changes where a door arrives
                    ok.insert(prg_at(2, uint16_t(dest + 2 * i + 1)));
                    ok.insert(prg_at(2, uint16_t(flags + 2 * i + 1)));
                }
            }
            const uint16_t list = prg_word(orig, 7, uint16_t(A.spawn_lists + 2 * r));
            for (size_t k = 0; k < spawn_types(orig, g, r).size(); k++) ok.insert(prg_at(7, uint16_t(list + 2 * k)));
        }
        size_t changed = 0;
        for (size_t i = 0; i < orig.size(); i++)
            if (orig[i] != mix[i]) { changed++; INFO("PRG offset " << i); CHECK(ok.count(i)); }
        CHECK(changed > 0);
    }
}

TEST_CASE("remix keeps every boss once, and each round's boss slots", "[remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        for (bool totem : {false, true}) {
            if (totem && g != Game::Proto) continue;
            std::vector<uint8_t> orig = load_prg(g);
            apply_bosses(orig, g, totem, false);
            for (uint32_t s : SEEDS) {
                INFO("seed " << remix_seed_text(s) << (totem ? " with the Totem Pole" : ""));
                const std::vector<uint8_t> mix = remixed(g, s, totem);
                CHECK(boss_doors(mix, g) == boss_doors(orig, g));
                for (int r = 0; r < 8; r++) CHECK(round_slots(mix, g, r) == round_slots(orig, g, r));
                // a boss door leads to an arena (RoomId 8-15), and its DoorId matches the arena's slot
                for (int r = 0; r < 8; r++)
                    for (const Door& d : doors(mix, g, r))
                        if (d.flags & 0x80) { CHECK(d.dest >= 8); CHECK(d.dest < 16); }
            }
        }
    }
}

TEST_CASE("remix swaps enemy types only for the same kind, each type in one round", "[remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> orig = load_prg(g);
        for (uint32_t s : SEEDS) {
            INFO("seed " << remix_seed_text(s));
            const std::vector<uint8_t> mix = remixed(g, s);
            std::map<int, std::set<uint8_t>> all_before, all_after;   // kind -> types
            std::map<int, std::multiset<size_t>> counts_before, counts_after;   // kind -> types per round
            for (int r = 0; r < 8; r++) {
                const auto a = spawn_types(orig, g, r), b = spawn_types(mix, g, r);
                REQUIRE(a.size() == b.size());
                std::map<int, std::set<uint8_t>> ra, rb;
                for (size_t k = 0; k < a.size(); k++) {
                    const int kind = remix_enemy_kind(g, a[k]);
                    CHECK(remix_enemy_kind(g, b[k]) == kind);
                    if (kind == 0) CHECK(a[k] == b[k]);  // hazards and set pieces stay
                    all_before[kind].insert(a[k]); all_after[kind].insert(b[k]);
                    ra[kind].insert(a[k]); rb[kind].insert(b[k]);
                }
                for (auto& [kind, t] : ra) counts_before[kind].insert(t.size());
                for (auto& [kind, t] : rb) counts_after[kind].insert(t.size());
            }
            CHECK(all_after == all_before);
            CHECK(counts_after == counts_before);
        }
    }
}

// ---- round 6's door maze
namespace {
// the camera byte of a door's destination -> the camera's cell column (as TryEnterDoor decodes it)
int camera_cell(uint8_t b) {
    unsigned c = b >> 7, a = (b << 1) & 0xFF;
    for (int i = 0; i < 3; i++) { const unsigned nc = a >> 7; a = ((a << 1) | c) & 0xFF; c = nc; }
    return int((((a << 1) | c) & 0x0F) * 16 + (a >> 4));
}
}  // namespace

TEST_CASE("round 6's own doors: 27 pairs, a one-way door and the boss door, and it can be beaten", "[remix]") {
    // (door numbers in the table's order) the pairs of the original maze, one-way 52 -> 3, boss door 54
    const int pairs[27][2] = {{0, 4}, {1, 7}, {2, 9}, {3, 6}, {5, 10}, {8, 15}, {11, 14}, {12, 13}, {16, 27}, {17, 18}, {19, 24}, {20, 25}, {21, 26},
                              {22, 31}, {23, 28}, {29, 53}, {30, 41}, {32, 33}, {34, 45}, {35, 39}, {36, 40}, {37, 49}, {38, 42}, {43, 50}, {44, 46},
                              {47, 55}, {48, 51}};
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> orig = load_prg(g);
        const MazeCheck c = check_round6_doors(orig, g);
        REQUIRE(c.valid);
        REQUIRE(c.to.size() == 56);
        for (auto& p : pairs) { CHECK(c.to[p[0]] == p[1]); CHECK(c.to[p[1]] == p[0]); }
        CHECK(c.to[52] == 3);
        CHECK(c.boss == 54);
        CHECK(c.beatable);
        // every door's entry is what the shuffle would write for its target: the camera at the target's
        // column - 8 (kept in 0-240), and the target's row and offset in the arrival byte
        const Addrs A = addrs(g);
        const uint16_t pos = prg_word(orig, 2, uint16_t(A.door_pos + 10)), dest = prg_word(orig, 2, uint16_t(A.door_dest + 10)),
                       flags = prg_word(orig, 2, uint16_t(A.door_flags + 10));
        for (int i = 0; i < 56; i++) {
            if (i == 54) continue;
            const int tr = prg_rd(orig, 2, uint16_t(pos + 2 * c.to[i])), tc = prg_rd(orig, 2, uint16_t(pos + 2 * c.to[i] + 1));
            const int cam = camera_cell(prg_rd(orig, 2, uint16_t(dest + 2 * i + 1)));
            const int arr = prg_rd(orig, 2, uint16_t(flags + 2 * i + 1));
            INFO("door " << i);
            CHECK(cam == std::min(std::max(tc - 8, 0), 240));
            CHECK(arr == (tr << 4 | (tc - cam)));
            CHECK(prg_rd(orig, 2, uint16_t(dest + 2 * i)) == 5);
        }
    }
}

TEST_CASE("remix shuffles round 6's doors, and the round can always be beaten", "[remix]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> orig = load_prg(g);
        std::set<int> boss_doors_seen;
        int changed = 0;
        for (uint32_t k = 1; k <= 100; k++) {
            const uint32_t seed = k * 2654435761u;
            INFO("seed " << remix_seed_text(seed));
            const std::vector<uint8_t> mix = remixed(g, seed);
            const MazeCheck c = check_round6_doors(mix, g);
            REQUIRE(c.valid);
            CHECK(c.beatable);                      // boss door and exit reachable, nowhere to get stuck
            CHECK(c.hops_to_boss >= 1);             // not right at the start
            CHECK(c.hops_to_exit >= 2);
            // still one boss door, one one-way door, and the rest in pairs
            int mutual = 0, one_way = 0;
            for (size_t i = 0; i < c.to.size(); i++) {
                if (int(i) == c.boss) continue;
                if (c.to[c.to[i]] == int(i) && c.to[i] != c.boss) mutual++;
                else one_way++;
            }
            CHECK(c.to.size() == 56);
            CHECK(mutual == 54);
            CHECK(one_way == 1);
            boss_doors_seen.insert(c.boss);
            if (c.boss != 54) changed++;
            // the exit's tile and the door positions are not touched
            CHECK(doors(mix, g, 5).size() == 56);
            const auto before = doors(orig, g, 5), after = doors(mix, g, 5);
            for (size_t i = 0; i < before.size(); i++) { CHECK(before[i].x == after[i].x); CHECK(before[i].y == after[i].y); }
        }
        CHECK(boss_doors_seen.size() >= 20);        // the boss door really moves around
        CHECK(changed >= 90);
    }
}

TEST_CASE("the game plays with Remix on", "[game][remix][golden]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        Options o;
        o.remix = true;
        o.remix_seed = SEEDS[0];
        Result a = run(g, play_round1(), o);
        CHECK(a.ram[GameState] == 5);
        CHECK(a.ram[RoundNumber] == 0);         // the real round stays (not a foreign task's)
        CHECK(a.ram_hash == run(g, play_round1(), o).ram_hash);
        CHECK(a.ram_hash == golden(std::string(g == Game::Proto ? "proto" : "us") + ".remix_1A2B3C4D.play_round1", a.ram_hash));
    }
}

TEST_CASE("--boss-door puts any boss behind round 1's first boss door", "[game][remix]") {
    struct Case { Game g; uint8_t boss; };
    for (Case c : {Case{Game::US, 0x01}, Case{Game::US, 0x0D}, Case{Game::US, 0x15}, Case{Game::US, 0x1A},
                   Case{Game::Proto, 0x0D}, Case{Game::Proto, 0x1B}}) {
        REQUIRE_ROM(c.g);
        INFO((c.g == Game::Proto ? "proto" : "us") << " boss $" << std::hex << int(c.boss));
        Options o;
        o.boss_door = c.boss;
        Result r = run(c.g, enter_first_boss_door(), o);
        CHECK((r.ram[BossRoomFlag] & 0x80));
        CHECK(r.ram[RoomId] == 8 + (c.boss >> 2));
        CHECK(r.ram[BossId] == c.boss);
        CHECK(r.ram[RoundNumber] == 0);
        CHECK(r.is_cheating);                   // a debug option
    }
}

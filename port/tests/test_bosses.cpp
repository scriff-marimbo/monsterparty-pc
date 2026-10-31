// The launcher's Bosses options (apply_bosses) and the 'finished' Totem Pole.
#include <catch2/catch_test_macros.hpp>
#include "ram_symbols.h"
#include "rom_data.h"
#include "support.h"

using namespace mptest;

namespace {
std::vector<size_t> differences(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<size_t> out;
    for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) out.push_back(i);
    return out;
}
int boss_doors_in_round7(const std::vector<uint8_t>& prg, Game g, int slot) {
    int n = 0;
    for (const Door& d : doors(prg, g, 6)) n += (d.flags & 0x80) && ((d.flags >> 4) & 7) == slot;
    return n;
}
}  // namespace

TEST_CASE("round 7 needs 2 bosses in both versions, and has 3 boss doors in the US one", "[bosses]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        const std::vector<uint8_t> prg = load_prg(g);
        CHECK(prg_rd(prg, 4, uint16_t(addrs(g).bosses_per_round + 6)) == 2);
        CHECK(boss_doors_in_round7(prg, g, 3) == (g == Game::US ? 1 : 0));
    }
}

TEST_CASE("with nothing chosen, apply_bosses changes nothing", "[bosses]") {
    for (Game g : {Game::US, Game::Proto}) {
        REQUIRE_ROM(g);
        std::vector<uint8_t> prg = load_prg(g);
        CHECK(apply_bosses(prg, g, false, false).empty());
        CHECK(prg == load_prg(g));
    }
}

TEST_CASE("the Totem Pole gets round 7's 3rd boss door (prototype only)", "[bosses]") {
    REQUIRE_ROM(Game::Proto);
    const std::vector<uint8_t> orig = load_prg(Game::Proto);
    std::vector<uint8_t> prg = orig;
    apply_bosses(prg, Game::Proto, true, false);
    const auto diff = differences(orig, prg);
    REQUIRE(diff.size() == 1);
    CHECK(prg[diff[0]] == 0xB3);               // boss door, slot 3, DoorId 3
    CHECK(orig[diff[0]] == 0x80);              // was an empty room
    CHECK(boss_doors_in_round7(prg, Game::Proto, 3) == 1);
    // the count stays 2: as in the US version (the key-steal bug comes with it)
    CHECK(prg_rd(prg, 4, uint16_t(addrs(Game::Proto).bosses_per_round + 6)) == 2);

    REQUIRE_ROM(Game::US);
    std::vector<uint8_t> us = load_prg(Game::US);
    apply_bosses(us, Game::US, true, false);
    CHECK(us == load_prg(Game::US));           // nothing to do in the US version
}

TEST_CASE("the round 7 key-steal fix makes round 7 need 3 bosses", "[bosses]") {
    REQUIRE_ROM(Game::US);
    const std::vector<uint8_t> orig = load_prg(Game::US);
    std::vector<uint8_t> prg = orig;
    apply_bosses(prg, Game::US, false, true);
    const auto diff = differences(orig, prg);
    REQUIRE(diff.size() == 1);
    CHECK(diff[0] == prg_at(4, uint16_t(addrs(Game::US).bosses_per_round + 6)));
    CHECK(prg[diff[0]] == 3);

    // the prototype's round 7 has only 2 boss doors: 3 would make it impossible, so no change...
    REQUIRE_ROM(Game::Proto);
    std::vector<uint8_t> jp = load_prg(Game::Proto);
    apply_bosses(jp, Game::Proto, false, true);
    CHECK(jp == load_prg(Game::Proto));
    // ...unless the Totem Pole gives it a 3rd
    apply_bosses(jp, Game::Proto, true, true);
    CHECK(prg_rd(jp, 4, uint16_t(addrs(Game::Proto).bosses_per_round + 6)) == 3);
}

TEST_CASE("the 'finished' Totem Pole fights and shoots heads", "[game][bosses]") {
    REQUIRE_ROM(Game::Proto);
    Options o;
    o.boss_door = 0x1B;
    o.totem_finished = true;
    Script s = enter_first_boss_door();
    s.frames = 2400;                           // ~10 shots' worth of frames in the arena
    Result r = run(Game::Proto, s, o);
    CHECK(r.ram[BossId] == 0x1B);
    CHECK((r.ram[BossRoomFlag] & 0x80));
    CHECK(r.totem_obj >= 0);                   // his handler ran
    // a shot was fired: some shot slot was given round 1's arena as its home
    bool shot = false;
    for (int i = 0; i < 12; i++) shot |= r.nes->remix.slot_custom[i];
    CHECK(shot);
    // and without the option, the same fight has no shots
    o.totem_finished = false;
    Result plain = run(Game::Proto, s, o);
    for (int i = 0; i < 12; i++) CHECK_FALSE(plain.nes->remix.slot_custom[i]);
}

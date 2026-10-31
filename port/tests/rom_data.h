// Reading the game's data tables from a PRG image, for the tests (the same addresses as remix.cpp).
#pragma once
#include <cstdint>
#include <tuple>
#include <vector>
#include "nes.h"

namespace mptest {

inline size_t prg_at(int bank, uint16_t addr) { return size_t(bank) * 0x4000 + (addr & 0x3FFF); }
inline uint8_t prg_rd(const std::vector<uint8_t>& prg, int bank, uint16_t a) { return prg[prg_at(bank, a)]; }
inline uint16_t prg_word(const std::vector<uint8_t>& prg, int bank, uint16_t a) {
    return uint16_t(prg_rd(prg, bank, a) | prg_rd(prg, bank, uint16_t(a + 1)) << 8);
}

struct Addrs { uint16_t door_pos, door_dest, door_flags, spawn_maps, spawn_lists, bosses_per_round; };
inline Addrs addrs(Game g) {
    return g == Game::Proto ? Addrs{0x98AE, 0x9A20, 0x9B82, 0xEC28, 0xEC48, 0x822C}
                            : Addrs{0x98AE, 0x9A26, 0x9B8E, 0xED69, 0xED89, 0x945B};
}

// A round's doors (bank 2): position (2 bytes), destination RoomId, flags (bit 7 boss door, bits
// 4-6 boss slot, low nibble DoorId).
struct Door { uint8_t x, y, dest, flags; };
inline std::vector<Door> doors(const std::vector<uint8_t>& prg, Game g, int room) {
    const Addrs A = addrs(g);
    const uint16_t pos = prg_word(prg, 2, uint16_t(A.door_pos + 2 * room));
    const uint16_t dest = prg_word(prg, 2, uint16_t(A.door_dest + 2 * room));
    const uint16_t flags = prg_word(prg, 2, uint16_t(A.door_flags + 2 * room));
    std::vector<Door> out;
    for (int y = 0; y < 254 && prg_rd(prg, 2, uint16_t(pos + y)) != 0xFF; y += 2)
        out.push_back({prg_rd(prg, 2, uint16_t(pos + y)), prg_rd(prg, 2, uint16_t(pos + y + 1)),
                       prg_rd(prg, 2, uint16_t(dest + y)), prg_rd(prg, 2, uint16_t(flags + y))});
    return out;
}

// A round's spawn entries (bank 7): the enemy type of each entry the round's spawn map uses.
inline std::vector<uint8_t> spawn_types(const std::vector<uint8_t>& prg, Game g, int round) {
    const Addrs A = addrs(g);
    const uint16_t map = prg_word(prg, 7, uint16_t(A.spawn_maps + 2 * round));
    const uint16_t list = prg_word(prg, 7, uint16_t(A.spawn_lists + 2 * round));
    int entries = 0;
    for (int i = 0; i < 256; i++) entries = std::max(entries, int(prg_rd(prg, 7, uint16_t(map + i))));
    std::vector<uint8_t> out;
    for (int k = 0; k < entries; k++) out.push_back(prg_rd(prg, 7, uint16_t(list + 2 * k)));
    return out;
}

}  // namespace mptest

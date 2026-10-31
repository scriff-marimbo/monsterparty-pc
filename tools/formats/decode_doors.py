#!/usr/bin/env python3
"""Decodes a room's door warps from a ROM and shows which door leads where.

    python3 tools/formats/decode_doors.py ROM.nes [ROOM_ID] [--compare OTHER_ROM.nes]

ROOM_ID is the RoomId (0-7 are the rounds' main levels, so round 6 is 5; the default). The ROM is the
US release or the Japanese prototype, told apart by the CRC32 of PRG + CHR.

The tables (bank 2, one word per RoomId pointing to a list with 2 bytes per door, as the game's
TryEnterDoor / _func_da26 reads them):
  position  : tile row (ObjY >> 4), then column = page * 16 + (x >> 4); the list ends with $FF
  destination: RoomId ($FF = back to where the player came from), then the camera: page (low nibble) and
               scroll (high nibble * 16, after the game's asl/rol shuffle)
  flags     : bit 7 = boss door (bits 4-6 = boss slot), low nibble = DoorId; then the arrival tile,
               row in the high nibble, column on the screen in the low nibble ($FF = where you left)
A door's partner is the door at the tile you arrive on. The US and the prototype read the tables the
same way, but the prototype adds 8 to the arrival Y where the US adds 4.
"""
import sys
import zlib

ADDR = {  # (positions, destinations, flags) in bank 2, by ROM: found by comparing the two
    'us': (0x98AE, 0x9A26, 0x9B8E),
    'jp': (0x98AE, 0x9A20, 0x9B82),
}


CRC = {0x02B9E7C2: 'us', 0x21E28F50: 'jp'}   # CRC32 of PRG + CHR


def load(path):
    d = open(path, 'rb').read()
    ver = CRC.get(zlib.crc32(d[16:16 + 0x40000]))
    if ver is None:
        sys.exit(f'{path}: not the US release or the Japanese prototype')
    return d[16:16 + 0x20000], ver


def rd(prg, a):
    return prg[2 * 0x4000 + (a & 0x3FFF)]


def word(prg, a):
    return rd(prg, a) | rd(prg, a + 1) << 8


def camera(b):
    """The destination's second byte -> (page, scroll), as the code at $D987 does."""
    c, a = b >> 7, (b << 1) & 0xFF
    for _ in range(3):
        c, a = a >> 7, ((a << 1) | c) & 0xFF
    x = a
    return (((x << 1) | c) & 0x0F), x & 0xF0


def doors(prg, ver, room):
    P, D, F = ADDR[ver]
    pos, dst, fl = (word(prg, t + 2 * room) for t in (P, D, F))
    out, i = [], 0
    while rd(prg, pos + i) != 0xFF:
        row, col = rd(prg, pos + i), rd(prg, pos + i + 1)
        e = dict(i=i // 2, row=row, tile=col, room=rd(prg, dst + i), cam=rd(prg, dst + i + 1),
                 flags=rd(prg, fl + i), arr=rd(prg, fl + i + 1))
        out.append(e)
        i += 2
    by_tile = {(e['row'], e['tile']): e['i'] for e in out}
    for e in out:
        if e['room'] == 0xFF:
            e['to'] = 'back'
        elif e['room'] != room:
            e['to'] = f"room {e['room']}"
        else:
            page, scroll = camera(e['cam'])
            x = page * 256 + scroll + (e['arr'] & 15) * 16 + 8
            e['to'] = by_tile.get((e['arr'] >> 4, x >> 4), '?')
    return out


def show(ds):
    inc = {e['to'] for e in ds if isinstance(e['to'], int)}
    for e in ds:
        t = e['to']
        note = ''
        if isinstance(t, int):
            note = 'pair' if ds[t]['to'] == e['i'] else 'ONE-WAY'
        elif e['flags'] & 0x80:
            note = f"boss door, slot {(e['flags'] >> 4) & 7}"
        if e['i'] not in inc and not note.startswith('boss'):
            note += ' (nothing leads here)'
        print(f"#{e['i']:2d}  row {e['row']:2d} page {e['tile'] >> 4:2d} tile {e['tile'] & 15:2d}  ->  {t}  {note}")


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rom = args[0]
    room = int(args[1]) if len(args) > 1 else 5
    prg, ver = load(rom)
    ds = doors(prg, ver, room)
    show(ds)
    if '--compare' in sys.argv:
        other = sys.argv[sys.argv.index('--compare') + 1]
        prg2, ver2 = load(other)
        ds2 = doors(prg2, ver2, room)
        same = len(ds) == len(ds2)
        print(f'\ncompare with {other}: {len(ds)} vs {len(ds2)} doors')
        for a, b in zip(ds, ds2):
            for k in ('row', 'tile', 'room', 'cam', 'flags', 'arr', 'to'):
                if a[k] != b[k]:
                    same = False
                    print(f"  door #{a['i']}: {k} {a[k]} vs {b[k]}")
        print('  identical' if same else '')

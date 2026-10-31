#!/usr/bin/env python3
"""Decode Monster Party's object/boss sprite definitions (metasprites) from your ROM into JSON (data only).

usage: decode_sprites.py ROM.nes OUT.json

Format (see level_and_sprite_formats.md). All of it lives in PRG bank 3 (ObjectGfxBank):
  $8000: 18 pointers, one per graphics set.  set = RoundNumber*2 + (ObjDrawFlags & 1)
         even sets all point to the shared set (player, items, common enemies);
         odd sets are the round's own set (that round's enemies and bosses).
  set:   table of 2-byte pointers, one per frame (ObjFrame)
  frame: list of chunks, ended by $00
           n = $01-$7F : n pieces follow, 4 bytes each: dY, dX (signed), tile, attributes
           n = $80-$BF : pointer to another chunk list (this byte = high byte, next byte = low byte); drawn, then continue
  Pieces are 8 x 8 sprites from the sprite CHR bank (PlayfieldChr0Bank, PPU $0000).
  When the object is flipped (ObjAttr bit 6 / bit 7) the offsets are mirrored:  d -> -d - 8.
  The object's ObjAttr is XORed into each piece's attributes (low 2 bits = palette).
"""
import json, sys

rom = open(sys.argv[1], 'rb').read()
prg = rom[16:16 + rom[4] * 0x4000]
BANK = 3
rd = lambda a: prg[BANK * 0x4000 + a - 0x8000]
rdw = lambda a: rd(a) | rd(a + 1) << 8
s8 = lambda v: v - 256 if v > 127 else v

def parse(p, depth=0):
    out, y = [], 0
    while True:
        n = rd(p + y)
        if n == 0:
            return out
        if n & 0x80:
            sub = n << 8 | rd(p + y + 1)
            out.append(dict(include=f'${sub:04X}', pieces=parse(sub, depth + 1)))
            y += 2
            continue
        for k in range(n):
            q = p + y + 1 + 4 * k
            out.append(dict(dy=s8(rd(q)), dx=s8(rd(q + 1)), tile=rd(q + 2), attr=rd(q + 3)))
        y += 1 + 4 * n

set_ptrs = [rdw(0x8000 + 2 * i) for i in range(18)]
sets = {}
for sp in sorted(set(set_ptrs)):
    frames, i = [], 0
    while True:
        a = sp + 2 * i
        if frames and a >= min(f for f in frames):
            break                      # the frame table ends where the first frame's data begins
        frames.append(rdw(a))
        i += 1
    sets[f'${sp:04X}'] = [dict(frame=n, address=f'${f:04X}', chunks=parse(f)) for n, f in enumerate(frames)]

def flat(chunks):
    for c in chunks:
        if 'include' in c: yield from flat(c['pieces'])
        else: yield c

json.dump(dict(
    set_table=[dict(set=i, round=i // 2 + 1, kind='shared' if i % 2 == 0 else 'round', address=f'${p:04X}')
               for i, p in enumerate(set_ptrs)],
    sets=sets,
    notes='Tiles index the sprite CHR bank in use (PlayfieldChr0Bank; RoomChr0Banks per room, bosses may switch it). '
          'Offsets are relative to ObjX/ObjY.'), open(sys.argv[2], 'w'), indent=1)
print(f'wrote {sys.argv[2]}: {len(sets)} distinct sets, '
      f'{sum(len(v) for v in sets.values())} frames, '
      f'{sum(len(list(flat(f["chunks"]))) for v in sets.values() for f in v)} sprite pieces')

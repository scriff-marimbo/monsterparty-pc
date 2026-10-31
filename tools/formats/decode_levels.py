#!/usr/bin/env python3
"""Decode Monster Party room tilemaps from your ROM into JSON (data only).

usage: decode_levels.py ROM.nes OUT.json

Format (see level_and_sprite_formats.md):
  per RoomId (16):   tileset, data bank, last scroll page, CHR banks, palette id   (fixed-bank tables $DA6B-$DACA)
  per tileset (16):  pointers to metatiles / columns / screens / attribute sets    ($DACB-$DB4A)
  screen   = 8 x [column id, attribute-set id]                      (16 bytes; one screen = 256 x 192 px)
  column   = 32 px wide: 12 metatile ids for the left 16 px, then 12 for the right   (24 bytes)
  metatile = 16 x 16 px: 4 background tile ids  TL, TR, BL, BR                       (4 bytes)
  attrset  = 6 attribute bytes for the 32 px column, top to bottom (each covers 32 x 32 px)
  palette  = 25 bytes in bank 6 at $AEDA + 25*id: backdrop colour, then 8 palettes x 3 colours
"""
import json, sys

rom = open(sys.argv[1], 'rb').read()
prg = rom[16:16 + rom[4] * 0x4000]

def rd(bank, addr):
    return prg[bank * 0x4000 + addr - (0xC000 if bank == 7 else 0x8000)]
def rdw(bank, addr):
    return rd(bank, addr) | rd(bank, addr + 1) << 8

ROOMS, TILESETS = 16, 16
rooms = []
for r in range(ROOMS):
    rooms.append(dict(
        room_id=r,
        tileset=rd(7, 0xDA6B + r),
        data_bank=rd(7, 0xDA7B + r),
        last_page=rd(7, 0xDA8B + r),      # rooms with 0 are one screen; door rooms pick the screen with DoorId
        chr0_bank=rd(7, 0xDA9B + r),      # 4 KB CHR bank for sprites ($0000)
        chr1_bank=rd(7, 0xDAAB + r),      # 4 KB CHR bank for the background ($1000)
        palette_id=rd(7, 0xDABB + r)))
# round 1 "horror world" override (Room_Init): RoomId 0 with HorrorWorld set
horror = dict(room_id=0, chr1_bank=1, palette_id=0, music=0x36)

bank_of = {r['tileset']: r['data_bank'] for r in rooms}
ptr = {t: dict(metatiles=rdw(7, 0xDACB + 2 * t), columns=rdw(7, 0xDAEB + 2 * t),
               screens=rdw(7, 0xDB0B + 2 * t), attrsets=rdw(7, 0xDB2B + 2 * t)) for t in range(TILESETS)}

tilesets = []
for t in range(TILESETS):
    bk = bank_of[t]
    bounds = sorted({v for u in range(TILESETS) if bank_of[u] == bk for v in ptr[u].values()} | {0xC000})
    end = lambda p: min(x for x in bounds if x > p)
    P = ptr[t]
    n_scr = (end(P['screens']) - P['screens']) // 16
    screens = [[[rd(bk, P['screens'] + s * 16 + 2 * c), rd(bk, P['screens'] + s * 16 + 2 * c + 1)]
                for c in range(8)] for s in range(n_scr)]
    n_col = max(c for s in screens for c, a in s) + 1
    n_att = max(a for s in screens for c, a in s) + 1
    columns = [[rd(bk, P['columns'] + 24 * i + k) for k in range(24)] for i in range(n_col)]
    n_mt = max(m for col in columns for m in col) + 1
    metatiles = [[rd(bk, P['metatiles'] + 4 * i + k) for k in range(4)] for i in range(n_mt)]
    attrsets = [[rd(bk, P['attrsets'] + 6 * i + k) for k in range(6)] for i in range(n_att)]
    tilesets.append(dict(tileset=t, data_bank=bk, pointers={k: f'${v:04X}' for k, v in P.items()},
                         screens=screens, columns=columns, metatiles=metatiles, attrsets=attrsets))

def palette(pid):
    base = 0xAEDA + 25 * pid
    raw = [rd(6, base + k) for k in range(25)]
    return dict(backdrop=raw[0], bg=[raw[1 + 3 * i:4 + 3 * i] for i in range(4)],
                sprites=[raw[13 + 3 * i:16 + 3 * i] for i in range(4)])
palettes = {pid: palette(pid) for pid in sorted({r['palette_id'] for r in rooms} | {horror['palette_id']})}

def screen_tiles(ts, screen):
    """32 x 24 background tile ids and 32 x 24 palette numbers (0-3) for one screen."""
    tiles = [[0] * 32 for _ in range(24)]
    pals = [[0] * 32 for _ in range(24)]
    for c, (col_id, att_id) in enumerate(ts['screens'][screen]):
        col, att = ts['columns'][col_id], ts['attrsets'][att_id]
        for half in range(2):
            for row in range(12):
                tl, tr, bl, br = ts['metatiles'][col[half * 12 + row]]
                x, y = c * 4 + half * 2, row * 2
                tiles[y][x], tiles[y][x + 1], tiles[y + 1][x], tiles[y + 1][x + 1] = tl, tr, bl, br
                byte = att[row // 2]
                shift = (half * 2) + (row % 2) * 4          # TL, TR, BL, BR quadrants of the 32 x 32 area
                p = (byte >> shift) & 3
                for dy in range(2):
                    for dx in range(2):
                        pals[y + dy][x + dx] = p
    return tiles, pals

for ts in tilesets:
    ts['screen_tiles'] = []
    for s in range(len(ts['screens'])):
        tiles, pals = screen_tiles(ts, s)
        ts['screen_tiles'].append(dict(screen=s, tiles=tiles, palettes=pals))

json.dump(dict(rooms=rooms, horror_world=horror, tilesets=tilesets, palettes=palettes,
               notes='Tile ids index the 4 KB CHR bank chr1_bank (background, PPU $1000). '
                     'NES colour indices in palettes.'), open(sys.argv[2], 'w'), indent=1)
print(f'wrote {sys.argv[2]}: {len(rooms)} rooms, {len(tilesets)} tilesets, '
      f'{sum(len(t["screens"]) for t in tilesets)} screens')

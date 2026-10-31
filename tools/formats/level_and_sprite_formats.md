# Monster Party: room tilemap and sprite formats

Worked out from the code (`Room_Init`, `Room_Scroll`/`Room_BuildColumn`, `DrawObject`/`DrawMetaspriteList`). Each structure was checked against the data:

- **Level data:** in every tileset, the largest id the screens and columns use is exactly the table length minus one.
- **Sprite data:** every sprite offset falls within ±56 pixels.

The two scripts write JSON only. To see the results, use Mesen's Tilemap Viewer and Sprite Viewer while playing.

```
python3 tools/formats/decode_levels.py  "roms/Monster Party (USA).nes" levels.json
python3 tools/formats/decode_sprites.py "roms/Monster Party (USA).nes" sprites.json
```

## Rooms (tilemaps)

### Per RoomId (16 rooms), fixed-bank tables

| Table | Meaning |
|---|---|
| `RoomTileset` $DA6B | tileset number 0-15 (here always = RoomId) |
| `RoomDataBank` $DA7B | PRG bank with the tilemap data (0 for tilesets 0-5, 1 for 6-15) |
| `RoomLastPages` $DA8B | last scroll page (15 = 16 screens wide; 0 = one screen) |
| `RoomChr0Banks` $DA9B | 4 KB CHR bank for sprites (PPU $0000) |
| `RoomChr1Banks` $DAAB | 4 KB CHR bank for the background (PPU $1000) |
| `RoomPalettes` $DABB | palette id (see Palettes) |

Round 1's horror world uses the same room (RoomId 0) with CHR1 bank 1 and palette 0. See `Room_Init`.

### Per tileset (16), 2-byte pointer tables into the data bank

| Table | Points to | Entry size |
|---|---|---|
| `TilesetScreens` $DB0B | screens | 16 bytes |
| `TilesetColumns` $DAEB | column definitions | 24 bytes |
| `TilesetMetatiles` $DACB | metatiles | 4 bytes |
| `TilesetAttrSets` $DB2B | attribute sets | 6 bytes |

### The structures

- **Screen**, 256 x 192 px: 8 pairs `[column id, attribute-set id]`, one per 32 px column.
  - The code picks screen number `ScreenPage + DoorId` from the tileset's list.
  - DoorId is how the small door and boss rooms that share a tileset each get their own screen.
- **Column**, 32 x 192 px: 24 metatile ids.
  - Bytes 0-11 are the left 16 px, top to bottom; bytes 12-23 are the right 16 px.
- **Metatile**, 16 x 16 px: 4 background tile ids, in the order top-left, top-right, bottom-left, bottom-right.
- **Attribute set**: 6 standard NES attribute bytes for the 32 px column, top to bottom.
  - Each byte covers 32 x 32 px.
  - Bits 0-1 = top-left 16 x 16, 2-3 = top-right, 4-5 = bottom-left, 6-7 = bottom-right. Each value is a background palette number 0-3.

The playfield is 12 metatiles tall (192 px). The status bar below it is drawn separately.

### Sizes found

| Tilesets | Size |
|---|---|
| 0-7 | 16 screens each (the scrolling stages) |
| 8-15 | 1-5 screens each (door and boss rooms) |

157 screens in total.

### Palettes

Bank 6, at `$AEDA + 25 * id`. Each palette is 25 bytes:

- 1 byte: backdrop colour;
- 4 x 3 bytes: background palettes;
- 4 x 3 bytes: sprite palettes.

The values are NES colour indices.

## Sprites (objects, enemies, bosses)

Everything is in PRG bank 3 (`ObjectGfxBank`).

### Choosing the frame

1. **Graphics set:** `set = RoundNumber*2 + (ObjDrawFlags & 1)`. The table of 18 set pointers is at $8000.
   - Even sets all point to one shared set ($8024, 60 frames): the player, items and common enemies.
   - Odd sets are the round's own set ($87CA for round 1 with 39 frames; 13 frames for the other rounds), holding that round's enemies and bosses. Rounds 8 and 9 share a set.
2. **Frame:** `ObjFrame` indexes the set's table of 2-byte frame pointers.

### Frame data

A list of chunks, ended by `$00`:

- **`$01-$7F` = n:** n pieces follow, 4 bytes each: `dY, dX` (signed, from ObjY/ObjX), `tile`, `attributes`.
- **`$80-$BF`:** the address of another chunk list (this byte is the high byte, the next byte the low byte). That list is drawn, then this one continues. Used 4 times.

### Pieces

- Each piece is an 8 x 8 sprite (the game never uses 8 x 16), taken from the sprite CHR bank.
- **The sprite CHR bank** is `PlayfieldChr0Bank`. Normally it comes from `RoomChr0Banks`; some boss code switches it (e.g. `b5_Boss_LoadGfx` sets bank 2).
- **Flipping:** `ObjAttr` bit 6 mirrors X and bit 7 mirrors Y. A mirrored offset d becomes `-d - 8`.
- **Attributes:** the object's `ObjAttr` is XORed into each piece's attributes. Low 2 bits = sprite palette, bit 5 = behind the background.

### Totals and limits

- 9 distinct sets, 190 frames, 1,522 pieces.
- The largest frames have 22-30 pieces, which is where the big bosses are.
- Not yet mapped: which `ObjFrame` values each boss uses. Read `ObjFrame,Y` for the boss's object (slot 6) in Mesen to tie frames to bosses.
- Not checked: whether any boss body is drawn into the background instead of as sprites. If one is, it will appear in a room's tilemap instead.

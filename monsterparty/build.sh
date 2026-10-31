#!/bin/sh
# Rebuild Monster Party from source and check it matches the original ROM.
# Needs cc65 (ca65 + ld65), e.g. `sudo apt install cc65`.
# Also writes monsterparty.dbg: open monsterparty.nes in Mesen and it loads our labels.
set -e
cd "$(dirname "$0")"
ROM="../roms/Monster Party (USA).nes"
# CHR (graphics) is pulled from your ROM rather than kept in the source tree.
tail -c 131072 "$ROM" > chr.bin
ca65 -g monsterparty.asm -o monsterparty.o
ld65 -C monsterparty.cfg monsterparty.o -o monsterparty.nes --dbgfile monsterparty.dbg
if cmp -s monsterparty.nes "$ROM"; then echo "OK: byte-identical to original"; else echo "MISMATCH"; exit 1; fi

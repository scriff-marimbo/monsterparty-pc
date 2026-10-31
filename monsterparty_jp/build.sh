#!/bin/sh
# Rebuild the Japanese prototype (Parody World - Monster Party) from source and check it matches.
# Needs cc65 (ca65 + ld65). Also writes monsterparty.dbg for Mesen.
set -e
cd "$(dirname "$0")"
ROM="../roms/Parody World - Monster Party (Japan) (Proto).nes"
# CHR (graphics) is pulled from your ROM rather than kept in the source tree.
tail -c 131072 "$ROM" > chr.bin
ca65 -g monsterparty.asm -o monsterparty.o
ld65 -C monsterparty.cfg monsterparty.o -o monsterparty_jp.nes --dbgfile monsterparty_jp.dbg
if cmp -s monsterparty_jp.nes "$ROM"; then echo "OK: byte-identical to original"; else echo "MISMATCH"; exit 1; fi

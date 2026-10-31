# Tools

- `merge_seeds.py` - merges per-bank retrodisasm runs into one project.

Regenerating the disassembly (e.g. after a better CDL):

    for s in 0 4 5; do mkdir -p s$s; (cd s$s && RD_SEED=$s retrodisasm -verify -cdl ../mp.fceux.cdl -o mp.asm ../mp.nes); done
    python3 merge_seeds.py out s0/mp.asm 4=s4/mp.asm 5=s5/mp.asm

Banks 4 and 5 are the only switchable banks with code. If a new CDL shows code elsewhere, add that seed.
Re-running overwrites hand-made labels, so do this before you start renaming things.

## Text (`text/`)

- `main.tbl` - main font: $01 space, $04-$0D 0-9, $0E-$27 A-Z, $28-$41 a-z, $43-$4A punctuation and quotes;
  $FE = line break, $FF = end of page. Used for the HUD, password and continue screens, and the intro/ending story and credits.
- `bubble.tbl` - boss speech-bubble font (A=$D7 ... Z=$F0, $F1-$F9 = bubble border). Each bubble is a raw
  12x8 tile grid (96 bytes) in bank 2 at $9D3A-$A7BC.
- `dump_text.py ROM.nes out.txt` - finds and decodes everything; `monster_party_text.txt` is its output.
  Single-word labels such as LIFE/SCORE/HIGH/ROUND/KEY (bank 2 $8020) and CONTINUE ($96D3) are too short for its filter.

## Names (`relabel.py`)

`python3 tools/relabel.py monsterparty` applies the known names (scheduler, MMC1 routines, NMI updates,
RAM variables, task entry tables, text blocks). Output stays byte-identical. Run it again after
regenerating with retrodisasm, then reapply your own renames, or add them to its RENAME/VARS tables.

## Your own names (`names.txt` + `apply_names.py`)

Add RAM addresses or label renames to `tools/names.txt`, then:

    python3 tools/apply_names.py monsterparty tools/names.txt && ./monsterparty/build.sh

It is safe to re-run. Multi-byte variables become `Name+1`, `Name+2`...

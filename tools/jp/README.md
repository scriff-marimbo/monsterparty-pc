# Japanese prototype tools

Tools that made `monsterparty_jp/`, the disassembly of *Parody World - Monster Party (Japan) (Proto)*, from the US disassembly in `monsterparty/`. The differences found are in `us_jp_differences.md`.

The JP ROM needs to be at `roms/Parody World - Monster Party (Japan) (Proto).nes`, with its Mesen CDL beside it as `.cdl`.

## How it was made

retrodisasm alone (same patch and seeds as the US build) rebuilt the JP ROM byte-identical. But the JP CDL is partial, so most of the code came out as bytes. Instead, the US disassembly was used as a map:

1. **Align** (`xalign.py`): every US instruction becomes a token: opcode, addressing mode, RAM/register operand, immediate and branch offset. Absolute ROM addresses are left out, because they moved. Runs of 6 tokens that are unique in both ROMs are anchors, and each anchor is extended both ways while the tokens match.
   - Result: 95.5% of US bank 4, 78.3% of bank 5 and 94.6% of bank 7 instructions matched a JP instruction.
   - Output: `data/map_us_jp.json` as `[us bank, us addr, jp bank, jp addr]`.
2. **Tables** (`tables.py`): the US jump/pointer tables (`.word label`, `<label`/`>label`, `label-1`) are located in JP through the matched instructions that read them. Entries are read in the same format.
   - A JP table ends early if it would run into known code or into another table; several JP tables are shorter.
   - Output: `data/tables.json`, and code targets as seeds in `data/table_seeds.txt`.
3. **Trace** (`codemap.py`): recursive descent from the matched instructions, the table targets, the vectors and `extra_seeds.txt`. That file holds CDL-only entry points and the boss-init table entries; the US source keeps that table as plain bytes.
   - Checks: no overlapping instructions, every CDL code byte covered, no instruction on a CDL data byte. All three pass.
   - Output: `data/codemap.json`.
4. **Emit** (`jpasm.py`): writes `bank0.asm`–`bank7.asm` and `monsterparty.asm` in the US format. Names come from:
   - US labels at matched instructions. Their comments come too.
   - The same table entry in the US table.
   - What a matched instruction reads (`lda b4_SceneStripePtrs,Y`).
   - Byte matches in data areas.
   - Bank 2's layout, which is unchanged.

   A US `b4_`/`b5_` label that moved banks gets the new bank's prefix. A US auto label (`_label_XXXX`) is renumbered to the JP address. Everything else gets an auto name.
5. **Check:** `monsterparty_jp/build.sh` must print `OK: byte-identical to original`.

`make_jp_disasm.sh` runs steps 1–4 into `monsterparty_jp_new/`. It never writes into `monsterparty_jp/`, which is meant for hand edits now, like `monsterparty/`.

## Names for the JP source

- `names_jp.txt`: JP-only names, in the same format as `tools/names.txt`. Apply them with `python3 tools/apply_names.py monsterparty_jp tools/jp/names_jp.txt`, then run `./monsterparty_jp/build.sh`.
- `bosses.md`: the boss roster, one row per boss type, with the US name and the JP prototype name. Several prototype bosses look different, though their AI is mostly the same.

## Other tools

- `diffreport.py US_ASM JP_ASM CODEMAP MAP`: the per-function tables of unmatched code in `us_jp_differences.md`.
- `gaps.py`, `triage.py`: explain JP bytes that aren't code (matching US data, filler), and rank what's left by CDL flags, references and how cleanly it decodes. Used to decide code vs data.
- `indirect.py`: lists every `jmp (ind)` and RTS-trick dispatch in JP code, and whether its table was mapped. Run it from a folder holding `jp.nes`, `codemap.json` and `tables.json`.
- `peek.py ROM BANK START END`: linear disassembly. Traced instruction starts are marked `C`.
- `m6502.py` (opcode table) and `usasm.py` (US source reader) are shared helpers.

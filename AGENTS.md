# NESdsm: Monster Party (USA) disassembly and native port

The goal: a disassembly of Monster Party (USA) that rebuilds byte-identical, and a native Linux port of the game in C++ on SDL3, made from that disassembly. A second disassembly covers the Japanese prototype, *Parody World - Monster Party (Japan) (Proto)*; the port runs both.

## Layout

- `roms/`: `Monster Party (USA).nes` and `Parody World - Monster Party (Japan) (Proto).nes`, plus Mesen CDL files (the JP one is partial). The ROMs are git-ignored and must never be committed or copied into the repo.
- `monsterparty/`: ca65 source.
  - `monsterparty.asm` holds the header, hardware/RAM symbols and includes. `bank0.asm`–`bank7.asm` are the PRG banks. `monsterparty.cfg` is the ld65 config.
  - `build.sh` rebuilds the ROM and checks it's byte-identical. It also writes `monsterparty.dbg`, which Mesen loads for labels.
  - `chr.bin` is extracted from the ROM at build time (git-ignored).
- `monsterparty_jp/`: the JP prototype's ca65 source, same file names and format as `monsterparty/`. `build.sh` checks it against the JP ROM. Names were carried over from the US source where code matches; JP-only code has auto names.
- `tools/`: disassembly tooling (see `tools/README.md`).
  - `jp/`: how `monsterparty_jp/` was made from the US source (`jp/README.md`), and `jp/us_jp_differences.md`, the list of US/JP differences. `jp/make_jp_disasm.sh` regenerates into `monsterparty_jp_new/`, never over `monsterparty_jp/`.
  - `jp/names_jp.txt`: names for `monsterparty_jp/` only (same format as `names.txt`; apply with `apply_names.py monsterparty_jp tools/jp/names_jp.txt`). A rename in `names.txt` doesn't reach the JP source.
  - `jp/bosses.md`: boss roster with the US name and the JP prototype name for each boss type. Several prototype bosses look different. JP labels keep the US boss names unless renamed in `names_jp.txt`.
  - `names.txt` + `apply_names.py`: the user's names for RAM variables and labels.
  - `text/`: text tables and a dumper, and `text_catalogue.csv` (the user's: every text of both games, and the translations for the Language setting). `formats/`: level/tilemap and metasprite decoders, plus `level_and_sprite_formats.md`.
  - `porting_coverage.md`: the inventory of indirect jumps and code coverage.
  - `relabel.py` and `merge_seeds.py` are only for freshly regenerated retrodisasm output. `relabel.py` crashes on the current files.
- `port/`: native port (see `port/README.md` and `port/porting_checklist.md`).
  - `tools/translate.py` turns `monsterparty/bank{4,5,7}.asm` into `gen/bank{4,5,7}.cpp`, `gen/ram_symbols.h` and `gen/rom_symbols.h`. `gen/` and `gen_jp/` are gitignored: CMake runs `translate.py` (target `mp_gen`) whenever the .asm files or the script are newer, so a normal build regenerates them. `--check OLD_DIR` confirms a change to the script left the generated code the same.
  - `--variant jp` translates `monsterparty_jp/` into `gen_jp/` (namespace `jp`, hooks at the prototype's addresses). `Nes::run()` runs `gen/` or `gen_jp/` depending on the loaded ROM (`Nes::game`).
  - ROMs come from `roms/` (current folder, next to the executable, then the repo's `roms/` via `MP_ROM_DIR`), checked by CRC32 of PRG+CHR: US `02B9E7C2` (required; error box if missing), prototype `21E28F50` (optional; enables the launcher's "Play the Japanese prototype", `prototype=1`, `--prototype`).
  - `src/`: `cpu.h` (6502 state and helpers), `nes.{h,cpp}` (MMC1, PPU, controllers, hooks), `audio.{h,cpp}` (the port's own NES APU emulation: pulse x2, triangle, noise), `music.{h,cpp}` (optional MIDI music: the music channels as MIDI through a SoundFont via TinySoundFont, `.mid` recording), `main.cpp` (SDL3 front end and headless test mode), `launcher.cpp` (Dear ImGui settings window shown before the game), `settings.cpp` (`~/.config/monsterparty/settings.ini`), `shader_chain.cpp` + `gl_api.cpp` (optional RetroArch GLSL presets over OpenGL; SDL's renderer when no shader is chosen), `palette.cpp` (built-in NTSC palette; reads `.pal` palette files, JASC-PAL text or binary), `language.{h,cpp}` (the Language setting; `lang_text.h` is generated from the CSV by `tools/make_lang_text.py`, `font_extra.h` from `graphics/font_extra.png` by `tools/make_font_extra.py`).
  - `src/platform.{h,cpp}`: what differs between Linux and Windows. All file access goes through `mp_fopen` / `mp_read_file` / `mp_rename` / `mp_remove` (UTF-8 paths; on Windows the wide-character calls, so non-ASCII paths work), never `std::fopen` / `std::ifstream`; `mp_config_dir()` is the one settings folder (`$XDG_CONFIG_HOME` or `~/.config` + `/monsterparty/`; Windows `%APPDATA%\NESdsm\monsterparty\`; `$MP_CONFIG_HOME` overrides both, used by the tests); `mp_is_absolute` instead of `path[0] == '/'`.
  - Windows build: `cmake/mingw-w64-x86_64.cmake` (toolchain; Wine, if installed, runs the tests), `windows/` (manifest + `.rc`, icon from `graphics/icon.png` via `tools/make_icon.py` if that file exists), `dist/README-windows.txt`. Static (`-static`, no DLLs), GUI subsystem (`mp_setup_console()`: the parent console, else `monsterparty.log` in the settings folder), `-Wa,-mbig-obj` for the generated banks.
  - `palettes/`: bundled JASC-PAL `.pal` palettes (64 colours, NES order; the file name is the name shown), made by `tools/make_palettes.py` and editable in any palette editor. The player's own palettes go in `~/.config/monsterparty/palettes/`.

## How the disassembly was made

The steps, in order, so they can be redone (e.g. with a better CDL) or checked. Commands are in `tools/README.md`.

1. **Code/data log (CDL):** the user played through the game in Mesen 2 with the Code/Data Logger on, covering as much of the game as possible. It saved `roms/Monster Party (USA).cdl`.
   - Mesen 2 writes `CDLv2` + CRC32 + one flag byte per PRG byte.
   - A converter (`mesen2fceux_cdl.py`, no longer in the repo) turned it into FCEUX format (`.fceux.cdl`, the format retrodisasm reads; it was first written for the BZK disassembler, which was tried and dropped): bit 0 code, bit 1 data, bits 2–3 the CPU 8 KB window. Mesen's bits 2–3 mean something else (jump target / subroutine entry), so they're rewritten from the MMC1 layout.
2. **retrodisasm** (github.com/retroenv/retrodisasm, commit 8d3435c), with an MMC1 patch of our own (the patch file is no longer kept in the repo):
   - Stock retrodisasm had no MMC1 support and didn't follow bank switches across banks. The patch adds an MMC1 model: a 16 KB switchable bank at $8000 and the last bank fixed at $C000. It ignores the MMC1 serial bank-switch writes, which a static tracer can't follow.
   - The patch also adds `RD_SEED=<bank>`, which picks which bank is mapped at $8000 for a run.
3. **One run per code bank:** `RD_SEED=0`, `4` and `5`, each with `-verify -cdl`. Only banks 4 and 5 have switchable-bank code; seed 0 provides the data banks. Bank 7 (fixed) comes from the first run.
4. **Merging:** `tools/merge_seeds.py` takes each bank's segment from the run where that bank was mapped and writes `monsterparty.asm` plus `bank0.asm`–`bank7.asm`. It stops if the runs disagree on a constant.
   - The `ld65` config `monsterparty.cfg` places each bank. CHR isn't in the source; `build.sh` extracts it from the ROM.
5. **Byte-identical check:** `build.sh` (ca65 + ld65) rebuilds the ROM and compares it with the original. This has stayed true through every later edit.
6. **First naming pass:** `tools/relabel.py` applied the names known at that point (scheduler, MMC1 routines, NMI updates, task entry tables, text blocks) and prefixed bank 4/5 labels `b4_` / `b5_`. It only works on fresh retrodisasm output.
7. **Hand fixes after merging:**
   - The tracer over-read the end of the item-handler jump table, which made fake labels and code. Separately, $AC86–$AE04 (bank 4) was real code shown as bytes; it's now code.
   - A fake `brk` at $E1AC and a fake label at $BA20 were removed.
   - $CBB6 is shown as `rts`. $9C44 is both code and data (an RTS that's also byte 0 of `b4_PasswordFieldBits`).
   - Indirect jumps (26 sites, inventoried in `tools/porting_coverage.md`) had their table targets turned into labels and code.
   - Every CDL-logged code byte is shown as code. Every jsr/jmp/branch lands on an instruction start; the only exception is the jump into RAM at $0191.
8. **Ongoing naming:** since then everything goes through `tools/names.txt` + `apply_names.py`, mostly from the user's observations in Mesen. Load `monsterparty.dbg` in Mesen to see the names while debugging.

**Regenerating from scratch overwrites all hand fixes and names** except what's in `names.txt`. The fixes in step 7 would have to be redone. Prefer editing the existing source.

## Commands

```sh
./monsterparty/build.sh                                       # must print "OK: byte-identical to original"
./monsterparty_jp/build.sh                                    # same for the JP prototype
python3 tools/apply_names.py monsterparty tools/names.txt     # idempotent; run build.sh after
python3 port/tools/translate.py monsterparty port/gen         # CMake does this by itself after any .asm change; by hand for --check
python3 port/tools/translate.py monsterparty_jp port/gen_jp --variant jp   # the same for the prototype
cd port && cmake -B build && cmake --build build -j
cd port && cmake -B build-portable -DMP_PORTABLE=ON && cmake --build build-portable --target portable_tar   # self-contained folder + .tar.gz for other people (see port/README.md)
cd port && cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMP_PORTABLE=ON && cmake --build build-windows -j --target portable_zip   # Windows .zip, cross-compiled with MinGW-w64
./build/monsterparty                                          # ROMs from roms/; settings window first (unless using --no-launcher); F10 reset, F11 fullscreen, Tab fast forward (4x), F12/Esc quit
./build/monsterparty_tests "~[game]"                          # quick unit tests (no game runs); all of them: ./build/monsterparty_tests (ask first, see below)
./build/monsterparty --headless 1500 --press 200:S --press 500:S --press 800:S --dump --wav out.wav   # add --prototype for the JP prototype
```

- **Headless test mode:**
  - `--press FRAME:BUTTONS` holds buttons for 8 frames. BUTTONS uses the letters `ABsSudlr`.
  - `--reset FRAME` presses Reset at that frame.
  - `--dump` prints the game variables and any nametable text.
  - `--wav FILE` writes the sound to a file and prints its RMS (0 means silence).
  - Pressing Start at frames 200, 500 and 800 reaches round 1 (`GameState` 5).
- **Unit tests** (`port/tests/`, Catch2 v3, `build/monsterparty_tests`; see `port/tests/README.md`): the `[game]` tests run the game headless (a couple of minutes in all), so the rule above applies to them; the rest (`"~[game]"`) take under a second and can be run any time. `tests/golden.txt` holds the original game's RAM hashes after scripted runs; re-record with `MP_RECORD_GOLDEN=1 ./build/monsterparty_tests "[golden]"` only when a change in behaviour is intended. Add tests for new features, and keep the "doesn't change the game" checks (`test_options.cpp`) passing.
  - The build is a library `mpcore` (the runtime and translated code) used by both `monsterparty` and the tests.
- **Dependencies:** cc65 (ca65/ld65) and the installed system SDL3 (the Windows build always builds SDL itself; it needs the MinGW-w64 GCC with POSIX threads, e.g. Arch `mingw-w64-gcc`). CMake fetches SDL only if none is installed. CMake also fetches TinySoundFont (MIT) and the GeneralUser GS SoundFont (free to redistribute; `-DMP_FETCH_SOUNDFONT=OFF` skips it), Dear ImGui v1.92.9b (MIT) and builds its core and SDL3 backends for the launcher, libretro/glsl-shaders at a pinned commit (read at run time; per-file licences, mostly GPL) and stb_image.

## Rules

- **!IMPORTANT!** Always ask the user before doing manual headless testing (UNLESS you are just running `/build/monsterparty_tests [game]`) as it can take a very long time + the user is much better at playing and testing the game.
- Don't worry about Git: Don't stage files or make commits on your own. Only use Git history if trying to figure out how/when something was changed if necessary.
- **The disassemblies must stay byte-identical.** Run the matching `build.sh` after every .asm edit (`monsterparty/` or `monsterparty_jp/`). Renames and comments are fine; don't change instruction bytes.
- **Keep both versions in step.** The US release and the Japanese prototype have separate translated code (`gen/` and `gen_jp/`) but share the runtime. Any custom code added for one version must be added to the other too, unless it's specific to that version (e.g. it depends on a routine that only exists in one ROM). This covers:
  - hooks and special cases in `translate.py` (give the prototype its own addresses in `HOOKS`);
  - hand-written replacements in `src/`;
  - names and comments (`tools/names.txt` vs `tools/jp/names_jp.txt`).
  When a change applies to only one version, say so in a comment where it's made. Regenerate and test both: `translate.py` for `gen/` and `--variant jp` for `gen_jp/`, then headless runs with and without `--prototype`.
- **The user edits `names.txt` by hand.** Read the current file before changing it, and don't regenerate over their work.
- **Prefer adding a name to `tools/names.txt`** over hand-editing labels, so names survive. It takes:
  - RAM lines: `$ADDR Name [size] ; comment`
  - label renames: `label Old New ; comment`
  - new labels: `at bankN $ADDR Name ; comment`
- **Naming:**
  - Bank 4/5 labels are prefixed `b4_` / `b5_`. Bank 7 labels have no prefix.
  - Auto labels look like `_label_XXXX` / `_func_XXXX` / `_data_XXXX`.
  - RAM names are CamelCase, e.g. `PlayerHealth` or `BossId`.
- **Generated files** in `port/gen/` and `port/gen_jp/` are never edited by hand, and never committed (gitignored; CMake makes them). Change `translate.py` or the .asm instead and rebuild.
- **ROM content stays out of the repo.** No ROM bytes, CHR dumps or extracted graphics are committed. The port and `build.sh` read the user's ROM at run time.
- **Gameplay changes go behind a setting, never into Normal play.** The port's default must play exactly like the original ROM (difficulty Normal, widescreen drawing-only).
- **Port testing is non-visual.** Use the game variables and nametable text from `--dump`, and the RMS from `--wav`. The user checks visuals and sound on their machine.

## Key facts

- **ROM:** MMC1 (mapper 1), NES 2.0 header. PRG is 8 × 16 KB banks; bank 7 is fixed at $C000. CHR is 128 KB, used as 4 KB banks.
  - Code is only in banks 4, 5 and 7.
  - Bank 6 holds sound data and palettes. Bank 3 holds metasprites. Banks 0–2 hold levels, text and other data.
  - Vectors: reset $FFD8, NMI $C1F3. The IRQ vector is unused.
- **Scheduler:** cooperative, 12 task slots, entry points called with the RTS trick.
  - Slot 0 is the player (bank 4). Slots 6–9 are enemies and bosses (bank 5). Slot 11 is game flow.
- **Game state variables:**
  - `ScreenId`: 1 title, 2 password, 3 intro, 4 round start, 5 in game, 6 game over.
  - `GameMode` $4C: 08 died, 10 door, 40 round complete, 80 title.
  - `BossId = RoundNumber*4 + BossIdInRound`.
- **Self-modifying code:** InitSound builds `STA $40xx / RTS` at RAM $0191, and $CF07 jumps there. The port replaces this with `Cpu::ram191_trampoline`.
- **Port design:**
  - Static recompilation: one function per bank, `goto` for jumps inside a bank, and a `switch(pc)` dispatch for returns and cross-bank jumps.
  - The real 6502 stack lives in emulated RAM, because the dispatchers depend on it.
  - Hooks:
    - $C06D: end of frame. Runs the rest of the `UpdateRandom` spin (see below), renders, collects audio, polls input, handles reset, then enters the NMI.
    - $C03F: sprite-0 status-bar split (also moves the cycle count up to the sprite-0 hit, scanline 190).
    - $CF07: the RAM trampoline.
    - $C597: records off-screen sprite pieces for the widescreen renderer (drawing only; gameplay is identical with widescreen on or off). See `port/porting_checklist.md`.
    - Difficulty (`DIFF_HOOKS` in `translate.py`, `Cpu::diff_*` in `src/nes.cpp`, at both versions' addresses): player damage, enemy/boss starting HP, reflected-shot instant kills, drop kinds. Casual/Nasty only; **Normal must stay identical to the original game**. Check that with `--dump`'s RAM hash against a run from before the change.
    - MIDI music (`MUSIC_HOOKS`, `Cpu::music_*`, `src/music.cpp`): read-only reports of the sound engine's music channels (logical channels 0-3; 4-8 are sound effects). With MIDI on, `ram191_trampoline` writes the music channels' volume registers as silent. Settings `music=nes|midi`, `soundfont=`, `midi.pulse1/pulse2/triangle=`; `--soundfont`, `--midi FILE.mid` (headless recording).
    - Remix Mode (`src/remix.cpp`): edits the loaded ROM data in memory (boss door flags/destinations, enemy spawn types), the file is untouched. Remix always mixes rounds, which also uses `REMIX_HOOKS` (`Cpu::remix_*`): per-task RoundNumber override at the scheduler, StartTask inheritance, DrawObject's round, RoundNumber*4+BossIdInRound in other rounds' arenas, plus per-sprite CHR bank/palette in the renderer. All no-ops with Remix off; settings `remix=`, `remix_seed=`; `--remix SEED`. Debug: `--boss-door BOSSID`, `--enter-boss-door`.
    - Bosses (launcher section "Bosses", with or without Remix; `apply_bosses()` in `src/remix.cpp`, applied before the remix): `totem_pole=` / `--totem-pole` (prototype: the unused Totem Pole gets round 7's 3rd boss door, at (04,C2)); `fix_round7_key=` / `--fix-round7` (round 7's `b4_BossesPerRound` 2 -> 3). `totem_finished=` / `--no-totem-attack` ('Finished' Totem Pole): a 2nd animation frame (`graphics/totem_frame2.png`) and head shots every 128 frames (a Man-Eating Plant bubble underneath, drawn as `graphics/totem_head.png`). The port's own sprites: `tools/make_custom_sprites.py` turns `graphics/*.png` (the user's art) into `src/custom_sprites.h`; DrawObject tags the object's pieces (`draw_custom`, `obj_custom_kind`, `oam_custom`) and the renderer draws the sprite in their place (`BOSS_HOOKS`, JP b5 $A49F, `Cpu::totem_fire`: fakes a jsr to `b5_BossFireProjectile` returning to the same instruction; the bubble task gets home 8 = round 1's arena via `RemixRuntime::force_home`, so the remix run-time part is switched on with `enable_remix_runtime`). Remix homes are RoomIds (8-15 = arenas; the code's round is home & 7). The key-steal bug: round 7 has 3 boss doors but a count of 2, and `dec BossesLeft` on each win has no floor, so a 3rd win takes it to $FF and the key is gone until a Game Over.
    - Trainers (cheats, launcher section "Trainers"): `TRAINER_HOOKS` in translate.py (`Cpu::trainer_*` in `src/nes.cpp`): starting health / Always Bert at the title's and the continue's `sta PlayerHealth`, Start on Round at the title's `sty GameState` (Y=3 new game), Super/Moon Jump at the jump table step (`inc TaskDropKind`) and movement (`sta $16`); Infinite Life in `diff_player_damage_end`; Mark Round 6 Doors (drawing only) notes the entered door in `TryEnterDoor` (US $D968 / JP $D853, `Nes::doors_entered`, cleared by `trainer_title_done`) and `render_frame` draws `graphics/r6_markeddoor.png` (`custom_sprite::R6_MARKED_DOOR`) over those doors of RoomId 5, from the door list at RAM $7B. Settings `trainer.*=`, `--trainers LIST`. `Nes::is_cheating`: password, any trainer, or a debug option (`--boss-door`, `--enter-boss-door`, `--poke`); set at the title's end and at reset; unused for now.
  - Language (launcher "Language", `language=default|en|ja`, `--language`; drawing only): English shows the prototype in English, 日本語 the US release in Japanese (needs the prototype's ROM, for its font); the other combinations change nothing. Text: the CSV's "JP ROM EN" / "US ROM JA" columns. `--lang-report` prints every text as laid out plus what doesn't fit. The font is the games' own, per character from either ROM (`find_source` in `src/language.cpp`; Latin = US $1F, kana = JP $1F plus JP bubble-font banks), missing ones from `graphics/font_extra.png` (the user's art). Tile codes are picked per context (native codes reused, else free blank tiles) and drawn by the renderer from `LangRuntime::bg_override` (`TileOverride`, background only; CHR copy untouched): story box (bank $1F on ScreenId 3/9; pages are byte streams at $6000-$7FFF via `Cpu::rd`, `b4_StoryPagePtrs` repointed; the game's engine wraps at 20, 4 lines, ゛/゜ = codes $60/$61 in the row above), screens (bank $1F otherwise; bank-2 pictures of ScreenId 2/5/6, the bank-6 TRY AGAIN box and, for English, `b4_PasswordCharTiles` + the alphabet are edited in the PRG copy), speech bubbles (`LANG_HOOKS`: US $D0C4/$D12A, JP $D095/$D0FB; the RAM $055C grid is rewritten as each page is copied, with that arena's bubble-font codes). Game Default sets nothing up.
  - Leaderboards (on by default; launcher "Scores" row; `--no-leaderboards`, headless `--leaderboards FILE`): `LB_HOOKS` in translate.py (`Cpu::lb_*`, `Nes::lb` / `lb_draw_screen` in `src/nes.cpp`, US and JP addresses); `src/leaderboard.{h,cpp}` is the emulator-free data module (`leaderboards.ini` next to settings.ini). A new game from the title with `is_cheating` false is timed from the end of ROUND 1 START (frames); the final boss's defeat logs time + the run's best score (`lb_track_score`: the highest `ScoreDigits` reached since round 1, kept over continues; not HIGH, which is the session's); the credits' last page then waits for A/Start and shows the result on ScreenId 6 (the GAME OVER screen, menu loop replaced; `:` and `#` patched into CHR $1F tiles $F0/$F1 in the port's copy). One board per ROM + difficulty + Remix, each with top-10 times and scores. A fixed/replayed Remix seed isn't logged. The result screen waits for the credits text's end (`StoryTextFlags` bit 6; they settle at $43). Off = no change to the game (tests/test_options.cpp).
  - 3D mode (`--3d`, `mode3d=1`; drawing only): `Nes::scene3d` (a `Scene3D`) is filled by `render_frame`/`build_scene3d` (background and sprite layers, per-cell collision type from the game's per-metatile table at RAM $68 (`RoomView::coll`)); `src/render3d.cpp` draws it with OpenGL (blocks for types 4/2 with the whole metatile on their sides, the whole background flat at the blocks' back face, sprite layer, flat status bar). The user tried a parallax back wall further back and didn't want it. Follows the Widescreen setting (`Scene3D::wide`; without it a hidden 16 px margin gives the room view and only the game's 256 columns are drawn). Mouse orbits (relative mode), wheel zooms, right click resets, Esc releases the mouse and a left click recaptures; the gamepad's right stick orbits too (`gamepad_camera()` in `main.cpp`, polled once a frame) and clicking it resets (unless mapped as a control); shaders off. Headless `--3d --screenshot` + `--camera`.
  - Widescreen (`--widescreen`, settings `widescreen=1`): margins in rooms are drawn from the room map data (`Nes::room_tile`), with a camera clamped to the room; other screens extend their edges. Always (`Nes::wide_dim`, set with widescreen; the user dropped the separate option): the extra playfield either side is drawn at 5/8 brightness while playing (not in boss rooms) and on the title screen (the monster parade). The password and game over screens' skeleton (tiles $8D-$94) at the right margin is replaced by the floor (ScreenId 2 and 6).
  - Leaving the game (Esc/F12/closing the window) goes back to the launcher (`session` loop in `main.cpp`, a fresh `Nes` each time; command-line options apply to the first session only); the launcher's Quit ends the program, `--no-launcher` ends it with the game.
  - Rendering is whole-frame, not cycle-accurate. Audio writes within a frame are spaced 8 cycles apart.
  - Randomness: the translated code counts CPU cycles since the NMI (`c.cyc`; base cycles per instruction from `cycles()` in translate.py, +1 per taken branch, +514 for OAM DMA, no page-crossing penalties). `frame_hook` runs `WaitNextFrame`'s `jsr UpdateRandom / lda NmiDoneFlag` loop until cycle 29780/29781 like the hardware (`Cpu::update_random`), so the RNG moves ~25-35 steps a frame in a room, varying with the frame's work and the sound update (`Cpu::rng_steps`). Before this the spin ran once a frame, which left long stretches with no RNG zero byte (the Dark World Master's nose, $0461 == 0, could stay shut ~35 s). The title seeds the RNG ($5B x8), so this is the game's only randomness.

- **JP prototype:** see `tools/jp/us_jp_differences.md`.

## Known gaps

- **Unused boss types:** 20, 22 and 23 never spawn (round 6 has only one boss), so their code is unchecked. Every other boss has been fought while recording the CDL.
- **Sprites:** the 8-sprites-per-line limit isn't emulated, and 8×16 sprites aren't supported.
- **JP prototype:** its CDL covers rounds 1, 3, 4, part of 5, 8 and the ending, and agrees with the static code map: no code bytes are missed and no instruction sits on data. Rounds 2, 6 and 7 and password decoding are unlogged.
- **Frame timing:** frames are paced by the system clock. If the sound crackles, pace them from the audio queue instead.

# Port unit tests

[Catch2](https://github.com/catchorg/Catch2) v3 tests for the C++ port (not the disassembly or the
Python tools). CMake fetches Catch2 and builds `monsterparty_tests` next to the game
(`-DMP_BUILD_TESTS=OFF` skips them; never in a portable build). They read the ROMs from the
repository's `roms/`; tests whose ROM is missing are skipped.

```sh
cmake --build build -j && ./build/monsterparty_tests            # everything (about 2 minutes)
./build/monsterparty_tests "~[game]"                             # only the quick ones (no game runs; < 1 s)
./build/monsterparty_tests "[remix]"                             # one area: [rom] [game] [golden] [options] [remix] [bosses] [trainers] [3d] [widescreen] [files]
cd build && ctest                                                # the same through CTest
```

## What's covered

- **`[rom]`** `test_rom.cpp`: both ROMs are recognised by CRC; wrong, missing and broken files are refused.
- **`[game]`** tests run the translated game headless, like `--headless` (`support.cpp`: a scripted
  controller, RAM pokes, a per-frame `watch`). Each run is a fresh `Nes`.
- **`[golden]`** `test_emulation.cpp`: the original game (both versions, and Remix with seed
  1A2B-3C4D) gives the same RAM as recorded in `golden.txt`, at round 1's start and after a minute of
  play. This guards the translation, the CPU helpers, MMC1 and the hooks. Also: round 1 is reached,
  runs repeat exactly, there is sound, Reset works.
- **`[options]`** `test_options.cpp`: things that must not change the game don't: widescreen (both
  pixel shapes), listening to the music for MIDI, difficulty Normal, the Remix run-time part with
  nothing remixed, the Bosses options outside round 7, trainers all off. Casual and Nasty do change it.
- **`[remix]`** `test_remix.cpp`: seeds round-trip as text; a seed always gives the same remix; Remix
  only edits door destinations/flags and spawn types; every boss stays behind exactly one door and each
  round keeps its boss slots (with and without the Totem Pole); enemy types only swap within their
  kind, each round keeps its number of types per kind, and fixed hazards stay. The game plays with
  Remix on; `--boss-door` reaches the chosen boss in its own arena.
- **`[bosses]`** `test_bosses.cpp`: round 7's boss count and doors; `apply_bosses` changes exactly the
  expected byte for the Totem Pole and the key-steal fix (and nothing in the version it doesn't
  apply to); the 'finished' Totem Pole fights and fires heads, and doesn't without the option.
- **`[leaderboards]`** `test_leaderboard.cpp` (quick: categories, places, ties, the top-10 limit, the file, the run's hooks and the result screen's tiles, called directly) and, in `test_options.cpp`, leaderboards on leave the game's RAM unchanged.
- **`[trainers]`** `test_trainers.cpp`: Full Life, Start on Round, Always Bert, Infinite Life, Super
  and Moon Jump, Mark Round 6 Doors (through door #3 and back in both versions: both doors noted, door
  #3 drawn recoloured, RAM the same as without it; and a quick check that a new game clears the marks
  and a continue keeps them), and `is_cheating` (new game, password, debug pokes).
- **`[3d]`** `test_3d.cpp`: 3D mode's scene doesn't change the game; in a room it covers the frame, finds round 1's floor, its background and sprite layers make up the 2D frame exactly, and the back wall is drawn; other screens are flat.
- **`[language]`** `test_language.cpp`: setting names; a language that doesn't change a game leaves its PRG alone; 日本語 without the prototype's ROM is refused; every character of both translations has a tile and fits the free tiles; the story pages ($6000 on) end and keep to 20 tiles a line; the password and game over screens' pictures read back as the CSV's text. `[game]`: the intro in English (prototype) and Japanese (US) as typed into the text box, round 1's first speech bubble in both, and Game Default leaving the game's RAM unchanged.
- **`[widescreen]`** `test_widescreen.cpp`: the title, password and game over screens in widescreen (with their margin exceptions and dimming) against recorded frame hashes; the game over screen's right-hand skeleton once came back in the margin.
- **`[files]`** `test_files.cpp`: settings save/load (including old names and bad lines, in a temporary
  `$XDG_CONFIG_HOME`), palettes save/load/refuse, every bundled palette loads, the port's own sprites
  are well formed.

## Golden values

`golden.txt` holds the expected RAM hashes. When the port's behaviour changes on purpose (a fix to the
translation, say), check the change is what you meant and record the new values:

```sh
MP_RECORD_GOLDEN=1 ./build/monsterparty_tests "[golden]"
```

A missing value fails the test (showing the actual one) until it's recorded.

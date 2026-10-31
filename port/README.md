# Monster Party: Let's Go Again (PC Port)

The game's own 6502 code, translated to C++ from the disassembly. It runs on a small software NES layer (PPU, APU, MMC1, controllers) behind SDL3. Graphics come from your ROM at run time. The game code is translated at build time from the disassembly (`gen/` and `gen_jp/` are made by CMake and are not in git); no ROM and no graphics are stored here.

## Legal

This is an unofficial fan project. It is not affiliated with, endorsed by or connected to Bandai (now Bandai Namco Entertainment) or any other rights holder of Monster Party. *Monster Party*, its characters, graphics, music and code belong to their owners. No ROMs and no game graphics are included: you need your own legally obtained copy of the game.

The port's own code, tools and documentation are under the MIT licence (`LICENSE`). That licence covers only that original work, not the game, nor the disassembly in `monsterparty/` and `monsterparty_jp/` (made from the game's code), nor anything translated from it. Third-party libraries and data keep their own licences (SDL3, Dear ImGui, TinySoundFont, stb, the GeneralUser GS SoundFont and libretro's glsl-shaders).

## Build and run

```
cd port
cmake -B build
cmake --build build -j
./build/monsterparty
```

### ROMs

The game looks for its ROMs in a folder called `roms`: first in the current folder, then next to the executable, then the repository's own `roms/` (so `./build/monsterparty` works straight from a checkout).

- **`Monster Party (USA).nes`** is required. If it's missing, or isn't the expected dump, a message box says so and the game doesn't start. A path given on the command line (`./build/monsterparty path/to/rom.nes`) is used instead of `roms/`.
- **`Parody World - Monster Party (Japan) (Proto).nes`** is optional: the unreleased Japanese prototype. When it's there, *Play the Japanese prototype* in the settings window can be ticked (it's saved as `prototype=1`). `--prototype` on the command line does the same. Without it, the checkbox is greyed out and its tooltip says why.
- Each ROM is checked by the CRC32 of its contents after the 16-byte header: `02B9E7C2` for the US release and `21E28F50` for the prototype. The translated code was made from exactly these dumps.

- **SDL3:** an installed SDL3 is used if CMake finds one. Distro packages are usually `libsdl3-dev` or `SDL3-devel`.
- **MIDI music:** CMake downloads TinySoundFont (MIT, two header files) and the GeneralUser GS SoundFont (see *Music* below; `-DMP_FETCH_SOUNDFONT=OFF` skips the SoundFont).
- **Launcher UI:** CMake also downloads Dear ImGui v1.92.9b (MIT) from github.com/ocornut/imgui and builds its core plus the SDL3 backends.
- **Translated code:** `python3` runs `tools/translate.py` as part of the build to make `gen/` and `gen_jp/` from the disassembly (about a second). It runs again when the `.asm` files or `translate.py` change.
- **Shaders:** CMake also downloads libretro's [glsl-shaders](https://github.com/libretro/glsl-shaders) (pinned commit, about 56 MB with history) and stb_image (public domain/MIT, for the shaders' PNG textures). The shaders are read at run time and never compiled into the game.
- **No SDL3 installed:** CMake downloads and builds SDL `release-3.2.30` from github.com/libsdl-org/SDL. That needs your distro's X11 and/or Wayland development packages so SDL can open a window.

### Portable build for release

`-DMP_PORTABLE=ON` makes a self-contained folder instead of a build that points into your checkout:

```sh
cmake -B build-portable -DMP_PORTABLE=ON
cmake --build build-portable -j
cmake --build build-portable --target portable_tar     # build-portable/monsterparty-portable.tar.gz (about 58 MB)
```

- **The folder**, `build-portable/monsterparty-portable/`: the `monsterparty` executable, `palettes/`, `soundfonts/` (GeneralUser GS and its licence), `shaders/` (only the presets the Shader list offers and the files they need, 43 files, from the glsl-shaders collection; the list is `dist/shaders.txt`), an empty `roms/` with a note saying which files go there, and a `README.txt` for players. ROMs are never included.
- **Data lookups:** everything is found next to the executable (and in `~/.config/monsterparty/`). No paths into your checkout or build folder are compiled in. ROMs are looked for in `roms/` in the current folder, then next to the executable.
- **Libraries:** SDL3 is always built from source and linked in, and so are the C++ runtime and libgcc (`-static-libstdc++ -static-libgcc`). The executable then only needs the system's C library (glibc), which every desktop Linux has. SDL loads X11, Wayland, PipeWire/PulseAudio/ALSA and OpenGL from the player's system at run time, so it works on GNOME, KDE and others, under X11 or Wayland.
- **The build machine needs the development headers** for those systems, because SDL only includes a display or sound system if they were installed when it was built. Configuring checks this and stops with the package list for Arch, Fedora and Debian/Ubuntu if X11, Wayland, PipeWire, PulseAudio or ALSA would be missing (`-DMP_PORTABLE_ALLOW_MISSING=ON` builds anyway). On Bazzite (an immutable Fedora), build inside a Fedora distrobox.
- **glibc:** the executable runs on distros with the same or a newer glibc than the one it was built on. Building on a very new LTS (Ubuntu 26.04) leaves out the previous LTS, Debian stable and older SteamOS releases; for a release, build in a container of the oldest distro you want to support (Ubuntu 22.04 is a good target — see below).
- **No sound device:** the game now starts silently instead of refusing to start.

Making a release:

If your glibc is newer than the player's, build in a container of the oldest distro you want to support. Ubuntu 22.04 (glibc 2.35) is a good target: it covers Ubuntu 22.04, 24.04 and 26.04, Debian 12 and 13, current Fedora and Arch, and SteamOS on the Steam Deck. Build the release there, in a distrobox:

```sh
distrobox create --name mp-build --image ubuntu:22.04
distrobox enter mp-build
sudo apt update && sudo apt install -y build-essential cmake git python3 pkg-config \
  libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
  libpipewire-0.3-dev libpulse-dev libasound2-dev libgl-dev libegl-dev
cd /path/to/NESdsm/port
rm -rf build-portable
cmake -B build-portable -DMP_PORTABLE=ON
cmake --build build-portable -j --target portable_tar
```

(`docker run --rm -it -v "$PWD:$PWD" -w "$PWD" ubuntu:22.04` works the same if you'd rather not install distrobox.) Check the result again with the `objdump` line above before sending it.

Ubuntu 22.04 does not cover Steam's Linux Runtime 3.0 "sniper" container (Debian 11, glibc 2.31). A non-Steam shortcut normally runs on SteamOS itself, which is newer than 22.04; only if a player forces that runtime would you need to build on Debian 11 instead (its PipeWire is too old for SDL3, so add `-DMP_PORTABLE_ALLOW_MISSING=ON`).

Send `build-portable/monsterparty-portable.tar.gz`. The player extracts it, puts their own `Monster Party (USA).nes` (and optionally the prototype) in its `roms/` folder, and runs `./monsterparty` (or double-clicks it). On a Steam Deck-style setup, add it to Steam as a non-Steam game for Game Mode. Their settings go in `~/.config/monsterparty/`.

### Windows build (cross-compiled on Linux)

The same portable build for Windows, made on Linux with the MinGW-w64 GCC (`cmake/mingw-w64-x86_64.cmake`):

```sh
sudo apt install g++-mingw-w64-x86-64-posix        # Fedora: mingw64-gcc-c++
cd port
cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMP_PORTABLE=ON
cmake --build build-windows -j --target portable_zip   # build-windows/monsterparty-windows.zip
```

- **The folder**, `build-windows/monsterparty-windows/`: `monsterparty.exe` with the same `palettes/`, `soundfonts/`, `shaders/` and empty `roms/` as the Linux one, and `dist/README-windows.txt` as its `README.txt`. The translated code is compiled by the same GCC as on Linux, so `translate.py` and the generated `gen/` / `gen_jp/` are unchanged.
- **One file, no DLLs:** SDL3 is built from source and linked in, and so are libstdc++, libgcc and winpthread (`-static`). The generated banks need `-Wa,-mbig-obj` (more sections than a normal object file holds); CMake adds it.
- **A GUI program:** no console window opens with it. Started from cmd or PowerShell, its output still goes to that console; double-clicked, it goes to `monsterparty.log` in the settings folder.
- **Settings folder:** `%APPDATA%\NESdsm\monsterparty\` (settings.ini, leaderboards.ini, palettes\, soundfonts\, monsterparty.log). `HOME` and `XDG_CONFIG_HOME` are ignored on Windows: Git Bash sets `HOME`, which would hide the settings somewhere else. `MP_CONFIG_HOME` overrides the folder on both systems (the folder is then `$MP_CONFIG_HOME/monsterparty/`); the unit tests use it.
- **Non-ASCII paths:** every file goes through `src/platform.h` (`mp_fopen`, `mp_read_file`, `mp_rename`, ...), which opens UTF-8 paths with the wide-character Windows calls, so a user folder like `C:\Users\José` or a Japanese folder name works. The SoundFont is loaded through it too (not `tsf_load_filename`), and stb_image is built with `STBI_WINDOWS_UTF8`. The .exe's manifest also sets UTF-8 as the process code page (Windows 10 1903+) as a second line of defence. New file access should use these helpers rather than `std::fopen` / `std::ifstream`.
- **Icon:** put a small square PNG of your own at `graphics/icon.png` (e.g. 32×32 pixel art) and the build makes it the .exe's icon with `tools/make_icon.py` (Pillow). Without it the .exe has Windows' default icon.
- **Unit tests:** without `-DMP_PORTABLE=ON` the build also makes `monsterparty_tests.exe`; with Wine installed, `ctest` in `build-windows/` (or `wine build-windows/monsterparty_tests.exe "~[game]"`) runs them.
- **Check on a real Windows machine:** shaders and 3D mode on its GPU driver, sound timing, gamepads, and the settings window at 125-200 % display scaling.

### Settings window

A settings window opens first, in two columns: *Video and sound* on the left (monitor, windowed or fullscreen, scale, pixel shape, palette, music, shader), and the game's options on the right (*Game*: which version, difficulty, Leaderboards, Remix; *Bosses*; *Trainers*). Controls..., Quit and START are at the bottom right; press START (or Enter, or Start on a gamepad). Esc or Quit closes it.

- **Display:** *Windowed*; *Fullscreen* (borderless, at the desktop resolution); or *Exclusive* (switches the monitor to the resolution you pick).
- **Scale:** always a whole number. In a window it sets the starting size, and resizing snaps the picture to the largest whole scale that fits. In fullscreen it's *Largest that fits* or a fixed scale, centred with black borders.
- **Fit (Fill the screen height):** off by default. When on, the picture is scaled as large as the window or screen allows while keeping its shape, usually top to bottom, instead of the largest whole-number scale. The Scale setting then only sets the starting window size. Pixels are first enlarged sharply to the next whole-number size and then smoothed down to fit, so they stay crisp, with at most a 1-pixel soft edge between them.
- **Pixel shape:** *NES TV (8:7)* (the default), slightly wide pixels like on a CRT, or *Square (1:1)*. With 8:7 the height stays a whole-number scale and the width is rounded, so some columns are a pixel wider.
- **Saving:** the choices go in `~/.config/monsterparty/settings.ini` (or `$XDG_CONFIG_HOME/monsterparty/`; on Windows `%APPDATA%\NESdsm\monsterparty\settings.ini`).
- **Skipping it:** `--no-launcher` skips it once.
- **Leaving the game** (Esc in a window, F12, or closing the window) brings this window back, so you can change settings and play again; *Quit* here ends the program. (`--no-launcher`: the program ends with the game.)

The picture is drawn in real screen pixels, so it stays sharp on HiDPI screens.

### Palettes

The *Palette* list in the settings window picks the 64 colours the whole game is drawn with, and shows them as a strip under the list.

- **Default:** the built-in colours, generated from the NES's NTSC signal (`src/palette.cpp`).
- **Bundled:** *Vivid*, *Soft*, *Warm CRT*, *Grayscale*, *Sepia*, *Green phosphor*, *Four-shade green* and *nes_ntsc*, in `palettes/`. `tools/make_palettes.py palettes` regenerates the first seven from the same signal model (it overwrites them, so keep edited copies elsewhere).
- **Your own:** choose *Open my palettes folder...* at the bottom of the list. It opens `~/.config/monsterparty/palettes/`, creating it if needed. If the folder is empty, it also puts `My palette.pal` there: a copy of the default colours to edit. Any `.pal` file in that folder shows up in the list under *Yours* the next time the list is opened.
- **File format:** palettes are `.pal` files, and the file name (without `.pal`) is the name shown in the list.
  - **JASC-PAL:** the Paint Shop Pro palette format, which most image and palette editors open and save. It's text: `JASC-PAL`, `0100`, the number of colours, then one `R G B` line per colour (0–255). All the bundled palettes use it.

    ```
    JASC-PAL
    0100
    64
    102 102 102
    0 42 132
    ...
    ```

    The first 64 entries are the NES colours `$00` to `$3F` in order: `$00`–`$0F` (darkest row), then `$10`–`$1F`, `$20`–`$2F` and `$30`–`$3F`. A file with fewer than 64 colours is rejected. If a line can't be read, the list shows why under the colour strip, and the game falls back to the default colours.
  - **Binary:** the usual emulator palette file, 64 RGB triples (192 bytes). Longer 512-colour files work too; only the first 64 colours are used. So palettes made for other NES emulators can be dropped in as they are.
  - Text palettes (`.txt`) are no longer read. A setting that named a bundled `.txt` palette is switched to the `.pal` with the same name.
- **Command line:** `--palette FILE` uses a palette file directly, with or without a window.

### Shaders

The *Shader* list in the settings window offers a few RetroArch GLSL presets. The default is *None*: plain sharp pixels drawn by SDL's renderer, as before.

| Choice | Preset in glsl-shaders | Notes |
|---|---|---|
| Sharp bilinear | `interpolation/sharp-bilinear-simple.glslp` | Crisp pixels, smoothed edges; good with 8:7 |
| Bicubic | `cubic/bicubic.glslp` | Soft scaling |
| NTSC (simple) | `ntsc/ntsc-simple.glslp` | Light composite colour bleed |
| NTSC (adaptive) | `ntsc/ntsc-adaptive.glslp` | Composite with dot crawl and fringing |
| NTSC 256px + scanlines | `ntsc/ntsc-256px-gauss-scanline.glslp` | Composite tuned for 256-pixel-wide consoles |
| CRT Lottes | `crt/crt-lottes.glslp` | Light CRT: scanlines, mask, slight curve |
| CRT Royale, NTSC 256px composite | `crt/crt-royale-ntsc-256px-composite.glslp` | Detailed CRT; 14 passes, needs a decent GPU |
| CRT Royale, NTSC 320px composite | `crt/crt-royale-ntsc-320px-composite.glslp` | Same, with the composite pass tuned for 320-pixel-wide consoles |

The NES picture is 256 pixels wide and its NTSC signal is the 3-phase kind, so the 256px versions are the closer match. *Other file...* opens any `.glslp` preset or single `.glsl` shader.

- **How it runs:** with a shader chosen, the game window uses OpenGL (3.0 or newer, compatibility profile) instead of SDL's renderer. `src/shader_chain.cpp` runs the preset's passes the way RetroArch's GLSL driver does. It doesn't keep frame history (`PrevN`) or feedback passes, so the few presets that need those will look wrong. If a shader fails to load or compile, a message box shows the error and the game runs without it.
- **Scaling:** the shader draws into the same whole-number-scaled rectangle as without one.
- **Where they're found:** a `shaders` folder next to the executable if there is one, otherwise the copy CMake downloaded.
- **Licences:** glsl-shaders has no single licence; each file carries its own. Most of the ones above are GPL 2 or later (bicubic, the NTSC low-pass pass, CRT Royale), the royale blur passes are MIT, sharp-bilinear and CRT Lottes are public domain, and some of the NTSC passes carry no licence notice at all. Because the port only reads them at run time as data, they don't change the port's own licence. A portable build only includes the files these presets need (`dist/shaders.txt`, with the licence files that sit next to them), as they are, with their licence headers. After adding a preset to `kShaders` in `src/launcher.cpp` (or moving to another glsl-shaders commit), update the list: `python3 tools/list_shaders.py PATH/TO/glsl-shaders --write dist/shaders.txt`. *Other file...* can still open any shader the player has, e.g. from RetroArch.

### Language

*Language* in the settings window (`language=default|en|ja`, `--language` on the command line):

- **Game Default:** each game in its own language, as released.
- **English:** both games in English. The prototype's text is replaced; the US release is already English.
- **日本語:** both games in Japanese. The US release's text is replaced; the prototype is already Japanese. This needs the prototype's ROM (its font), so it's greyed out without it. The list shows "日本語" if the system has a Japanese font for the launcher to use, else "Japanese".
- **What's translated:** the intro and ending story (and credits), the speech bubbles, and the password, between-rounds (YOUR PASSWORD) and game over screens. The title screen isn't. With English, the prototype's password alphabet becomes the US one (passwords are the same position for position).
- **The text** is in `tools/text/text_catalogue.csv`: the *JP ROM EN* column is the prototype in English and *US ROM JA* the US release in Japanese. After editing it, run `python3 tools/make_lang_text.py` (writes `src/lang_text.h`) and rebuild.
- **Checking it fits:** `./build/monsterparty --lang-report` lays out every page, bubble and screen text as the games will show them, and lists what doesn't fit or has no tile. The story box is 20 tiles wide and shows 4 lines, scrolling up for more, so a page can be any length. English story text is re-flowed: the CSV's line breaks are kept after a sentence's end (`. ! ? ” …`), before an opening `“` and at blank lines, and the rest is joined and broken between words, like the US release's own text. Bubble lines are taken as typed, and only a line that's too long is broken between words. Japanese lines are used as typed (a line over the width is broken after the last tile that fits, as the game would). Bubbles are 10 x 6 tiles inside and placed like the US release's: 1 line on the 3rd row, centred; 2 lines on the 2nd and 4th rows; more from the 2nd row down; lines one tile in from the left (at the edge if one is 10 wide). Japanese gets 3 lines (2nd, 4th, 6th rows), with ゛/゜ in the row above each like the prototype. The report shows the US release's own bubble next to each one, and how a Japanese bubble reads.
- **Font:** the games' own font, from whichever ROM has each character: Latin from the US release, kana from the prototype (its story font, plus some katakana that only its speech-bubble fonts have). Characters neither has (マ, ニ, …, :) are the port's own tiles in `graphics/font_extra.png`, an 8 x 8 cell each (white = background, blue = letters). Edit it in any pixel editor and run `python3 tools/make_font_extra.py`; to add a character, widen the PNG by a cell and add it to `CHARS` in that script.
- **How:** drawing only, and nothing is changed in the ROM files or in the port's copy of the CHR. The text is written with tile codes picked for it (codes that already show the right character are reused; others are free blank tiles), and the renderer draws those codes with the borrowed tiles. Story pages are new byte streams the game's own text engine reads from $6000-$7FFF; the screens' pictures (bank 2) and the TRY AGAIN box (bank 6) are edited in memory after loading, like the Remix; a speech bubble's text is rewritten in RAM as it's shown (translate.py `LANG_HOOKS`). Code: `src/language.cpp`. With Game Default nothing is set up.

### Difficulty

The original game has no difficulty setting, so the port adds one: a *Difficulty* list in the settings window, saved as `difficulty=casual|normal|nasty`, or `--difficulty` on the command line. **Normal is the game exactly as released**: the difficulty hooks change nothing on it (checked by comparing all of RAM after long scripted runs).

| | Casual (easy) | Nasty (hard) |
|---|---|---|
| Damage the player takes | as normal | double from enemies (2); a boss's hit costs 1 more (US 3, prototype 4) |
| Enemy and boss starting HP | half, rounded up | 1.5x, rounded up |
| Drops | an enemy that would drop nothing drops small health 25% of the time, a Bert capsule 10% | health and Bert capsule drops disappear half the time |
| Shots knocked back into enemies | instant kill, as normal | instant kill only half the time; otherwise 3 damage |

Enemies with HP $FF (the ones only an instant kill removes) are never scaled, and a knocked-back shot always kills them. The "?" and Bert's weapon items are left alone, as is the game's limit of 8 small-health drops per round. The numbers are constants at the top of the difficulty section in `src/nes.cpp`, and the hooks are listed in `tools/translate.py` (`DIFF_HOOKS`), at each version's own addresses.

### Music: NES sound chip or MIDI

The *Music* list in the settings window chooses how the music plays. Sound effects always use the NES sound chip emulation.

- **NES sound chip** (the default): the music as the NES played it.
- **MIDI: *SoundFont name***: the music is turned into MIDI as it plays and goes through a SoundFont synthesizer ([TinySoundFont](https://github.com/schellingb/TinySoundFont), MIT).
  - The game's sound engine plays music on its logical channels 0–3, one per sound chip channel, and sound effects on 4–8.
  - Hooks in the translated sound engine report each music channel's new notes, volume and pitch. Once per frame these become MIDI:
    - note on/off;
    - pitch bend, for vibrato and slides;
    - expression (CC 11), for the volume envelopes;
    - drums on channel 10, for the noise channel: bass drum, snare or closed hi-hat, depending on the noise pitch.
  - The music channels still set up the sound chip, so the sound effects that borrow a channel behave exactly as before. Their volume is just forced to 0.
- **SoundFonts:**
  - CMake downloads **GeneralUser GS** (S. Christian Collins, free to use and redistribute; about 32 MB, plus about 95 MB of git history in the build folder). It's read at run time. Skip the download with `cmake -B build -DMP_FETCH_SOUNDFONT=OFF`.
  - Your own `.sf2` files go in `~/.config/monsterparty/soundfonts/`: *Open my SoundFonts folder...* at the bottom of the list opens it. A `soundfonts` folder next to the executable is used instead of the downloaded one if it exists.
  - Saved as `music=midi` and `soundfont=` (a bundled file name, or a full path).
- **Instruments:** *Instruments...* next to the Music list picks the General MIDI instrument for each melodic channel (saved as `midi.pulse1`, `midi.pulse2`, `midi.triangle`, 0-127). The defaults are Lead 1 (square) for both pulses and Electric Bass (finger) for the triangle. How each one sounds depends a lot on the SoundFont.
- **Notes and accents:** the sound data often re-starts the same pitch just to change its volume (accents, soft-then-loud swells). That stays one held MIDI note with a volume change, rather than a string of short notes. A same-pitch note only restarts when the previous one had faded to a third of the new volume or less, or stopped.
- **Command line:**
  - `--soundfont FILE.sf2` plays the music through that SoundFont.
  - Headless, `--midi FILE.mid` also records the music as a standard MIDI file, timed by the game's frames, so you can open it in any MIDI editor.
- **The game is unaffected:** RAM after a long scripted run is identical with MIDI music on or off. In both the US version and the prototype, the music channels play songs and the effects channels play effects.

### Remix Mode

Tick *Remix* in the settings window (saved as `remix=1`; `--remix SEED` on the command line) to shuffle the game once per session. The remix stays the same until you quit (Reset keeps it); the next launch makes a new one.

- **Bosses:** the bosses and empty boss rooms are shuffled between rounds, and between the doors of each round. Each round keeps its number of bosses and empty rooms, and no boss appears twice. A boss keeps its slot number (1st, 2nd or 3rd boss of a round), because the game records a win by slot, so a round's progress and key work as usual. Round 9's final boss isn't included.
  - The door leads to the boss's own round's arena, so the fight plays exactly as at home (its code runs as its own round; BossId, the boss music and the speech bubble use that round). The status bar keeps showing the real round.
  - Arenas keep the look of the round you're in: the renderer draws the boss's arena as the current round's empty arena, in the current round's colours, except for the cells where the boss's screen differs from its own round's empty arena. Those are the boss's scenery (the well, the sphinx, the bed, round 8's eyes...) and are drawn as they are, with the current arena showing through their blank pixels inside the arena; changes to the frame (like the exit opened in the left wall after a win) are drawn as they are. The status bar and the player keep the current round's colours. Chameleon Man keeps his whole arena, since its wall of faces is part of his fight. Floors and walls can differ by a few pixels between arenas.
  - The launcher's *Bosses* options (below) are applied first, so with the Totem Pole included he's shuffled with the other 3rd bosses (six bosses for six doors).
- **Round 6's doors:** round 6 is one big maze of doors that warp to each other (27 pairs, one one-way door and the boss door). Remix pairs the 56 doors up again and moves the boss door to a random door (the old one becomes an ordinary warp); the exit in the lower right corner stays. A shuffle is only used if the round can still be beaten: the map is read from the ROM, and from the start the boss door must be reachable, then the exit from the boss door, with no place you can reach from which either is out of reach, and at least 2 door hops from the start to the boss and from the boss to the exit. The model is cautious (the player is 2 cells tall, jumps 1 cell, doors only work standing on the floor), and the original maze passes it in both versions. The shuffle uses its own random numbers, so the rest of a seed's remix is the same as before it existed. Code: `shuffle_maze()` and `check_round6_doors()` in `src/remix.cpp`; the maze itself is documented in `tools/formats/round6_doors.md`.
- **Enemies:** the enemy types are dealt out to the rounds again. For each kind (walkers, fliers, swimmers), every round's types are shuffled together and handed back, so each round keeps its number of enemy types of each kind (round 1 still has 4 kinds of walker) but they come from other rounds, and each type appears in one round. Each spawn point then picks from its round's new types of the same kind. Ceiling drips, stalactites, blocks and the constellations stay as they are, and the water round keeps swimmers. The kinds are in `kind_of()` in `src/remix.cpp`; the prototype's Spirit hops, so it counts as a walker there.
- **How:** the door and spawn data are edited in memory after loading (the file isn't touched). The game only has one round's sprite graphics loaded, and picks enemy behaviour and animation frames by RoundNumber, so a task from another round runs with RoundNumber set to its home round (put back before the next task runs), tasks it starts (shots, drops) inherit that, DrawObject uses its home round's metasprites, and the renderer draws its sprite pieces with its home round's sprite CHR bank and sprite palettes (from the ROM). These only apply while playing (not on the game over or title screens). Hooks: translate.py `REMIX_HOOKS`; code: `src/remix.cpp`. With Remix off nothing is edited and the hooks do nothing.
- **Seed:** empty = a new random remix each launch. The seed (e.g. `1A2B-3C4D`) is shown in the game window's title, and the launcher's *Replay* button puts the last one in the box so you can play it again. Settings: `remix_seed=` (typed seed), `remix_last_seed=`. Seeds from the earlier "mix rounds" option give the same remix; seeds from plain Remix before that don't.

### Bosses

The settings window's *Bosses* section works with or without Remix:

- **Include the unused Totem Pole boss** (prototype only; `totem_pole=1`, `--totem-pole`): the prototype's unfinished Totem Pole (BossId $1B) has no door leading to him. This turns round 7's door at the spot where the US version has its 3rd boss (an empty room in the prototype) into a 3rd-boss door. Without the fix below, round 7 then has the same key-steal bug as the US version.
  - **'Finished' Totem Pole boss** (on by default; `totem_finished=0` or `--no-totem-attack` turns it off): not in the game. The unfinished Totem Pole only walks back and forth with one frame, so the port finishes him off:
    - **A 2nd animation frame**, the user's `graphics/totem_frame2.png` (16 x 96, black = transparent): his own frame and this one alternate every 16 frames (`TOTEM_ANIM_FRAMES`). The renderer hides his own sprite pieces on those frames and draws the 2nd frame centred on them and bottom-aligned, facing the same way. Only while his handler runs (not once he's beaten).
    - **Head shots:** every 128 frames (about 2 seconds) he fires one of his heads at the player from the top of his sprite. The shot is the Man-Eating Plant's bubble underneath (it flies and hits like one, kind 0 or 1 at random as the plant picks them), with a 16 x 16 hit box, and the renderer draws the head (`graphics/totem_head.png`, the four corner pixels transparent) over it, facing the way it flies.
    - The art is the user's own, not from the ROM. `tools/make_custom_sprites.py` turns `graphics/*.png` into `src/custom_sprites.h`; run it after editing a PNG. Each PNG colour is stored as the nearest NES colour number (over the bundled colour palettes), so the sprites follow the chosen palette like the game's own; draw with NES colours. The 2nd frame PNG faces the other way to his unflipped sprite, so it's drawn flipped when his isn't.
    - How: a hook in his handler (translate.py `BOSS_HOOKS`, JP b5 $A49F) notes his object each frame and, for a shot, has the translated code jsr `b5_BossFireProjectile` with the plant's arguments, with his ObjY moved up to his sprite's top for the call. The shot task gets round 1's arena as its home, so Remix's run-time part (switched on for this) runs it as round 1's. DrawObject tags the pieces of objects the port draws itself, and the renderer draws the port's sprites in their place. Rate, hit box and animation speed: `TOTEM_FIRE_FRAMES`, `TOTEM_HEAD_HITBOX`, `TOTEM_ANIM_FRAMES` in `src/remix.cpp`.
- **Fix Round 7 Key Steal bug** (`fix_round7_key=1`, `--fix-round7`): round 7 has 3 boss doors, but `b4_BossesPerRound` gives it a count of 2 (in both versions). Each boss win does `dec BossesLeft` (`b5_BossDefeated_Reward`) with no stop at 0, and the exit and the key icon need exactly 0. So beating the 3rd boss after you have the key takes the count to $FF and the key is gone until a Game Over (the continue sets the count again). Giant Spider II's own code has nothing to do with it: it happens with whichever round 7 boss is beaten third, and he's probably just the one most players reach last, hence the legend that he steals your key. The fix sets round 7's count to 3, so all three bosses are needed. In the prototype it only changes anything with the Totem Pole included (round 7 otherwise has 2 boss doors).
- Code: `apply_bosses()` in `src/remix.cpp` (edits the loaded data; the file isn't touched).

### Randomness

The game's random numbers come from `UpdateRandom` ($C66E), a 64-bit shift register that the title screen seeds with $5B in every byte. Its only source of randomness is timing: at the end of each frame `WaitNextFrame` goes round `jsr UpdateRandom / lda NmiDoneFlag / beq` until the NMI, so the register moves on by however many rounds fit in what's left of the frame. The translated code counts CPU cycles (`c.cyc`: base cycles per instruction, +1 for a taken branch, +514 for the OAM DMA; page-crossing penalties are left out), the sprite-0 hook moves the count up to the hit (scanline 190), and the end-of-frame hook runs the loop until cycle 29780/29781. In a room that's about 25-35 steps a frame, changing with the frame's work and the sound. (Earlier the loop ran once a frame, which made every random choice a fixed function of the frame number and left gaps of up to 2,000 frames with no zero byte: the Dark World Master only opens his nose on one.) It is an approximation of the hardware's timing, not an exact match for a given console.

### Leaderboards

A finished game is logged locally: the time and the run's best score at the moment the final boss (the Dark World Master) is beaten. On by default; the *Scores* row of the settings window has the checkbox and a *View...* button for the lists (`--no-leaderboards` turns it off for one run).

- **What counts:** a new game from the title screen (no password, no trainer, no debug option: `is_cheating` false) that ends with the final boss. A Remix run with a fixed seed (typed in the launcher, or `--remix`) isn't logged; a random Remix is, as is any Totem Pole / Fix Round 7 setting. Continuing after Game Over keeps the run.
- **Time:** emulated frames (at 60.0988 a second) from the end of ROUND 1 START to the final boss's defeat, so pauses and a Game Over in between count. **Score:** the run's own best: the highest the score got from round 1 to the final boss (`Cpu::lb_track_score`, every frame), so a continue, which clears the score, doesn't lose it. Not the HIGH value, which is the session's: 200,000 from power-on and kept over every game since.
- **Leaderboards:** one for each version (US / JP prototype) + difficulty + Remix on or off ("US - Nasty", "JP - Normal (Remix)"), each with two top-10 lists: fastest times and highest scores. A tie goes below the older entry. Stored in `leaderboards.ini` next to `settings.ini` (`time|score KEY FRAMES POINTS UNIXTIME` per line).
- **The result screen:** after the credits, A or Start (once the credits text has reached its end, i.e. `StoryTextFlags` bit 6 (they settle at $43), and a second has passed; the hooked `rts` runs every frame from the start of the credits) goes to a screen built on the GAME OVER screen (the skeletons in the blood-red room) with `TIME 02:03:46 #2` and `SCORE 0840200 #1` (no `#n` outside the top 10) and the leaderboard's name. It stays for ever, like the credits. Reset or Esc leave as usual. The font has no `:` or `#`, so two blank tiles of CHR bank $1F get them, in the port's copy of the CHR only.
- **How:** `LB_HOOKS` in translate.py (`Cpu::lb_*`, `Nes::lb_*` in `src/nes.cpp`): the clock starts after ROUND 1 START (`b4_Screen4_RoundStart`), the last boss of round 9 is noted at `b5_BossDefeated_Reward`, the final boss's branch there logs the result, the ending's last page (`b4` story rts) switches to `ScreenId` 6, and that screen's menu loop is replaced by a wait. The result screen plays the password screen's music (`PlaySoundRange` US $04 / JP $34). `src/leaderboard.{h,cpp}` has the lists and file format and no emulator code. Headless: `--leaderboards FILE` (otherwise off). With the setting off every hook does nothing and the RAM hash is the original game's.

### Trainers (cheats)

The settings window's *Trainers* section has cheats, like Game Genie codes. All off is the game as released.

- **Infinite Life:** hits still knock you back, but take no health (the health lost in `b4_PlayerHit` is put back; this also wins over Nasty's extra damage).
- **Start with Full Life:** a new game, and a continue after Game Over, start with full health ($2C) instead of $0C.
- **Start on Round N** (2-8): START on the title screen begins the game in that round (passwords and continues work as usual).
- **Super Jump:** the rising part of the jump moves twice as far each frame, so the player jumps twice as high. **Moon Jump** (only one of the two can be on): the rising part is doubled and each step of the jump lasts two frames, and falling is half as fast, so the jump goes about four times as high and the player floats down. Both apply to Bert too.
- **Always play as Bert** (Baryuu in the prototype): a new game (and a continue) starts as Bert, and his time is kept from running out.
- **Mark Round 6 Doors:** in round 6's door maze (RoomId 5, 56 doors; see `tools/formats/round6_doors.md`), each door you have gone through is drawn as `graphics/r6_markeddoor.png` (the door recoloured, the user's art) instead. Only the door you entered is marked, not the one you arrive at (one door is one-way). The marks last for the run: a new game from the title (or a password) clears them, a continue keeps them. Drawing only, but it counts as a trainer like the others (no leaderboards).
- Settings: `trainer.infinite_life=`, `trainer.full_life=`, `trainer.start_round=` (0 = off, else 2-8), `trainer.super_jump=`, `trainer.moon_jump=`, `trainer.always_bert=`, `trainer.mark_doors=`. Command line (overrides the settings): `--trainers life,full,round=5,super,moon,bert,doors`.
- **How:** hooks in translate.py `TRAINER_HOOKS` (`Cpu::trainer_*` in `src/nes.cpp`): the starting health (title screen and continue), the end of the title screen (the round), and the jump, which is a table of per-frame movements indexed by `TaskDropKind` (the hooks scale the movement and, for Moon Jump, take each step twice). Always Bert sets `PlayerForm` bit 7 with the starting health and keeps `BertTimer` at 1. Mark Round 6 Doors: a hook in `TryEnterDoor` where the entered door has been found (US $D968, JP $D853; Y = the entry's offset in the room's door list) sets a bit in `Nes::doors_entered`; the renderer reads the room's door list (bank 2, pointer in RAM $7B) and draws the PNG over each marked door, which is the entry's cell (row, page*16 + x tile, the player's spot in the doorway) and the cell above it. The PNG's black is the backdrop; its colours become NES colours through `tools/make_custom_sprites.py` (so they follow the palette and the game's fades).
- **Cheating flag:** the port keeps `is_cheating` (in `Nes`) for each game: true if the game was started from the password screen, if any trainer is on, or if a command-line debug option that changes the game is used (`--boss-door`, `--enter-boss-door`, `--poke`). It's set when a game starts from the title screen, and shown by `--dump`. Leaderboards use it (below).

### Widescreen (experimental)

Tick *Widescreen* in the settings window (or run with `--widescreen`) for a 16:9 picture: 428 x 240 with square pixels, 374 x 240 with 8:7 ones. That's 86 (or 59) more pixels of the game on each side.

- **Rooms:** the extra columns come from the room's own map data (`room_tile()` in `src/nes.cpp`, the same lookups as `Room_BuildColumn`). The nametables can't supply them, because they only hold the visible screen plus the next column. The view has a camera of its own, clamped to the room, so at the start and end of a stage the picture stops at the room's edge and the player walks towards the side, instead of showing empty space. Rooms narrower than the view (most door and boss rooms are one screen) have their edge metatile column repeated outwards.
- **Sprites:** the game hides any sprite piece that's off its 256-pixel screen. A hook notes where each hidden piece really is, and the renderer draws it in the margins.
- **Gameplay is untouched:** widescreen only changes what's drawn. Enemies still spawn when they reach the original screen's edge and are removed 16-64 pixels past it, so in the wider picture they can appear and vanish in view. (An earlier version moved spawning and despawning to the wider view's edges; it's in the git history.)
- **Other screens** (title, story, password, round start): the outer 32 pixels repeat outwards, which continues their borders, drapes and floors. Two exceptions, where that strip holds part of a picture: the title screen leaves the margins black beside the title card's mouth, and the password screen's right-hand skeleton isn't repeated. The status bar's edge tile column repeats.
- **Known oddities:**
  - While a room is loading, the margins can show the new room for a few frames before the middle catches up.
  - Tiles the game changes in the nametable at run time show in the middle only. The margins come from the map data.
  - The picture is wider than the camera logic the game was designed around, so things it hides "just off screen" can be visible.


- **The extra sides are dimmed** while playing: the extra playfield either side of the original screen is drawn a little darker (5/8 brightness; not the status bar, and not in boss rooms, where nothing appears at the edges), so it's clear where enemies appear and disappear. The title screen's sides are dimmed too, for the monster parade. Drawing only.

### 3D mode (experimental)

Tick *3D mode* in the settings window (`mode3d=1`, `--3d`) to draw the game in 3D with OpenGL. It's drawing only: the game runs exactly as in 2D (the unit tests check the RAM is the same).

- **Rooms:** cells whose metatile the game treats as solid (its collision types 4 and 2: floors, walls, platforms) become blocks 16 px deep. Their front face is the 2D picture; their top, bottom and sides show the cell's whole 16 x 16 metatile across the depth, folded over the front edge, and shaded. Everything else in the background (doors, exits, scenery) is flat on a wall at the blocks' back face, in its 2D place. The sprites are one flat layer just in front of the blocks, and the status bar stays flat along the bottom of the window.
- **Other screens** (title, story, password, round start, game over) are a flat picture in the 3D view.
- **Camera:** the mouse turns it around the middle of the screen (up to 75° sideways, 60° up or down; the mouse is captured while playing), the wheel zooms, a right click puts it back straight on. Esc lets go of the mouse (a second Esc leaves fullscreen or quits, as usual); a left click in the window takes it again. On a gamepad the right stick turns the camera too, the opposite way to the mouse on both axes (the stick pushed right turns it as moving the mouse left does, pushed up as moving the mouse down; whether or not the mouse is captured; dead zone a quarter of the way, then faster the further it's pushed, up to 120° a second; the Controls window mentions it), and clicking the right stick puts it back straight on (unless the right stick button is set as one of the game's controls). The gamepad has no zoom.
- **Width:** with *Widescreen* on, 3D shows the same 16:9 view as 2D widescreen (with the dimmed sides); with it off, just the original screen (the window is 4:3). Straight on, rooms look as in 2D.
- Shaders are off in 3D mode. 
- **How:** `Nes::render_frame` fills a `Scene3D` (`nes.h`): the background and the sprites as separate layers, each 8 x 8 cell's collision type (the game's own per-metatile table, pointed to by RAM $68), (`Nes::build_scene3d`). `src/render3d.cpp` draws it. Headless, `--3d --screenshot FILE.bmp [--camera YAW,PITCH,ZOOM]` draws the last frame in 3D (`SDL_VIDEODRIVER=offscreen` works without a display).
### Controls

Keyboard defaults:

| Key    | NES action |
|--------|------------|
| Arrows | D-pad      |
| X      | A          |
| Z      | B          |
| A      | Turbo B (hold to keep swinging the bat) |
| Enter  | Start      |
| RShift | Select     |
| F10    | Reset      |
| Tab    | Fast forward on/off (4x speed) |
| F12    | Quit       |
| F11 or Alt+Enter | fullscreen on/off |
| Esc | leave fullscreen; quits when windowed |

In this game, B swings the bat and A jumps. Select only moves the cursor on the title and continue menus (Down does the same); it does nothing in play.

Gamepad (SDL's standard layout; button names are the Xbox ones):

| Gamepad | NES |
|---|---|
| D-pad or left stick | D-pad |
| B (right) | A (jump) |
| A (bottom) | B (bat) |
| X (left) | Turbo B |
| Back / View / Select | Select |
| Start / Menu | Start |

These are the defaults. **Controls...** in the settings window changes them. Each NES button (and Turbo B) gets one keyboard key and one gamepad button; the triggers can be used too. Click a box, then press the key or button. Esc cancels, and Delete clears the box. A key or button can only do one thing, so assigning it removes it from wherever else it was. *Reset to defaults* puts everything back, and a checkbox turns the left stick on or off as a d-pad. The controls are saved in `settings.ini` with the rest when you press START, as `key.b=Z`, `pad.b=a` and so on, using SDL's key names and gamepad button names (`a` = bottom, `b` = right, `x` = left, `y` = top). F10, F11, F12, Tab and Esc are fixed (Tab can't be assigned).

Only one gamepad is used: the first one connected. If it's unplugged, the next one connected takes over.

Turbo B presses B for 3 frames and releases it for 3. A bat swing lasts 17-18 frames, so holding turbo swings again as soon as each swing ends. `TURBO_HALF` in `src/main.cpp` sets the rate.

## How it works

- **`tools/translate.py`** reads `monsterparty/bank4.asm`, `bank5.asm` and `bank7.asm` and writes `gen/bank4.cpp`, `bank5.cpp` and `bank7.cpp` (10,304 instructions), plus `gen/ram_symbols.h` and `gen/rom_symbols.h`.
  - **The prototype** has its own translation: `--variant jp` reads `monsterparty_jp/` and writes `gen_jp/` (9,847 instructions), all inside `namespace jp`. The two versions share everything else (`src/`); `Nes::run()` picks `run_bankN` or `jp::run_bankN` for the loaded ROM. The prototype's code is at different addresses (its player code even moved from bank 4 to bank 5), so one translation with branches for each version wasn't practical.
  - The hooks below sit at the prototype's own addresses there: $C069, $C03F, $CED8 and $C58E (US: $C06D, $C03F, $CF07, $C597). `translate.py` checks each hook lands on the expected instruction.
  - Each instruction becomes a few lines of C++ that work on the emulated registers and 2 KB of RAM.
  - RAM variables keep their names from the disassembly, e.g. `R(PlayerHealth)` or `ram[ObjX + c.Y]`. ROM tables and hardware registers do too, as `rom::b5_BossStartHealth` and `hw::PPU_CTRL`.
  - Each line is one instruction, with its address and asm text as a trailing comment. Named routines get a header with their comment from the .asm, and gotos use the asm label names.
  - Jumps within a bank are `goto`s. Returns and cross-bank jumps go through a `switch` on the address.
  - The real 6502 stack is kept, because the game's dispatchers depend on it.
- **`src/nes.cpp`** emulates what the code talks to: the PPU registers and a whole-frame renderer, MMC1 bank switching, OAM DMA and the controller port. It also holds the four hand-written replacements listed in `porting_checklist.md`.
- **`src/audio.cpp`** is the port's own NES APU emulation (no outside sound library): the two pulse channels (duty, envelope, sweep), the triangle (linear counter), noise (both modes), length counters and the frame sequencer, mixed with the console's non-linear mixer formulas. The DMC only contributes its output level, because Monster Party never plays samples. Each output sample is the average over the CPU cycles it covers, then a 37 Hz high-pass and a 14 kHz low-pass filter shape it; about 735 samples per frame, 44.1 kHz mono. The port's cycle count is only approximate (see Randomness below), so the register writes within a frame are spaced 8 cycles apart. That's fine here because the sound engine writes everything in one burst per frame.
- **`src/launcher.cpp`** is the settings window (Dear ImGui), and **`src/settings.cpp`** loads and saves `settings.ini`.
- **`src/palette.cpp`** holds the built-in palette and reads and writes palette files.
- **`src/music.cpp`** turns the music channels into MIDI (see *Music* above), plays it through TinySoundFont, records `.mid` files and finds SoundFonts.
- **`src/shader_chain.cpp`** runs RetroArch GLSL presets with OpenGL; **`src/gl_api.cpp`** loads the OpenGL functions through SDL.
- **`src/main.cpp`** handles the SDL3 window, sound output (an SDL audio stream, capped at about 100 ms queued), input and 60.1 Hz pacing.

`gen/` and `gen_jp/` are not in git. The CMake build makes them (a custom command that runs `translate.py` when the `.asm` files or the script are newer than the output), so after renaming things in the disassembly just build again. To do it by hand:

```
python3 tools/translate.py ../monsterparty gen
python3 tools/translate.py ../monsterparty_jp gen_jp --variant jp
```

After changing `translate.py` itself, check that the generated code is unchanged by comparing it with the previous output. `--check` replaces every name with its number and compares the code token by token:

```
cp -r gen /tmp/gen_old
python3 tools/translate.py ../monsterparty gen --check /tmp/gen_old
```

## Tested so far

All of these were run headless, with scripted button presses:

- Boots to the title screen.
- New game: the intro story plays, and Start skips it.
- ROUND START screen, then into round 1 (`GameState` 5). Round 1's room tiles are in the nametable.
- The password screen draws "INPUT PASSWORD" and its character grid.
- 6,000 frames of walking right and attacking: no crashes, and the player takes damage and scrolls.
- Headless speed is about 3,000 frames per second.
- Sound (`--wav`): title music, then round 1 music, both at normal levels.
- The prototype: boots, reaches round 1 with the same button presses, walks and takes damage; 30,000 frames of random input in each version without a crash; its room map data lines up with its nametables (for widescreen) as well as the US one's does.
- ROM checks: a missing US ROM, a wrong file under the US name, and a wrong file under the prototype's name are each reported.

I couldn't open a real window here. Visual checks (status-bar split position, sprite priority, colours) still need doing on your machine.

## Not done yet

- **Sound timing:** frames are paced by the clock, not by the sound card. If you hear occasional crackles, the next step is to pace frames from the audio queue instead.
- **8-sprites-per-line limit:** not emulated. The game may rely on it for flicker; check in play.
- **Status-bar split line:** taken as sprite 0's Y + 3. Adjust if the boundary looks off.

## Unit tests

`tests/` has Catch2 tests for the port, built as `build/monsterparty_tests` (CMake fetches Catch2; `-DMP_BUILD_TESTS=OFF` skips them). They load the ROMs from `roms/` and run the game headless with scripted input: the original game against recorded RAM hashes (`tests/golden.txt`), options that must not change the game (widescreen, MIDI, Normal, the Remix run-time part, Bosses outside round 7, trainers off), Remix's data invariants, the Bosses options, the trainers and `is_cheating`, leaderboards, settings and palette files. `./build/monsterparty_tests "~[game]"` runs only the quick ones (no game runs). See `tests/README.md`.

## Headless test mode

```
./build/monsterparty --headless 1200 --press 200:S --press 500:S --press 800:S --dump
```

- **`--press FRAME:BUTTONS`** holds buttons for 8 frames starting at FRAME. BUTTONS uses the letters `A B s S u d l r` (A, B, Select, Start, up, down, left, right).
- **`--dump`** prints the main game variables and any text found in the nametables. (The text search uses the US font, so the prototype's Japanese text doesn't show.)
- **`--prototype`** runs the Japanese prototype instead.
- **`--difficulty casual|normal|nasty`** sets the difficulty (default normal).
- **`--soundfont FILE.sf2`** plays the music as MIDI through a SoundFont; **`--midi FILE.mid`** records the music as MIDI.
- `--dump` also prints a hash of all 2 KB of RAM, so two runs can be compared exactly.
- **`--reset FRAME`** presses Reset at that frame (the same as F10).
- **`--palette FILE`** draws with the colours from a palette file.
- **`--widescreen`** turns on widescreen mode (drawing only: the game runs exactly as without it).
- **`--remix SEED`** turns on Remix Mode with that seed (8 hex digits) and prints what it shuffled.
- **`--totem-pole`** (prototype) gives the unused Totem Pole round 7's 3rd boss door; **`--fix-round7`** fixes the round 7 key-steal bug (see Bosses above).
- **`--ending`** (debug, headless or not): a new game goes straight to the ending story (screen 9) instead of the intro story. Counts as cheating (`is_cheating`), so nothing is logged to the leaderboards.
- **`--trainers LIST`** turns on trainers (cheats): comma-separated from `life`, `full`, `round=N` (2-8), `super`, `moon`, `bert`, `doors` (see Trainers above).
- **`--poke FRAME:ADDR=VALUE`** writes a RAM byte (hex) at that frame, for tests (e.g. putting the player in front of a door).
- **`--boss-door BOSSID`** (debug, headless or not): every round's first boss door leads to that boss (hex BossId `00`-`1F`, e.g. `15` = Chameleon Man, `00` = round 1's empty room), in its own round's arena. Round 1's first boss door is on the upper ledge a little way into the round. Works with or without Remix.
- **`--enter-boss-door`** (headless): starts a game and goes through round 1's first boss door (walks right, puts the player in front of the door, presses Up), pressing A to get past the speech bubbles. With `--boss-door`, any boss's fight in a few hundred frames: `--headless 1800 --enter-boss-door --boss-door 1A --screenshot reaper.bmp`.
- **`--dump-vram FILE`** (headless): at the end, writes the nametables (2 KB), the palette (32 bytes) and the CHR banks and PPU control (3 bytes) to FILE.
- **`--wav FILE`** writes the sound to a WAV file and prints its RMS level (0 means silence).
- **`--screenshot FILE.bmp`** saves the last frame as a BMP (at the game's own size, widescreen included).

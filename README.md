# Monster Party: Let's Go Again (PC port)

A disassembly of the NES game Monster Party (US release) and the unreleased Japanese prototype
(*Parody World - Monster Party*), plus a native desktop port: the game's 6502 code translated to
C++ and run on a small NES layer behind SDL3, with a settings window, Remix Mode, difficulty
levels, trainers, leaderboards, MIDI music, shaders, a 3D mode and more.

- `port/`: the native port. Start with [`port/README.md`](port/README.md) (building, options, how it works).
- `monsterparty/`, `monsterparty_jp/`: the disassembly (ca65), rebuilt byte-for-byte by `build.sh` from your own ROM.
- `tools/`: scripts for names, text and level formats, and notes on how the disassembly was made.

You need your own copy of the game: put it in `roms/` (see `port/README.md`). No ROMs are included.

## Legal

This is an unofficial fan project. It is not affiliated with, endorsed by or connected to Bandai
(now Bandai Namco Entertainment) or any other rights holder of Monster Party. *Monster Party*, its
characters, graphics, music and code belong to their owners.

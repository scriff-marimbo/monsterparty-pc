#!/bin/sh
# Translate both roms' .asm to the port's C++ code. The CMake build does this by itself when the .asm or
# translate.py changed; this is for doing it by hand (e.g. translate.py --check).
python3 port/tools/translate.py monsterparty port/gen
python3 port/tools/translate.py monsterparty_jp port/gen_jp --variant jp

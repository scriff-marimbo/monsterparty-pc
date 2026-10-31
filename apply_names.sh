#!/bin/sh
# Apply custom labels to the generic asm labels/variables
python3 tools/apply_names.py monsterparty tools/names.txt
./monsterparty/build.sh
python3 tools/apply_names.py monsterparty_jp tools/jp/names_jp.txt
./monsterparty_jp/build.sh
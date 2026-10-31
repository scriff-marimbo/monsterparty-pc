./apply_names.sh
cd port
cmake --build build -j   # (also translates monsterparty/ and monsterparty_jp/ into port/gen and port/gen_jp when they changed)

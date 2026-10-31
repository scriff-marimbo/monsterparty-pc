#!/bin/sh
# Regenerate the JP prototype disassembly from the US one. Writes monsterparty_jp_new/ (never
# over monsterparty_jp/, which may have hand edits). Run from the repo root. Needs both ROMs and
# the JP Mesen CDL in roms/. See tools/jp/README.md.
set -e
J=tools/jp; D=$J/data
US="roms/Monster Party (USA).nes"
JP="roms/Parody World - Monster Party (Japan) (Proto).nes"
CDL="roms/Parody World - Monster Party (Japan) (Proto).cdl"
mkdir -p $D
python3 $J/xalign.py monsterparty "$JP" $D/map_us_jp.json
# tables twice: the second pass knows the code found by the first (stop rule for shorter tables)
python3 $J/tables.py monsterparty "$US" "$JP" $D/map_us_jp.json $D/tables.json $D/table_seeds.txt
cat $J/extra_seeds.txt $D/table_seeds.txt > $D/all_seeds.txt
python3 $J/codemap.py "$JP" "$CDL" $D/map_us_jp.json $D/all_seeds.txt $D/codemap.json > /dev/null
python3 $J/tables.py monsterparty "$US" "$JP" $D/map_us_jp.json $D/tables.json $D/table_seeds.txt $D/codemap.json
cat $J/extra_seeds.txt $D/table_seeds.txt > $D/all_seeds.txt
python3 $J/codemap.py "$JP" "$CDL" $D/map_us_jp.json $D/all_seeds.txt $D/codemap.json
python3 $J/jpasm.py monsterparty "$JP" $D/codemap.json $D/map_us_jp.json $D/tables.json monsterparty_jp_new
cp monsterparty_jp/monsterparty.cfg monsterparty_jp/build.sh monsterparty_jp_new/
echo "wrote monsterparty_jp_new/; run monsterparty_jp_new/build.sh to check it"

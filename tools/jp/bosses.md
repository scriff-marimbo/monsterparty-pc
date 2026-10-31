# Boss roster: US release vs. JP prototype

One row per boss type (`BossId` = `RoundNumber*4 + BossIdInRound`; the index into `b5_BossInitLo/Hi` and `b5_BossHandlersLo/Hi` in both ROMs). Both ROMs have the same 33 types, in the same order.

**Fill in the JP column by hand** as you identify the prototype's bosses.

The labels keep the US names in both disassemblies (`b5_Boss03_PumpkinGhost` in both). That keeps the two sources easy to compare and lets one port share names across both ROMs. To also rename a label in `monsterparty_jp/`, add it to `tools/jp/names_jp.txt`.

"JP code differs" comes from the instruction alignment (`us_jp_differences.md`). It only says the routine isn't instruction-for-instruction the same. The change may be just a different graphics bank, palette or animation frame, not the AI.

| BossId | Round | US name (label) | JP prototype name | Looks different in JP | JP code differs | Notes |
|---|---|---|---|---|---|---|
| $00 | - | EmptyRoom (`b5_Boss00_EmptyRoom`) | EmptyRoom | no | | |
| $01 | 1 | Man-Eating Plant (`b5_Boss01_ManEatingPlant`) | Man-Eating Plant | yes | | The invisible background speaker is now visible |
| $02 | 1 | Torigaran (`b5_Boss02_Torigaran`) | Torigaran | no | | dead creature; the US manual calls it "Giant Spider" |
| $03 | 1 | Pumpkin Ghost (`b5_Boss03_PumpkinGhost`) | Apeman Rider | yes | yes | Ape rider from the movie Planet of the Apes, visible Statue of Liberty background |
| $05 | 2 | Medusa (`b5_Boss05_Medusa`) | Medusa | yes | | Looks more like Medusa than a big snake with hair |
| $07 | 2 | Shrimp Attack (`b5_Boss07_ShrimpAttack`) | Shrimp Attack | no | | |
| $06 | 2 | Haunted Well (`b5_Boss06_HauntedWell`) | Haunted Well | no | | |
| $09 | 3 | Giant Spider (`b5_Boss09_GiantSpider`) | The Thing | yes | | Looks like a human head spider creature from the movie The Thing |
| $0A | 3 | Sphinx Guardian (`b5_Boss10_SphinxGuardian`) | The Mummy | yes | yes | Clearly looks like a mummy instead of transparent spirit, BG sphinx is colored differently |
| $0B | 3 | Giant Bull Man (`b5_Boss11_GiantBullMan`) | Giant Bull Man | no | | |
| $0D | 4 | Giant Cat (`b5_Boss13_GiantCat`) | Gremlin | yes | | Creature from the movie Gremlins |
| $0E | 4 | Punk Rocker (`b5_Boss14_PunkRocker`) | Punk Rocker | no | | |
| $0F | 4 | Giant Samurai (`b5_Boss15_GiantSamurai`) | Giant Samurai | no | yes | |
| $11 | 5 | Living Dead (`b5_Boss17_LivingDead`) | Bon-odori Zombies | yes | yes | There are 3 dancing zombies instead of 2, background looks darker, type 16 / 19 handler also differs |
| $12 | 5 | Mad Javelin Man (`b5_Boss18_MadJavelinMan`) | Daimajin | yes | | Almost the same but his face changes to look angry |
| $15 | 6 | Chameleon Man (`b5_Boss21_ChameleonMan`) | Chameleon Man | no | yes (init and handler) | Looks unfinished, instead of having fake faces mixed in its just the one boss face that moves back and forth. 4 tasks |
| $19 | 7 | Royce (`b5_Boss25_Royce`) | Royce | no | | |
| $1A | 7 | Grim Reaper (`b5_Boss26_GrimReaper`) | Xenomorph | yes | | Looks vaguely like a xenomorph from the movie Alien, projectiles are facehuggers |
| $1B | 7 | Giant Spider II (`b5_Boss27_GiantSpiderII`) | Totem Pole | yes | yes | An unfinished, unused boss. Unreachable normally: no round 7 door is set to boss 3. |
| $1D | 8 | Giant Dragon (`b5_Boss29_GiantDragon`) | Giant Dragon | no | yes | type 28 handler also differs |
| $1E | 8 | Snake Man (`b5_Boss30_SnakeMan`) | Snake Man | no | | |
| $1F | 8 | Hand Creature (`b5_Boss31_HandCreature`) | Hand Creature | no | no | |
| $20 | 9 | Dark World Master (`b5_Boss32_DarkWorldMaster`) | Dark World Master | no | yes | eyeball projectiles are blue |

The other types ($04, $08, $0C, $10, $13, $14, $16-$18, $1C) use the unnamed `b5_BossHandlerNN` routines, or are never placed in a round.

What the code differences do for $0F, $15, $1D, $1F and $20: see "Boss code differences" in `us_jp_differences.md`.

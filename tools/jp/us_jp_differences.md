# Monster Party: US release vs. the Japanese prototype

How *Parody World - Monster Party (Japan) (Proto)* differs from *Monster Party (USA)*. Found by aligning the two disassemblies (`monsterparty/`, `monsterparty_jp/`; see `tools/jp/README.md`). "JP" means the prototype throughout.


## Summary of differences

- The player code moved to bank 5. Bank 4 holds only game flow, screens and story.
- Task entries are b5 $8000/$8003/$8006/$8009 for slots 0 / 1–2 / 3–5 / 6–9, and b4 $8000 for slot 11.
- The RAM layout is the same.
- JP has 2 fewer enemy types (no BloodDrip or VerticalBlock).
- Port hooks in JP: frame $C069, split $C03F, trampoline $CED8, widescreen $C58E (built into `gen_jp/` by `translate.py --variant jp`).

## Overall

- **Header:** identical. MMC1, 8 × 16 KB PRG, 128 KB CHR.
- **Data banks, measured by bytes still in the same order** (difflib, which allows for data that moved):
  - **Bank 0:** identical.
  - **Bank 1:** 99% the same (1,439 bytes differ in place).
  - **Bank 2 (text, speech bubbles):** 81%. The JP text sits at the same addresses as the US text (the US build translated it in place), so the US labels carry over unchanged.
  - **Bank 3 (metasprites):** 91%.
  - **Bank 6 (sound, palettes):** 89%; the data has shifted.
- **CHR:** 1,190 of 8,192 tiles differ, in 24 of the 32 4 KB banks.
- **RAM:** every RAM and register address the two versions share is used the same way, so `monsterparty_jp/` reuses the US symbol names.

## Code layout

The code moved between the two switchable banks:

| | US | JP |
|---|---|---|
| Player (task slot 0) | bank 4 $8000–$9080 | bank 5 $8000–$905F |
| Item and "?" handlers | bank 4 $9081–$920A | bank 5 end, about $BC91–$BE2F |
| Game flow, screens, story (slot 11) | bank 4 $922C+ | bank 4 $8000+ |
| Enemies and bosses (slots 1–9) | bank 5 | bank 5, between the two player parts ($9060+) |
| Fixed bank | bank 7 | bank 7, mostly shifted down by up to $12D |

The task entry points change to match:

- **JP bank 5:** `jmp` table at $8000 (slot 0), $8003 (slots 1–2), $8006 (slots 3–5) and $8009 (slots 6–9).
- **JP bank 4:** $8000 (slot 11).
- `TaskBankTable` gives bank 5 for slots 0–9.

### Port hooks at their JP addresses

| Hook | US | JP | Notes |
|---|---|---|---|
| End of frame (`lda NmiDoneFlag`) | $C06D | $C069 | |
| Sprite-0 split | $C03F | $C03F | The code after it differs: a shorter delay (`ldx #$01`), the scroll is written before the CHR bank switches, and `UpdateSound` is called through a new wrapper that switches to bank 6 itself. |
| `jmp $0191` RAM trampoline | $CF07 | $CED8 | |
| Widescreen sprite-piece check | $C597 | $C58E | |
| NMI | $C1F3 | $C1EA | |
| Scheduler RTS dispatch | $C135 | $C12C | |
| Reset | $FFD8 | $FFD8 | JP: `sei; inc $FFFF; jmp $FF80` |

## Gameplay content

- **Enemy types:** JP has 34; the US has 36. The US-only types are the last two, 35 `AI_BloodDrip` and 36 `AI_VerticalBlock`. `EnemyAITable2` and `EnemyAITable` are 2 entries shorter, and so is everything else indexed by enemy type.
- **Bosses:** both have 33 boss types, in the same order (`BossInitLo/Hi`, `BossHandlersLo/Hi`). Several bosses' code differs a lot: 03 Pumpkin Ghost, 10 Sphinx Guardian, 15 Giant Samurai, 16/17 Living Dead, 21 Chameleon Man (init and handler), 27 Giant Spider II, 28/29 Giant Dragon, 31 Hand Creature, 32 Dark World Master. Handlers 22/23 enter 3 bytes into handler 20's code, where the US enters at the start.
- **After a boss:** the US code that runs after a non-final boss (`BossDefeated_NotFinal`, `MaybeSpawnQuestionMark`, `SpawnQuestionMark`, `BossLeaves`, 44 instructions) is a different, 12-instruction routine in JP.
- **Other tables:**
  - `b5_data_ac6e_indexed` has 4 entries in JP (US 6).
  - Items: `ItemHandlers` has 9 entries in both, but `Item2B_BertWeapon` and `QM_Health8` differ.
  - Drops: `Drop_None` differs.
- **Round 7 enemies:**
  - **Spirit (type $18):** a different enemy in JP, a hopping Chinese ghost with its own routine. It stands for 32 frames, or 8 when it's level with the player, then hops along an arc. It moves sideways while airborne (faster against Bert) and turns round after 48 steps. The US Spirit floats along a patrol, bobbing up and down.
  - **Umpire (type $1A):** looks like Jason in JP, but the AI is the same. The US only shifts its "is the player level with me" check 4 px.
- **Boss doors** (decoded from each room's door lists in bank 2: positions at $98AE, destinations at JP $9A20 / US $9A26, flags at JP $9B82 / US $9B8E; a flags byte with bit 7 set picks the boss, bits 4–6):
  - **Round 7:** the US has boss 1 behind door $0222, boss 2 behind $0A72 and boss 3 (Giant Spider II, the trap boss) behind $04C2. JP keeps boss 1 at $0222 but moves boss 2 to $045C, and $0A72 and $04C2 are empty boss rooms (boss 0, the "boss leaves" rooms). No JP door leads to boss 3, so **The Thing II can't be reached in the prototype**. Both versions need 2 bosses for round 7.
  - **Round 8:** the same bosses, but every boss door is at a slightly different spot. The round's layout must have changed.
  - **Round 2:** JP has 10 door entries, the US 13. The boss doors match.
  - **Rounds 1, 3–6 and 9:** boss doors identical.
- **Round-select password:** present and unchanged: the same code (chars 0, 5, 5, blank, then the round) and the same check. The password grid lists the codes left to right, 10 per row (first row あいうえお らりるれろ). So in JP the code is あ ら ら, a blank 4th position, then あ い う え お ら り る for rounds 1–8 (`b4_SpecialPasswords`). Round 9 can't be picked, same as the US. Like the US code, it only sets `RoundNumber`; the rest of the saved state isn't loaded.
- **Story:** Japanese text replaces the English pages at bank 4 $9F82–$A52D (`b4_StoryPagePtrs` plus the pages; 25 pages in both). `IntroStory` and story scene 16 differ slightly. `StorySceneTable` has 18 entries in both. Mark's Japanese name is Hiroshi, and Bert's Japanese name is Baryuu.
- **Fixed bank:** `CheckAllCollisions`, `Room_Update`, `StartBossTasks`, `PlayRoomMusic`, `DrawKeyIcon` and `Room_Init` each have small changed runs. The US `_func_d616` (55 instructions) has no JP counterpart.

## Boss code differences

These are the bosses whose code differs but which look the same (from `tools/jp/bosses.md`). Each was compared routine by routine against the US version. The JP labels in `monsterparty_jp/bank5.asm` carry the same notes, and the prototype names of the bosses that look different.

Two general changes come first, because they explain several of the bosses:

- **Multi-task boss:** `StartBossTasks` starts extra tasks for one boss.
  - **US:** Chameleon Man ($15) gets slots 7–9, for its 3 decoys.
  - **JP:** Bon-odori Zombies ($11) gets slots 7–8 instead. That is why there are three zombies, and why the prototype's Chameleon Man has no decoys.
- **Movement bounds:** the routine that moves a boss and checks the room edges takes a bounds mode in the US.
  - **US** (`b5_func_9b5c`): 3 modes. Mode 0 is Y $1F–$B1, X $0F–$F1; mode 1 is Y $2B–$A5, X $1B–$E5; mode 2 is Y $2F–$A1, X $1F–$E1.
  - **JP** (`b5_func_a887`): one set of bounds for every boss, Y $0F–$B0, X $0F–$F0.
- **Projectile kinds:** JP has 19 (US 20). The US kind $10 is used only by Giant Spider II (the only `ldx #$10` before a shot), and the kinds after it are numbered one higher than in JP. So a different number in the two sources can still mean the same projectile.

| Boss | What differs | Gameplay effect |
|---|---|---|
| $0F Giant Samurai | US: at 1 HP it sets ObjFlags bit 5 (skipped by collision checks) and plays a 48-frame death animation (frames $1B, $1B, $1A), then `b5_BossDefeated`. JP: no death sequence in the handler. JP also uses the wider shared bounds instead of mode 1. | Ends differently. The JP version can roam closer to the edges. |
| $15 Chameleon Man | US: 4 tasks. Init gives each task a start position and makes tasks 1–3 decoys (ObjFlags bit 6, skipped by collision checks). In the handler, each task at random (1 frame in 32) stops for 64–127 frames, then sets off toward the player. On the last frame of its hit timer (`TaskDropKind`, which the boss task counts down) it sets off away from the player. It turns back at the edges (mode 1 bounds). JP: 1 task. Init only sets the sprite CHR bank. The handler moves straight, reverses at an edge and animates. | Much simpler fight, consistent with the "unfinished" look. |
| $1D Giant Dragon (type 28 handler is `b5_BossLeaves` in both) | Init. US: the 7 body segments (objects 3–9) start at the head's position and are pre-moved 8 steps each, so the body starts spread along its path. JP: the segments start in a horizontal line from X $80, Y $80. Steering (every 8 frames). US: turns away from the walls (mode 2 bounds), otherwise heads for a random direction 1 time in 8 and toward the player otherwise, turning 2 of 16 steps. JP: walls at Y $30–$A0 / X $30–$E0, otherwise always toward the player, turning 1 step. | JP chases more predictably but turns more slowly. |
| $1F Hand Creature | Only the projectile number differs: kind $10 in JP is the US kind $11, the same projectile. | None. The user confirms it plays the same. |
| $20 Dark World Master | The whole background is his face. The boss object is his nose, and only the open nose can be damaged; the shots come from his mouth near the floor. US: the nose starts and normally stays closed; its init routine (`b5_Boss32_Init`) sets ObjFlags bit 5. The closed-nose sprite is drawn (`ObjDrawFlags` $81) and ObjFlags bit 5 is set, so collision checks skip it. At random (about 1 frame in 256 while closed) it opens for 128 frames: the sprite is hidden so the open nose in the background shows, and bit 5 is cleared, so it can be hit. It also animates through `b5_func_8d42`. JP: the init only sets the graphics bank and palette, and the handler only fires the 1-in-16 shot, with the same projectiles (kinds $11/$12 = US $12/$13). The nose never closes. The blue eyeballs therefore come from graphics/palette data, not this code. | Much easier in JP: the nose can be hit the whole fight instead of only during the short openings. |

The prototype bosses that look different: the only code changes found are noted per label in `monsterparty_jp/bank5.asm`. The large rewrites (Apeman Rider, The Mummy, and boss $1B, an unfinished, unused Totem Pole boss in place of Giant Spider II) aren't described here yet.

## State of `monsterparty_jp/`

- Rebuilds byte-identical (`monsterparty_jp/build.sh`).
- **Code found:**
  - Bank 4: 1,996 instructions (4,260 bytes).
  - Bank 5: 4,821 (10,895 bytes).
  - Bank 7: 3,030 (6,094 bytes).
- **Checks:** every CDL code byte is covered, no instruction sits on a CDL data byte, and every branch, `jsr` and `jmp` goes to a label. Every indirect jump's table is mapped and written as `.word label` / `<label` / `>label`. That includes the boss-init table, which the US source still has as plain bytes.
- **Names:** 455 descriptive labels carried over from the US source. These US names have no JP place, because the code was changed or doesn't exist there: `RoundStart_SetValues`, `BossDefeated_NotFinal`, `MaybeSpawnQuestionMark`, `SpawnQuestionMark`, `BossAnimFrames_9534`, `Boss21_Init_SetPos`, `Boss21_StartY`, `Boss21_StartX`, `AI_BloodDrip`, `AI_VerticalBlock`, `CheckAllCollisions`, `Room_RotatingJobs`.
- **Left as bytes:** these decode as clean code, but nothing in JP points to them.
  - b5 $AD57–$AD61.
  - b5 $B747–$B753.
  - b4 $8CDE–$8D3B: a pointer table no code reads.

  These look like the US's unused leftovers. Bank 7 $D268–$D366, $DD3F–$DD48 and $F657–$F6F5 are data.
- **CDL check (updated 2026-10-24):** the third JP CDL adds round 7 to the earlier play-through.
  - It logged 3,935 / 10,167 / 5,851 code bytes in banks 4 / 5 / 7, out of the 4,260 / 10,895 / 6,094 found statically.
  - Every byte it logged as code is shown as code, and no instruction sits on a byte it logged as data. So no new code turned up.
  - b4 $8CDE–$8D3B is logged as data, which confirms it's a table. The two clean-looking runs in bank 5 ($AD57, $B747) are still never touched.
  - Still never run: some story-scene code (b4 $8B3B–$8C03), round 2's bosses ($05–$07), round 6 (Chameleon Man), and boss $1B, which can't be reached.

# Per-function code differences

Instructions in one ROM with no matching instruction in the other, grouped into runs. "In / after" is the nearest label at or before the run.

## Code only in the US ROM (instructions with no counterpart in the other ROM)

### bank 4: 157 of 3501 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $82FC | $830F | 9 | `b4_Player_UpPressed+$F` |
| $8320 | $834A | 18 | `b4_Player_UpPressed+$33` |
| $863C | $8649 | 7 | `b4_label_863c` |
| $879C | $87A4 | 4 | `b4_label_879a+$2` |
| $87FA | $8802 | 4 | `b4_label_87ef+$B` |
| $8885 | $888C | 3 | `b4_label_8885` |
| $907B | $9080 | 3 | `b4_PlaceObject10+$A` |
| $916C | $917F | 9 | `b4_Item2B_BertWeapon` |
| $9212 | $922B | 10 | `b4_QM_Health8` |
| $943F | $9446 | 4 | `b4_Screen4_RoundStart+$24` |
| $99A0 | $99BF | 14 | `b4_label_999b+$5` |
| $9C88 | $9CA8 | 15 | `b4_IntroStory` |
| $AC49 | $AC76 | 19 | `b4_Screen9+$C` |
| $AE60 | $AE6A | 5 | `b4_StoryScene16_Step+$2` |
| $B05C | $B061 | 3 | `b4_label_b054+$8` |
| $B0D0 | $B0D5 | 3 | `b4_ClearTextBox_QueueRows` |
| $BFD8 | $BFDE | 3 | `b4_label_b1a0+$E38` |

(21 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)

### bank 5: 794 of 3665 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $8000 | $8008 | 3 | `b5_TaskEntryJumps` |
| $8084 | $8092 | 6 | `b5_label_807e+$6` |
| $82E5 | $82EA | 3 | `b5_func_82d9+$C` |
| $84C2 | $84DF | 14 | `b5_label_84ab+$17` |
| $8BCE | $8BE2 | 8 | `b5_label_8bcb+$3` |
| $8C16 | $8C23 | 6 | `b5_BossDefeated_Reward` |
| $8C5D | $8C67 | 4 | `b5_BossDefeated_Reward+$47` |
| $8C75 | $8C7F | 5 | `b5_BossDefeated_Reward+$5F` |
| $8C95 | $8CFA | 44 | `b5_BossDefeated_NotFinal` |
| $8DE4 | $8DE9 | 3 | `b5_func_8dd1+$13` |
| $8E22 | $8E37 | 10 | `b5_func_8e22` |
| $8E96 | $8E9A | 4 | `b5_label_8e92+$4` |
| $915E | $917B | 12 | `b5_Boss03_PumpkinGhost` |
| $91E3 | $9256 | 51 | `b5_label_91dd+$6` |
| $92F0 | $9306 | 11 | `b5_Boss10_SphinxGuardian+$6` |
| $9416 | $9451 | 27 | `b5_Boss15_GiantSamurai` |
| $9475 | $952E | 82 | `b5_label_946e+$7` |
| $9582 | $95A3 | 14 | `b5_Boss21_Init` |
| $95AC | $9621 | 53 | `b5_Boss21_ChameleonMan` |
| $96B7 | $9743 | 59 | `b5_label_96b6+$1` |
| $9765 | $97D6 | 54 | `b5_label_9733+$32` |
| $97E9 | $97EF | 3 | `b5_label_97d9+$10` |
| $980C | $9816 | 6 | `b5_label_97fe+$E` |
| $982B | $984C | 14 | `b5_func_982b` |
| $98A9 | $98BF | 11 | `b5_label_98a3+$6` |
| $98ED | $994D | 41 | `b5_Boss31_HandCreature+$25` |
| $99AD | $99B8 | 5 | `b5_label_99ad` |
| $9A8D | $9A9F | 9 | `b5_func_9a8d` |
| $9BAF | $9BDF | 22 | `b5_data_9b8f_indexed+$20` |
| $9C78 | $9C87 | 8 | `b5_func_9c78` |
| $A4E6 | $A4F0 | 5 | `b5_label_a4dd+$9` |
| $A53A | $A561 | 24 | `b5_func_a528+$12` |
| $A8BD | $A8CB | 6 | `b5_func_a8bd` |
| $A8D0 | $A8DE | 6 | `b5_func_a8d0` |
| $A8E3 | $A8F0 | 5 | `b5_func_a8e3` |
| $AA32 | $AA89 | 37 | `b5_AI_BloodDrip` |
| $AA91 | $AAC6 | 24 | `b5_AI_Spirit` |
| $ABB0 | $ABC6 | 13 | `b5_label_aba0+$10` |
| $AD20 | $AD6F | 36 | `b5_label_ad20` |
| $AD83 | $AD93 | 8 | `b5_func_ad7f+$4` |
| $ADAE | $ADB8 | 5 | `b5_label_ada8+$6` |
| $B0A9 | $B0B3 | 4 | `b5_Drop_None` |

(21 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)

### bank 7: 168 of 3138 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $C044 | $C069 | 15 | `_label_c03f+$5` |
| $C78D | $C79C | 7 | `CheckAllCollisions` |
| $C832 | $C84A | 11 | `CheckAllCollisions_OddFrame+$41` |
| $C8BC | $C8C1 | 3 | `SetPalette+$1` |
| $CCEE | $CCF2 | 3 | `SoundRunCommand+$2` |
| $CEC1 | $CEC6 | 3 | `SoundCmd_08+$B` |
| $D3E2 | $D3E9 | 4 | `Room_Init+$C` |
| $D4C9 | $D4D8 | 7 | `StartBossTasks+$E` |
| $D4F1 | $D4F9 | 4 | `PlayRoomMusic+$F` |
| $D583 | $D5A8 | 18 | `Room_Update+$11` |
| $D616 | $D68F | 55 | `_func_d616` |
| $D916 | $D91D | 4 | `DrawKeyIcon+$14` |
| $D934 | $D93B | 4 | `DrawKeyIcon+$32` |
| $DCA9 | $DCC0 | 13 | `_func_dca0+$9` |
| $FFD8 | $FFDE | 3 | `Reset` |

(13 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)


## Code only in the JP ROM (instructions with no counterpart in the other ROM)

### bank 4: 80 of 1996 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $8761 | $8780 | 14 | `b4_label_875c+$5` |
| $8A49 | $8A67 | 14 | `b4_IntroStory` |
| $9A08 | $9A35 | 19 | `b4_Screen9+$C` |
| $9A81 | $9A8E | 6 | `b4_label_9a7e+$3` |
| $9C29 | $9C3D | 9 | `b4_StoryScene16_Step+$2` |
| $9E2F | $9E34 | 3 | `b4_label_9e27+$8` |
| $9EA3 | $9EA8 | 3 | `b4_ClearTextBox_QueueRows` |

(11 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)

### bank 5: 522 of 4821 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $80D8 | $80DD | 3 | `b5_label_80b7+$21` |
| $8306 | $8319 | 9 | `b5_Player_UpPressed+$F` |
| $832A | $8330 | 3 | `b5_Player_UpPressed+$33` |
| $8622 | $862C | 5 | `b5_label_8622` |
| $877F | $8789 | 5 | `b5_label_877d+$2` |
| $87DF | $87E4 | 3 | `b5_label_87d4+$B` |
| $9059 | $905F | 4 | `b5_PlaceObject10+$A` |
| $9319 | $9332 | 13 | `b5_func_9319` |
| $933F | $9344 | 3 | `b5_func_9333+$C` |
| $951C | $952E | 9 | `b5_label_9505+$17` |
| $9BCF | $9BD8 | 4 | `b5_label_9bcc+$3` |
| $9C0C | $9C19 | 6 | `b5_BossDefeated_Reward` |
| $9C60 | $9C6F | 7 | `b5_BossDefeated_Reward+$54` |
| $9C85 | $9C9E | 12 | `b5_label_9c85` |
| $9D68 | $9D71 | 4 | `b5_label_9d59+$F` |
| $A065 | $A069 | 3 | `b5_label_a065` |
| $A0C5 | $A0E2 | 12 | `b5_Boss03_PumpkinGhost` |
| $A14A | $A152 | 4 | `b5_label_a145+$5` |
| $A1EC | $A1F7 | 5 | `b5_Boss10_SphinxGuardian+$6` |
| $A307 | $A311 | 4 | `b5_Boss15_GiantSamurai` |
| $A332 | $A38F | 41 | `b5_label_a332` |
| $A3DC | $A3F3 | 11 | `b5_label_a3dc` |
| $A489 | $A4C5 | 27 | `b5_label_a489` |
| $A4E7 | $A517 | 23 | `b5_label_a4ba+$2D` |
| $A54B | $A553 | 5 | `b5_label_a53d+$E` |
| $A568 | $A599 | 21 | `b5_func_a568` |
| $A5F3 | $A5FE | 5 | `b5_label_a5ed+$6` |
| $A62C | $A650 | 18 | `b5_Boss31_HandCreature+$25` |
| $A6B0 | $A6BB | 5 | `b5_label_a6b0` |
| $A790 | $A7B6 | 17 | `b5_func_a790` |
| $A7DA | $A7ED | 8 | `b5_label_a7d2+$8` |
| $A8B0 | $A8BA | 5 | `b5_label_a890+$20` |
| $A8E4 | $A8F1 | 7 | `b5_data_a8c4_indexed+$20` |
| $A977 | $A97D | 4 | `b5_func_a977` |
| $ACD9 | $ACE5 | 5 | `b5_AI_Patrol` |
| $B1E7 | $B1F1 | 5 | `b5_label_b1de+$9` |
| $B23B | $B263 | 20 | `b5_func_b229+$12` |
| $B431 | $B50F | 95 | `b5_AI_Spirit` |
| $B67E | $B68C | 6 | `b5_func_b67e` |
| $B691 | $B69F | 6 | `b5_func_b691` |
| $B6A4 | $B6B2 | 6 | `b5_func_b6a4` |
| $B9C4 | $B9D5 | 8 | `b5_func_b9c0+$4` |
| $BC51 | $BC5B | 4 | `b5_Drop_None` |
| $BD7C | $BD8F | 9 | `b5_Item2B_BertWeapon` |
| $BE22 | $BE2F | 5 | `b5_QM_Health8` |

(30 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)

### bank 7: 60 of 3030 instructions

| From | To | Instructions | In / after |
|---|---|---|---|
| $C044 | $C065 | 14 | `_label_c03f+$5` |
| $C888 | $C88D | 3 | `SetPalette+$1` |
| $CCBF | $CCC3 | 3 | `SoundRunCommand+$2` |
| $CE92 | $CE97 | 3 | `SoundCmd_08+$B` |
| $D457 | $D461 | 5 | `StartBossTasks+$E` |
| $D503 | $D513 | 8 | `Room_Update+$11` |
| $D807 | $D80E | 4 | `DrawKeyIcon+$14` |
| $D825 | $D82C | 4 | `DrawKeyIcon+$32` |
| $FFD8 | $FFDE | 3 | `Reset` |

(12 smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)


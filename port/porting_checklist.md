# Porting checklist: every hardware access in the game code

Generated from the disassembly. In the native port these are handled by `src/nes.cpp`.
Each one is either emulated there or replaced by a hook.

## Hand-replaced spots (hooks in the translated code)

Addresses are the US release's. In the Japanese prototype (`gen_jp/`) the same hooks sit at $C069 (frame), $C03F (split), $CED8 (`jmp $0191`) and $C58E (widescreen); `tools/translate.py --variant jp` places them there. The register list below is for the US code only.

The difficulty setting adds six more calls (`DIFF_HOOKS` in `tools/translate.py`, `Cpu::diff_*` in `src/nes.cpp`), at both versions' addresses: player damage begin/end, normal enemy and boss starting HP, the damage a normal enemy is about to take, and the drop kind. On Normal they do nothing.

MIDI music adds three read-only calls in the sound engine (`MUSIC_HOOKS`, `Cpu::music_*` in `src/nes.cpp`, `src/music.cpp`): a music channel's new note (US $CDF4 / prototype $CDC5), its volume register value ($CE5E / $CE2F) and its period ($CED9 / $CEAA). With MIDI music playing, `ram191_trampoline` also writes the music channels' volume registers as silent.

| Where | What the game does | What the port does |
|---|---|---|
| $C06D `WaitNextFrame_WaitNmi` | spins until the NMI sets `NmiDoneFlag` | `frame_hook`: present the frame, poll input, pace to 60.1 Hz, then enter the NMI handler like the hardware |
| $C03F `WaitNextFrame` | waits for the sprite-0 hit, then switches CHR banks and scroll for the status bar | `sprite0_split`: remembers the playfield settings; the renderer draws the status bar with the settings in effect at the end of the frame |
| $C000 `DelayX` | cycle-counted delay before the status-bar switch | runs as-is (harmless) |
| `WaitVblank`, other `PPU_STATUS` polls | busy-wait on the vblank / sprite-0 bits | `PPU_STATUS` reads alternate the vblank bit and always report the sprite-0 hit |
| $CF07 `jmp $0191` | sound engine runs `STA $40xx / RTS` it wrote into RAM | `ram191_trampoline`: does the store directly |

Widescreen drawing (`Nes::wide_margin` > 0). This hook only records information for the renderer, so the game runs exactly the same with widescreen on or off:

| Where | What the game does | What the port does |
|---|---|---|
| $C597 in `DrawSpritePieces` | hides a sprite piece whose 16-bit X is off the 256-pixel screen | `wide_sprite_piece`: notes where the piece really is, so the renderer can draw it in the side margins |

## Register accesses

### APU_DMC_RAW (1)

- bank 7 `MMC1_ResetShiftReg`: $C0D3 sta

### APU_FRAME (1)

- bank 7 `UpdateSound`: $CC50 sta

### APU_SND_CHN (1)

- bank 7 `MMC1_ResetShiftReg`: $C0EB sta

### JOYPAD1 (3)

- bank 7 `ReadJoypad`: $C62E sta, $C633 sta
- bank 7 `_label_c638`: $C638 lda

### MMC1_CHR0 (5)

- bank 7 `MMC1_SetChr0Bank`: $C5E5 sta, $C5E9 sta, $C5ED sta, $C5F1 sta, $C5F5 sta

### MMC1_CHR1 (5)

- bank 7 `MMC1_SetChr1Bank`: $C5F9 sta, $C5FD sta, $C601 sta, $C605 sta, $C609 sta

### MMC1_CONTROL (5)

- bank 7 `MMC1_SetControl`: $C5D1 sta, $C5D5 sta, $C5D9 sta, $C5DD sta, $C5E1 sta

### MMC1_PRG (8)

- bank 4 `b4_StoryPage24`: $BFD9 inc
- bank 7 `MMC1_ResetShiftReg`: $C0C0 sta
- bank 7 `MMC1_SetPrgBank`: $C60D sta, $C611 sta, $C615 sta, $C619 sta, $C61D sta
- bank 7 `Reset`: $FFD9 inc

### OAM_ADDR (1)

- bank 7 `NMI`: $C20A sta

### OAM_DMA (1)

- bank 7 `NMI`: $C20F sta

### PPU_ADDR (18)

- bank 7 `Nmi_ClearNametable`: $C2B1 sty, $C2B6 sta
- bank 7 `_label_c2f0`: $C2F2 sta, $C2F7 sta
- bank 7 `_label_c407`: $C40C sta, $C411 sta
- bank 7 `_label_c469`: $C46C sta, $C472 sta
- bank 7 `_label_c4a4`: $C4A7 sta, $C4AD sta
- bank 7 `_label_c4cc`: $C4D8 sta, $C4DD sta
- bank 7 `_label_c4e4`: $C507 sta, $C50C sta, $C50F sta, $C512 sta
- bank 7 `_label_c52b`: $C532 sta, $C539 sta

### PPU_CTRL (14)

- bank 7 `_label_c03f`: $C053 sta
- bank 7 `_label_c05b`: $C05F sta
- bank 7 `InitGame`: $C09E sta
- bank 7 `NMI`: $C1FC sta, $C22A sta
- bank 7 `InitPpuRegisters`: $C25E sta
- bank 7 `Nmi_ClearNametable`: $C2AE sta
- bank 7 `_label_c2e6`: $C2E6 sta
- bank 7 `_label_c3e6`: $C3E6 sta
- bank 7 `_label_c453`: $C45C sta
- bank 7 `_label_c477`: $C498 sta
- bank 7 `_label_c4cc`: $C4D3 sta
- bank 7 `_label_c54e`: $C54E sta
- bank 7 `ResetStage2`: $FF83 sta

### PPU_DATA (11)

- bank 7 `_label_c2bf`: $C2BF sta
- bank 7 `_label_c2cc`: $C2CC sta
- bank 7 `_label_c2fc`: $C2FE sta
- bank 7 `_label_c419`: $C41B sta
- bank 7 `_label_c477`: $C479 sta
- bank 7 `_label_c4a4`: $C4B3 sta
- bank 7 `_label_c4e4`: $C4E7 sta, $C4ED sta, $C4F4 sta, $C4FB sta
- bank 7 `_label_c552`: $C556 sta

### PPU_MASK (3)

- bank 7 `NMI`: $C205 sta, $C231 sta
- bank 7 `InitPpuRegisters`: $C265 sta

### PPU_SCROLL (5)

- bank 7 `_label_c03f`: $C04E stx
- bank 7 `NMI`: $C239 sta, $C240 sta
- bank 7 `InitPpuRegisters`: $C26C sta, $C271 sta

### PPU_STATUS (14)

- bank 7 `_label_c01b`: $C01B lda
- bank 7 `_label_c03f`: $C03F bit
- bank 7 `NMI`: $C234 lda
- bank 7 `WaitVblank`: $C277 lda
- bank 7 `_label_c27c`: $C27C lda
- bank 7 `Nmi_ClearNametable`: $C2A7 lda
- bank 7 `_label_c2cc`: $C2D2 lda
- bank 7 `Nmi_CopyVramBlock`: $C2DB lda
- bank 7 `_label_c36c`: $C36C lda
- bank 7 `_label_c407`: $C407 lda
- bank 7 `_label_c453`: $C453 lda
- bank 7 `_label_c477`: $C491 lda
- bank 7 `_label_c4cc`: $C4CC lda
- bank 7 `Nmi_WriteVramStripes`: $C526 lda


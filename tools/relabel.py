#!/usr/bin/env python3
"""Apply known names to the Monster Party disassembly.

usage: relabel.py MONSTERPARTY_DIR

Re-run after regenerating the disassembly with retrodisasm + merge_seeds.py.
Only renames symbols, adds comments, and splits .byte rows - output bytes are unchanged
(build.sh checks this).
"""
import re, sys, os

D = sys.argv[1]
FILES = ['monsterparty.asm'] + [f'bank{n}.asm' for n in range(8)]
src = {f: open(os.path.join(D, f), encoding='utf-8').read().split('\n') for f in FILES}

# --------------------------------------------------------------------------- symbols
RENAME = {
    # ---- fixed bank: startup
    '_label_ff80': 'ResetStage2',
    '_label_c09a': 'InitGame',
    'IRQ': 'MMC1_ResetShiftReg',            # IRQ vector also points here; IRQs are never used
    '_func_c25c': 'InitPpuRegisters',
    '_func_c277': 'WaitVblank',
    '_func_c085': 'SetupSprite0',
    '_func_c000': 'DelayX',
    # ---- fixed bank: MMC1 register writes (5 serial writes each)
    '_func_c5d1': 'MMC1_SetControl',
    '_func_c5e5': 'MMC1_SetChr0Bank',
    '_func_c5f9': 'MMC1_SetChr1Bank',
    '_func_c60d': 'MMC1_SetPrgBank',
    # ---- fixed bank: task scheduler
    '_label_c101': 'Scheduler_FrameStart',
    '_label_c105': 'Scheduler_NextTask',
    '_label_c11d': 'Scheduler_RunTask',
    '_func_c135': 'Scheduler_JumpToTask',
    '_label_c13e': 'Scheduler_EndFrame',
    '_data_c144_indexed': 'TaskInstanceTable',
    '_data_c150_indexed': 'TaskSlotIdTable',
    '_data_c15c_indexed': 'TaskEntryLo',
    '_data_c168_indexed': 'TaskEntryHi',
    '_data_c174_indexed': 'TaskBankTable',
    '_data_c180_indexed': 'TaskSlotCountTable',
    '_func_c18c': 'StartTask',
    '_func_c1da': 'StopTask',
    '_func_c1ea': 'WaitFrames',
    '_func_c016': 'WaitNextFrame',
    # ---- fixed bank: NMI-time PPU updates
    '_func_c282': 'ClearNametables',
    '_func_c284': 'ClearNametablesWithTile',
    '_func_c29f': 'Nmi_ClearNametable',
    '_func_c2d6': 'Nmi_CopyVramBlock',
    '_func_c329': 'QueueVramScript',
    '_func_c33d': 'Nmi_RunVramQueue',
    '_func_c36a': 'RunVramScript',
    '_func_c3cd': 'WriteVramRun',
    '_func_c4c4': 'Nmi_UploadPalette',
    '_func_c516': 'Nmi_WriteVramStripes',
    # ---- fixed bank: sprites
    '_func_c563': 'DrawSpritePieces',
    '_func_c565': 'DrawSpritePieces_Loop',
    '_func_c5b6': 'HideSprites',
    '_func_c5c6': 'HideAllSpritesAndWait',
    '_func_c699': 'DrawObject',
    '_func_c6c3': 'DrawMetasprite',
    '_func_c6ec': 'DrawMetaspriteList',
    '_func_c728': 'DrawAllObjects',
    # ---- fixed bank: input / misc
    '_func_c621': 'ReadJoypads',
    '_func_c62c': 'ReadJoypad',
    '_func_c66e': 'UpdateRandom',
    # ---- sound (engine code in fixed bank, data in bank 6)
    '_func_cb93': 'InitSound',
    '_func_cc4e': 'UpdateSound',
    # ---- switchable code banks: task entry points
    'b4_data_8000_indexed': 'b4_TaskEntryJumps',
    'b4_label_8006': 'b4_Task0_Main',
    'b4_label_922c': 'b4_Task11_Main',
    'b5_data_8000_indexed': 'b5_TaskEntryJumps',
    'b5_label_9d57': 'b5_Task1to2_Main',
    'b5_label_ab3e': 'b5_Task3to5_Main',
    'b5_label_8009': 'b5_Task6to9_Main',
    # ---- bank 4: story / credits text
    'b4_func_9e8b': 'b4_StartStoryPage',
    'b4_func_b0ef': 'b4_ReadStoryChar',
    'b4_func_b152': 'b4_PushStoryPointer',
}

VARS = {  # address -> name (RAM)
    0x08: 'TmpPtrLo', 0x09: 'TmpPtrHi',
    0x23: 'PpuCtrlShadow', 0x24: 'PpuMaskShadow',
    0x25: 'ScrollX', 0x26: 'ScrollY',
    0x32: 'NametableSelect', 0x33: 'VramStripeLen',
    0x36: 'NmiDoneFlag', 0x37: 'FrameCounter',
    0x38: 'CurrentTaskSlot', 0x39: 'CurrentTaskInstance', 0x3a: 'CurrentTaskSlotId',
    0x3b: 'CurrentPrgBank',
    0x3c: 'PlayfieldChr0Bank', 0x3d: 'PlayfieldChr1Bank',
    0x3e: 'VramQueueBank', 0x3f: 'ObjectGfxBank',
    0x41: 'StatusBarChr1Bank', 0x42: 'StatusBarChr0Bank',
    0x4a: 'ClearNametableHi', 0x4b: 'ClearNametableTile',
    0x0200: 'OamY', 0x0201: 'OamTile', 0x0202: 'OamAttr', 0x0203: 'OamX',
    0x0300: 'TaskState', 0x030c: 'TaskSleep', 0x0318: 'TaskArg0', 0x0324: 'TaskArg1',
    0x044b: 'PaletteDirty', 0x044e: 'VramQueueLen', 0x044f: 'VramQueue',
    0x045f: 'RngState', 0x0467: 'JoyHeld', 0x0469: 'JoyPressed',
    0x046b: 'PaletteBgColor', 0x046c: 'PaletteBuffer', 0x049d: 'VramStripeBuf',
    0x066b: 'StoryPtrDepth', 0x066d: 'StoryPtrLo', 0x066e: 'StoryPtrHi',
}

CONSTS = {'MMC1_CONTROL': '$9FFF', 'MMC1_CHR0': '$BFFF', 'MMC1_CHR1': '$DFFF', 'MMC1_PRG': '$FFFF'}

COMMENTS = {  # label -> comment block placed above it
    'ResetStage2': 'Reset continues here: switch to PRG mode 3 / bank $1E (=bank 6 with mode bits), then init.',
    'InitGame': 'Clear RAM, reset MMC1, init sound/PPU, start task 11 (top-level game flow), run scheduler.',
    'MMC1_ResetShiftReg': 'Writes $80 to MMC1 to reset its shift register. The IRQ vector ($C0C0) also points\n'
                          'here, mid-routine; IRQs are never enabled, so it is unused.',
    'MMC1_SetControl': 'MMC1 register write: A is shifted out 1 bit per write (5 writes).',
    'Scheduler_FrameStart': 'Cooperative task scheduler. 12 task slots ($0300,X = slot state, $FF/negative = free).\n'
                            'Each frame, every live slot whose TaskSleep is 0 gets its PRG bank switched in and\n'
                            'its entry point called through the RTS trick. Then WaitNextFrame.',
    'TaskInstanceTable': 'Per-slot tables (12 entries). Slot 0: bank 4 $8000. Slots 1-2: bank 5 $8000.\n'
                         'Slots 3-5: bank 5 $8003. Slots 6-9: bank 5 $8006. Slot 10: fixed bank Task10_Main.\n'
                         'Slot 11: bank 4 $8003 (started at boot).',
    'TaskEntryLo': 'Entry address minus 1 (for the RTS trick).',
    'StartTask': 'Start task type A: finds a free slot among TaskSlotCountTable[A] slots starting at A.\n'
                 'Copies $18/$19 to TaskArg0/1 and clears slot vars. Carry set = no free slot.',
    'StopTask': 'Free the task slot in A.',
    'WaitFrames': 'Wait A frames.',
    'WaitNextFrame': 'End of frame: wait for sprite-0 hit, switch CHR banks/scroll for the status bar,\n'
                     'update sound (bank 6), wait for NMI, read joypads, restore CurrentPrgBank.',
    'NMI': 'NMI: OAM DMA, pending PPU updates, scroll and playfield CHR banks, sets NmiDoneFlag.',
    'RunVramScript': 'Nametable/VRAM script interpreter (recursive; $FF ends, negative byte = sub-script pointer).',
    'UpdateRandom': '64-bit LFSR at RngState.',
    'ReadJoypads': 'Reads both pads. JoyHeld,X = buttons held, JoyPressed,X = newly pressed this frame.',
    'InitSound': 'Sound engine lives in the fixed bank; music/SFX data in bank 6.',
    'Multiply8x8': 'Unused (never executed): 8x8 multiply, A*X -> A:X. Left from development.',
    'InternalHeader': 'Licensee "internal header" (title, checksums) read by some dev/test hardware.',
    'Task10_Main': 'Task slot 10 entry (reached only through the scheduler RTS trick).',
    'b4_Task11_Main': 'Task slot 11: started at boot by InitGame; top-level game flow.',
    'b4_StartStoryPage': 'Start printing story page A (index into b4_StoryPagePtrs).',
    'b4_ReadStoryChar': 'Fetch next story text byte. Control codes: $FF = end (pop), $FD/$FB = call page n,\n'
                        '$FA = jump to page n, $FE = line break. Font: tools/text/main.tbl.',
}

def sub_all(pattern, repl):
    rx = re.compile(pattern)
    for f in FILES:
        src[f] = [rx.sub(repl, l) for l in src[f]]

# variable names: replace whole-symbol uses of _var_XXXX[_indexed] and raw z:$XX
var_names = {}
for f in FILES:
    for l in src[f]:
        m = re.match(r'^(_var_([0-9a-f]{4})(?:_indexed)?) = ', l)
        if m and int(m.group(2), 16) in VARS:
            var_names[m.group(1)] = VARS[int(m.group(2), 16)]
for old, new in {**RENAME, **var_names}.items():
    sub_all(r'(?<![\w.])' + re.escape(old) + r'(?!\w)', new)
for a, name in VARS.items():
    if a < 0x100:
        sub_all(r'\bz:\$' + f'{a:02X}' + r'\b(?!\w)', 'z:' + name)
# variables that had no definition yet
defined = {re.match(r'^(\w+) = ', l).group(1) for l in src['monsterparty.asm'] if re.match(r'^\w+ = ', l)}
extra = [f'{n} = ${a:04X}' for a, n in VARS.items() if n not in defined and any(n in l for f in FILES for l in src[f])]

# MMC1 register writes
sub_all(r'a:\$9FFF\b', 'a:MMC1_CONTROL')
sub_all(r'a:\$BFFF\b', 'a:MMC1_CHR0')
sub_all(r'a:_data_dfff\b', 'a:MMC1_CHR1')
sub_all(r'a:\$FFFF\b', 'a:MMC1_PRG')

# story pointer table: label at $B1B0 is just the high byte of entry 0
sub_all(r'\bb4_data_b1af_indexed\b', 'b4_StoryPagePtrs')
sub_all(r'\bb4_data_b1b0_indexed\b', 'b4_StoryPagePtrs+1')

# --------------------------------------------------------------------------- byte-row editing
BYTEROW = re.compile(r'^\.byte (.*?)\s*; \$([0-9A-F]{4,5})\s*$')

def row_info(l):
    m = BYTEROW.match(l)
    if not m: return None
    vals = [v.strip() for v in m.group(1).split(',') if v.strip()]
    return int(m.group(2), 16), vals

def fmt_row(addr, vals):
    body = '.byte ' + ', '.join(vals)
    return f'{body:<32} ; ${addr:04X}'

def split_at(lines, addr):
    """Make sure a line starts exactly at addr; return its index (or None)."""
    for i, l in enumerate(lines):
        info = row_info(l)
        if not info: continue
        start, vals = info
        if start == addr: return i
        if start < addr < start + len(vals):
            k = addr - start
            lines[i:i + 1] = [fmt_row(start, vals[:k]), fmt_row(addr, vals[k:])]
            return i + 1
    for i, l in enumerate(lines):          # instruction line with that address
        if re.search(r';\s*\$%04X\b' % addr, l) and not l.startswith('.byte'):
            return i
    return None

def add_label(f, addr, name, comment=None):
    lines = src[f]
    i = split_at(lines, addr)
    if i is None: sys.exit(f'{f}: no line at ${addr:04X} for {name}')
    block = ['']
    if comment: block += ['; ' + c for c in comment.split('\n')]
    block += [f'{name}:']
    lines[i:i] = block

def bytes_in(f, start, end):
    """Collect byte values for [start, end) from .byte rows."""
    out = {}
    for l in src[f]:
        info = row_info(l)
        if info:
            s, vals = info
            for k, v in enumerate(vals):
                if start <= s + k < end: out[s + k] = int(v.lstrip('$'), 16)
    return [out[a] for a in range(start, end)]

def replace_range(f, start, end, new_lines):
    lines = src[f]
    i = split_at(lines, start)
    j = split_at(lines, end)
    while j > i and (lines[j - 1] == '' or lines[j - 1].startswith(';') or re.match(r'^\w+:$', lines[j - 1])):
        j -= 1  # keep labels/comments that belong to the next item
    lines[i:j] = new_lines

# story page pointer table -> .word with labels
PAGES = [0xb1e1, 0xb22d, 0xb264, 0xb2da, 0xb30e, 0xb31d, 0xb329, 0xb336, 0xb392, 0xb3ba, 0xb405, 0xb428,
         0xb45c, 0xb4e3, 0xb50c, 0xb533, 0xb57a, 0xb597, 0xb5b5, 0xb601, 0xb61f, 0xb62c, 0xb66c, 0xb67b, 0xb6c9]
raw = bytes_in('bank4.asm', 0xb1af, 0xb1e1)
assert [raw[k] | raw[k + 1] << 8 for k in range(0, 50, 2)] == PAGES
# drop the old label lines at $B1AF/$B1B0
src['bank4.asm'] = [l for l in src['bank4.asm'] if l not in ('b4_StoryPagePtrs:', 'b4_StoryPagePtrs+1:')]
words = ['', '; Pointers to the 25 story/ending pages (text uses tools/text/main.tbl).', 'b4_StoryPagePtrs:']
words += [f'.word b4_StoryPage{n:02d}'.ljust(33) + f'; ${0xb1af + 2 * n:04X}' for n in range(len(PAGES))]
replace_range('bank4.asm', 0xb1af, 0xb1e1, words)
for n, p in enumerate(PAGES):
    add_label('bank4.asm', p, f'b4_StoryPage{n:02d}', 'Staff credits' if p == 0xb6c9 else None)

# bank 2 text
bubble_starts = [0x9d3a + 96 * k for k in range(9)] + [0xa09b + 96 * k for k in range(17)] + [0xa6fc, 0xa75d]
tbl = {0x01: ' ', 0xFA: '.', 0xFB: ',', 0xFC: '.', 0xFD: '!', 0xFE: "'"}
for k in range(26): tbl[0xD7 + k] = chr(65 + k)
for n, a in enumerate(bubble_starts):
    b = bytes_in('bank2.asm', a, a + 96)
    text = ' '.join(''.join(tbl.get(x, '') for x in b[r * 12 + 1:r * 12 + 11]).strip() for r in range(1, 7))
    text = re.sub(r'\s+', ' ', text).strip()
    first = 'Boss speech bubbles: raw 12x8 tile grids (96 bytes), font tools/text/bubble.tbl.\n' if n == 0 else ''
    add_label('bank2.asm', a, f'b2_BossBubble{n:02d}', f'{first}"{text}"')
for a, name in [(0x802b, 'b2_Text_Life'), (0x8033, 'b2_Text_Score'), (0x803c, 'b2_Text_High'),
                (0x8045, 'b2_Text_Round'), (0x8054, 'b2_Text_Key'), (0x8703, 'b2_Text_InputPassword'),
                (0x8dd9, 'b2_Text_RoundStart'), (0x91e5, 'b2_Text_YourPassword'), (0x95f1, 'b2_Text_GameOver'),
                (0x96d3, 'b2_Text_Continue')]:
    add_label('bank2.asm', a, name, 'main.tbl text' if name == 'b2_Text_Life' else None)

# fixed-bank labels that did not exist
add_label('bank7.asm', 0xd3ce, 'Task10_Main')

# task entry tables -> symbolic (entry - 1, for the RTS trick)
ENTRIES = ['b4_TaskEntryJumps'] + ['b5_TaskEntryJumps'] * 2 + ['b5_TaskEntryJumps+3'] * 3 + \
          ['b5_TaskEntryJumps+6'] * 4 + ['Task10_Main', 'b4_TaskEntryJumps+3']
lo = bytes_in('bank7.asm', 0xc15c, 0xc168); hi = bytes_in('bank7.asm', 0xc168, 0xc174)
assert [l | h << 8 for l, h in zip(lo, hi)] == [0x7fff] * 3 + [0x8002] * 3 + [0x8005] * 4 + [0xd3cd, 0x8002]
for start, op in ((0xc15c, '<'), (0xc168, '>')):
    replace_range('bank7.asm', start, start + 12, [f'.byte {op}({e}-1)'.ljust(33) + f'; ${start + k:04X}  slot {k}' for k, e in enumerate(ENTRIES)] + [''])
add_label('bank7.asm', 0xffdf, 'InternalHeader')

# unused multiply routine at $C656: show as code (same bytes)
lines = src['bank7.asm']
replace_range('bank7.asm', 0xc656, 0xc66e, ['', 'Multiply8x8:',
    '  sta z:$11                      ; $C656  85 11',
    '  stx z:$10                      ; $C658  86 10',
    '  lda #$00                       ; $C65A  A9 00',
    '  ldx #$08                       ; $C65C  A2 08',
    '@loop:',
    '  ror z:$10                      ; $C65E  66 10',
    '  bcc @skip                      ; $C660  90 03',
    '  clc                            ; $C662  18',
    '  adc z:$11                      ; $C663  65 11',
    '@skip:',
    '  ror a                          ; $C665  6A',
    '  dex                            ; $C666  CA',
    '  bne @loop                      ; $C667  D0 F5',
    '  tax                            ; $C669  AA',
    '  lda z:$10                      ; $C66A  A5 10',
    '  ror a                          ; $C66C  6A',
    '  rts                            ; $C66D  60', ''])

# comments above labels
for f in FILES:
    lines = src[f]
    out = []
    for l in lines:
        m = re.match(r'^(\w+):$', l)
        if m and m.group(1) in COMMENTS and m.group(1) not in ('Multiply8x8', 'InternalHeader', 'Task10_Main',
                                                              'b4_StoryPagePtrs'):
            out += ['; ' + c for c in COMMENTS[m.group(1)].split('\n')]
        out.append(l)
    src[f] = out
for f in ('bank7.asm',):
    for name in ('Multiply8x8', 'InternalHeader', 'Task10_Main'):
        k = src[f].index(f'{name}:')
        src[f][k:k] = ['; ' + c for c in COMMENTS[name].split('\n')]

# constants into main file
main = src['monsterparty.asm']
k = next(i for i, l in enumerate(main) if 'hardware registers' in l) + 1
main[k:k] = [f'{n} = {v}' for n, v in CONSTS.items()] + extra

# re-align comment column on instruction lines
ALIGN = re.compile(r'^(  \S.*?)\s+(; \$[0-9A-F]{4}\b.*)$')
for f in FILES:
    src[f] = [(lambda m: f'{m.group(1):<32} {m.group(2)}' if m else l)(ALIGN.match(l)) for l in src[f]]

for f in FILES:
    open(os.path.join(D, f), 'w', encoding='utf-8').write('\n'.join(src[f]))
print('renamed', len(RENAME) + len(var_names), 'symbols; new RAM defs:', extra)

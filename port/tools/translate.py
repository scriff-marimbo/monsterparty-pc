#!/usr/bin/env python3
"""Translate the Monster Party disassembly (banks 4, 5, 7) into C++.

usage: translate.py MONSTERPARTY_DIR OUT_DIR [--variant us|jp] [--check OLD_DIR]

  --variant jp   translate the Japanese prototype (monsterparty_jp/ -> gen_jp/): the code goes in
                 namespace jp, and the hooks below sit at the prototype's addresses

Every 6502 instruction becomes a few lines of C++ that update the emulated registers and RAM.
Each code bank becomes one function, `uint16_t run_bankN(Cpu&, uint16_t pc)`, with a label per
instruction and a `switch (pc)` at the top so any address (jsr target, RTS return point,
jump table entry, RTS-trick target) can be entered. Control flow:

  branch / jmp / jsr to the same bank  -> goto
  anything else (other bank, RTS, RTI, jmp (ind)) -> set pc and re-dispatch;
      if the new pc is outside this function, return it to the caller (main loop picks the bank)

The emulated 6502 stack in RAM $0100-$01FF is kept exactly, because the game relies on it
(RTS-trick dispatchers push addresses, the sound engine pushes its own return address).

Readability: RAM, ROM-table and hardware-register addresses are written with the disassembly's
names (ram_symbols.h, rom_symbols.h), gotos use the asm label names, and each named routine
gets a header with its comment from the .asm. None of this changes the generated statements:
--check OLD_DIR compares the output with an older translation after replacing every name with
its number, so the code itself is shown to be unchanged.

Special cases (see tools/porting_checklist.md), US address / JP prototype address:
  $C06D / $C069  frame hook: when the game starts waiting for NmiDoneFlag, end the frame and run the NMI
  $C03F / $C03F  sprite-0 split: the status-bar split point of the frame
  $CF07 / $CED8  jmp $0191: the sound engine's self-written `STA $40xx / RTS` in RAM -> direct APU write
  $C597 / $C58E  DrawSpritePieces' off-screen test: tell the widescreen renderer the piece's real position
  DIFF_HOOKS     difficulty: player damage, enemy and boss HP, reflected shots, drops (see below)
  MUSIC_HOOKS    MIDI music: the music channels' notes, volumes and periods (see below)
  REMIX_HOOKS    Remix Mode, mixing rounds: per-task round, per-object graphics, other rounds' arenas (see below)
  LB_HOOKS       leaderboards: the run's clock, the final boss, the result screen (see below)
  BOSS_HOOKS     new boss behaviour (prototype: the Totem Pole's bubble attack; see below)
  TRAINER_HOOKS  the launcher's Trainers (cheats): starting health / round / Bert, the player's jump, doors used (see below)
  LANG_HOOKS     the Language setting: speech bubbles' text (see below)
"""
import os, re, sys

SRC, OUT = sys.argv[1], sys.argv[2]
CHECK = sys.argv[sys.argv.index('--check') + 1] if '--check' in sys.argv else None
VARIANT = sys.argv[sys.argv.index('--variant') + 1] if '--variant' in sys.argv else 'us'
# hook addresses in the fixed bank, and the C++ namespace of the generated code ('' = global)
HOOKS = {'us': dict(frame=0xC06D, split=0xC03F, wide=0xC597, tramp=0xCF07, ns=''),
         'jp': dict(frame=0xC069, split=0xC03F, wide=0xC58E, tramp=0xCED8, ns='jp')}[VARIANT]
# Difficulty (src/nes.cpp, Cpu::diff_*): calls made just before these instructions run. On Normal
# they change nothing. (bank, address, expected instruction bytes, C++ statement, what it's for)
DIFF_HOOKS = {
    'us': [(4, 0x879A, 'A552', 'c.diff_player_damage_begin();', 'b4_PlayerHit: about to take damage'),
           (4, 0x87A9, 'A59E', 'c.diff_player_damage_end();', 'after the damage, before the clamp at 0'),
           (5, 0x80B2, '9DA803', 'c.A = c.diff_enemy_hp(c.A);', 'enemy spawn: HP from b5_EnemyHealthTable'),
           (5, 0x8AAD, '9D6003', 'c.A = c.diff_boss_hp(c.A);', 'boss spawn: HP from b5_EnemyStartHealth'),
           (5, 0x8500, 'A638', 'c.diff_enemy_hit();', 'b5_Enemy_ApplyDamage: damage in $10'),
           (5, 0xB062, 'BD5403', 'c.diff_drop();', 'b5_SpawnDrop: about to read TaskDropKind')],
    'jp': [(5, 0x877D, 'A552', 'c.diff_player_damage_begin();', 'b5_PlayerHit: about to take damage'),
           (5, 0x878E, 'A59E', 'c.diff_player_damage_end();', 'after the damage, before the clamp at 0'),
           (5, 0x90FA, '9DA803', 'c.A = c.diff_enemy_hp(c.A);', 'enemy spawn: HP from b5_EnemyHealthTable'),
           (5, 0x9AA8, '9D6003', 'c.A = c.diff_boss_hp(c.A);', 'boss spawn: HP from b5_EnemyStartHealth'),
           (5, 0x954F, 'A638', 'c.diff_enemy_hit();', 'b5_Enemy_ApplyDamage: damage in $10'),
           (5, 0xBC0A, 'BD5403', 'c.diff_drop();', 'b5_SpawnDrop: about to read TaskDropKind')],
}[VARIANT]
# Music as MIDI (src/music.cpp, Cpu::music_*): the sound engine's music channels (logical channels 0-3,
# one per APU channel; 4-8 are sound effects) report each new note, volume and period here. They
# only read registers and RAM, so the game runs the same whether MIDI music is on or off.
MUSIC_HOOKS = {
    'us': [(7, 0xCDF4, '9D2D01', 'c.music_key_on();', 'a new note from the sound data (X = channel)'),
           (7, 0xCE5E, 'A51E', 'c.music_volume();', 'channel X\'s register 0 value is in $1E'),
           (7, 0xCED9, '841E', 'c.music_period();', 'channel X\'s period: A = low byte, Y = high')],
    'jp': [(7, 0xCDC5, '9D2D01', 'c.music_key_on();', 'a new note from the sound data (X = channel)'),
           (7, 0xCE2F, 'A51E', 'c.music_volume();', 'channel X\'s register 0 value is in $1E'),
           (7, 0xCEAA, '841E', 'c.music_period();', 'channel X\'s period: A = low byte, Y = high')],
}[VARIANT]

# Remix Mode with "mix rounds" (src/remix.cpp, Cpu::remix_*): enemies and bosses from other rounds.
# A foreign task runs with its home round in RoundNumber (set before the scheduler runs it, put back
# before the next), tasks it starts inherit that, DrawObject draws foreign objects with their home
# round's metasprites, and RoundNumber*4+BossIdInRound uses the arena's round. With Remix off, or
# without mixing rounds, these change nothing.
REMIX_HOOKS = {
    'us': [(7, 0xC105, 'E638', 'c.remix_task_end();', 'Scheduler_NextTask: back from a task'),
           (7, 0xC11D, 'BD44C1', 'c.remix_task_begin();', 'Scheduler_RunTask: about to run slot X'),
           (7, 0xC19F, 'A518', 'c.remix_task_started();', 'StartTask_InitSlot: setting up slot Y'),
           (7, 0xC6AA, '0A', 'c.A = c.remix_draw_round(c.A);', 'DrawObject: A = RoundNumber, Y = object'),
           (7, 0xC6C3, '841A', 'c.remix_draw_untagged();', 'DrawMetasprite: not an object'),
           (7, 0xD0C0, '0A', 'c.A = c.remix_arena_round(c.A);', 'speech bubble: RoundNumber*4 + BossIdInRound'),
           (7, 0xD4C1, '0A', 'c.A = c.remix_arena_round(c.A);', 'StartBossTasks: BossId = RoundNumber*4 + BossIdInRound'),
           (7, 0xD4EA, '0A', 'c.A = c.remix_arena_round(c.A);', 'PlayRoomMusic: boss music')],
    'jp': [(7, 0xC0FC, 'E638', 'c.remix_task_end();', 'Scheduler_NextTask: back from a task'),
           (7, 0xC114, 'BD3BC1', 'c.remix_task_begin();', 'Scheduler_RunTask: about to run slot X'),
           (7, 0xC196, 'A518', 'c.remix_task_started();', 'StartTask_InitSlot: setting up slot Y'),
           (7, 0xC6A1, '0A', 'c.A = c.remix_draw_round(c.A);', 'DrawObject: A = RoundNumber, Y = object'),
           (7, 0xC6BA, '841A', 'c.remix_draw_untagged();', 'DrawMetasprite: not an object'),
           (7, 0xD091, '0A', 'c.A = c.remix_arena_round(c.A);', 'speech bubble: RoundNumber*4 + BossIdInRound'),
           (7, 0xD44F, '0A', 'c.A = c.remix_arena_round(c.A);', 'StartBossTasks: BossId = RoundNumber*4 + BossIdInRound'),
           (7, 0xD473, '0A', 'c.A = c.remix_arena_round(c.A);', 'PlayRoomMusic: boss music')],
}[VARIANT]

# Trainers (src/nes.cpp, Cpu::trainer_*): cheats chosen in the launcher. With all of them off these
# change nothing. (Infinite Life uses DIFF_HOOKS' diff_player_damage_end.) trainer_door_entered only
# notes which door was used, for Mark Round 6 Doors (drawing only).
TRAINER_HOOKS = {
    'us': [(4, 0x94C7, '859E', 'c.A = c.trainer_start_health(c.A);', 'continue after game over: PlayerHealth = A ($0C)'),
           (4, 0x9533, '859E', 'c.A = c.trainer_start_health(c.A);', 'title screen, for a new game: PlayerHealth = A ($0C)'),
           (4, 0x9584, '844D', 'c.trainer_title_done();', 'title screen done: Y = 3 new game, 2 password screen'),
           (4, 0x8488, 'EE5403', 'c.trainer_jump_step();', 'player jump: on to the next step of the jump table'),
           (4, 0x8498, '8516', 'c.A = c.trainer_jump(c.A);', 'player jump: A = this step\'s movement up ($80 = end)'),
           (7, 0xD968, 'B17D', 'c.trainer_door_entered();', 'TryEnterDoor: a door is entered, Y = its entry in the room\'s door list x 2')],
    'jp': [(4, 0x8298, '859E', 'c.A = c.trainer_start_health(c.A);', 'continue after game over: PlayerHealth = A ($0C)'),
           (4, 0x8304, '859E', 'c.A = c.trainer_start_health(c.A);', 'title screen, for a new game: PlayerHealth = A ($0C)'),
           (4, 0x8355, '844D', 'c.trainer_title_done();', 'title screen done: Y = 3 new game, 2 password screen'),
           (5, 0x846E, 'EE5403', 'c.trainer_jump_step();', 'player jump: on to the next step of the jump table'),
           (5, 0x847E, '8516', 'c.A = c.trainer_jump(c.A);', 'player jump: A = this step\'s movement up ($80 = end)'),
           (7, 0xD853, 'B17D', 'c.trainer_door_entered();', 'TryEnterDoor: a door is entered, Y = its entry in the room\'s door list x 2')],
}[VARIANT]

# New boss behaviour (src/remix.cpp, Cpu::totem_fire). Prototype only: the unused Totem Pole (boss
# type 27, only reachable with the launcher's Totem Pole option) just walks back and forth; the port
# gives him the Man-Eating Plant's bubbles, fired at the player from his top. Before his handler's
# last `jsr b5_func_9ce6`, the hook may "call" b5_BossFireProjectile: it pushes a return address of
# $A49E, so the rts comes back to this same instruction, where totem_fire() puts the registers back
# and returns false.
BOSS_HOOKS = {
    'us': [],
    'jp': [(5, 0xA49F, '20E69C', 'if (c.totem_fire()) { c.push16(0xA49E); goto b5_BossFireProjectile; }',
            'Totem Pole: maybe fire a bubble first (b5_BossFireProjectile, then back here)')],
}[VARIANT]


# Leaderboards (src/nes.cpp, Cpu::lb_*): a finished game is logged with its time and best score, and
# the credits' last page can go on to a result screen (the GAME OVER screen's room, with its menu
# loop replaced). With nes->lb.enabled off (or a cheating run) none of these change anything.
LB_HOOKS = {
    'us': [(4, 0x9425, 'A900', 'c.lb_round_start();', 'ROUND n START is over: the run\'s clock starts (round 1)'),
           (5, 0x8C16, 'A638', 'c.lb_boss_reward();', 'a boss is defeated: if it was the last one of round 9, note the frame'),
           (5, 0x8C80, 'A54C', 'c.lb_final_boss();', 'the final boss is defeated: log the time and the run\'s best score'),
           (4, 0xAEB9, '60', 'if (c.lb_credits_idle()) goto L_934A;', 'the credits\' last page waits: a button goes to the result screen (ShowScreen, ScreenId 6)'),
           (4, 0x9489, '2016C0', 'c.lb_screen_begin();', 'GAME OVER screen start: draw the result over it'),
           (4, 0x949A, '20E8CB', 'c.lb_screen_music();', 'GAME OVER screen: the password screen\'s music instead'),
           (4, 0x949D, 'A902', 'if (c.lb_screen_frame()) { c.push16(0x949C); return 0xC016; }', 'GAME OVER screen: result screen waits here for ever (no menu)')],
    'jp': [(4, 0x81FC, 'A900', 'c.lb_round_start();', 'ROUND n START is over: the run\'s clock starts (round 1)'),
           (5, 0x9C0C, 'A920', 'c.lb_boss_reward();', 'a boss is defeated: if it was the last one of round 9, note the frame'),
           (5, 0x9C70, 'A54C', 'c.lb_final_boss();', 'the final boss is defeated: log the time and the run\'s best score'),
           (4, 0x9C8C, '60', 'if (c.lb_credits_idle()) goto L_8121;', 'the credits\' last page waits: a button goes to the result screen (ShowScreen, ScreenId 6)'),
           (4, 0x825A, '2016C0', 'c.lb_screen_begin();', 'GAME OVER screen start: draw the result over it'),
           (4, 0x826B, '20B4CB', 'c.lb_screen_music();', 'GAME OVER screen: the password screen\'s music instead'),
           (4, 0x826E, 'A902', 'if (c.lb_screen_frame()) { c.push16(0x826D); return 0xC016; }', 'GAME OVER screen: result screen waits here for ever (no menu)')],
}[VARIANT]

# The Language setting (src/language.cpp, Cpu::lang_*): a speech bubble's page is rewritten in RAM
# just after the game copies it there. With the setting off (or on Game Default) they do nothing.
LANG_HOOKS = {
    'us': [(7, 0xD0C4, 'A8', 'c.lang_bubble_start();', 'speech bubble: A = RoundNumber*4 + BossIdInRound'),
           (7, 0xD12A, 'A206', 'c.lang_bubble_page();', 'a bubble page has been copied to RAM $055C')],
    'jp': [(7, 0xD095, 'A8', 'c.lang_bubble_start();', 'speech bubble: A = RoundNumber*4 + BossIdInRound'),
           (7, 0xD0FB, 'A206', 'c.lang_bubble_page();', 'a bubble page has been copied to RAM $055C')],
}[VARIANT]

NS = HOOKS['ns']
SRCNAME = os.path.basename(os.path.normpath(SRC))
os.makedirs(OUT, exist_ok=True)

INS = re.compile(r'^  ([a-z]{3})\b(.*?);\s*\$([0-9A-F]{4})\s+((?:[0-9A-F]{2} ?)+)')
LABEL = re.compile(r'^([A-Za-z_][\w]*):')
DEF = re.compile(r'^([A-Za-z_]\w*) = \$([0-9A-Fa-f]{1,4})\b')

ADDR = re.compile(r';\s*\$([0-9A-F]{4})\b')

# ---------------------------------------------------------------- read the disassembly
def read_bank(n):
    """ins: addr -> (mnemonic, bytes, asm text); labels: addr -> [names] (instructions only);
    all_labels: name -> addr (code and data); notes: name -> comment lines written above the label."""
    ins, labels, pending = {}, {}, []
    all_labels, notes, waiting, comment = {}, {}, [], []
    for l in open(f'{SRC}/bank{n}.asm', encoding='utf-8'):
        m = LABEL.match(l)
        if m and not l.startswith(' '):
            pending.append(m.group(1)); waiting.append(m.group(1))
            if comment: notes[m.group(1)] = comment
            comment = []
            continue
        if l.startswith(';'):
            comment.append(l[1:].strip()); continue
        comment = []
        a = ADDR.search(l)
        if a and (l.startswith('  ') or l.startswith('.')):
            for name in waiting: all_labels[name] = int(a.group(1), 16)
            waiting = []
        m = INS.match(l)
        if m:
            a = int(m.group(3), 16)
            ins[a] = (m.group(1), bytes.fromhex(m.group(4).replace(' ', '')), (m.group(1) + m.group(2)).strip())
            if pending: labels[a] = pending
            pending = []
        elif l.lstrip().startswith('.'):   # data row (.byte / .word, indented or not)
            pending = []
    return ins, labels, all_labels, notes

BANKS = (4, 5, 7)
code = {b: read_bank(b) for b in BANKS}
ramsyms, hwsyms = {}, {}
for l in open(f'{SRC}/monsterparty.asm', encoding='utf-8'):
    m = DEF.match(l)
    if m:
        v = int(m.group(2), 16)
        if v < 0x0800 and not m.group(1).startswith('_var_'):
            ramsyms.setdefault(v, m.group(1))
        elif 0x2000 <= v < 0x8000 and not m.group(1).startswith('_var_'):
            hwsyms.setdefault(v, m.group(1))

# ROM labels by address, per bank (first name wins), for naming table reads
romsyms = {b: {} for b in BANKS}
for b in BANKS:
    for name, a in code[b][2].items():
        romsyms[b].setdefault(a, name)

# C++ labels share one namespace per function; avoid the few that could collide with the
# generated labels or with macros from the headers (fall back to the L_XXXX label then)
RESERVED = {'dispatch', 'R', 'EOF', 'NULL', 'stdin', 'stdout', 'stderr', 'errno', 'assert', 'default', 'case'}
def cpp_label_ok(s): return re.fullmatch(r'[A-Za-z_]\w*', s) and s not in RESERVED and not re.fullmatch(r'L_[0-9A-F]{4}', s)

# ---------------------------------------------------------------- 6502 tables
MODES = {}
def op(name, *pairs):
    for o, m in pairs: MODES[o] = (name, m)
for name, base in [('ora', 0x00), ('and', 0x20), ('eor', 0x40), ('adc', 0x60), ('sta', 0x80), ('lda', 0xA0), ('cmp', 0xC0), ('sbc', 0xE0)]:
    for o, m in [(0x01, 'indx'), (0x05, 'zp'), (0x09, 'imm'), (0x0D, 'abs'), (0x11, 'indy'), (0x15, 'zpx'), (0x19, 'absy'), (0x1D, 'absx')]:
        if name == 'sta' and m == 'imm': continue
        MODES[base + o] = (name, m)
for name, base in [('asl', 0x00), ('rol', 0x20), ('lsr', 0x40), ('ror', 0x60)]:
    op(name, (base + 0x0A, 'acc'), (base + 0x06, 'zp'), (base + 0x16, 'zpx'), (base + 0x0E, 'abs'), (base + 0x1E, 'absx'))
op('bit', (0x24, 'zp'), (0x2C, 'abs'))
op('cpx', (0xE0, 'imm'), (0xE4, 'zp'), (0xEC, 'abs')); op('cpy', (0xC0, 'imm'), (0xC4, 'zp'), (0xCC, 'abs'))
op('dec', (0xC6, 'zp'), (0xD6, 'zpx'), (0xCE, 'abs'), (0xDE, 'absx')); op('inc', (0xE6, 'zp'), (0xF6, 'zpx'), (0xEE, 'abs'), (0xFE, 'absx'))
op('ldx', (0xA2, 'imm'), (0xA6, 'zp'), (0xB6, 'zpy'), (0xAE, 'abs'), (0xBE, 'absy'))
op('ldy', (0xA0, 'imm'), (0xA4, 'zp'), (0xB4, 'zpx'), (0xAC, 'abs'), (0xBC, 'absx'))
op('stx', (0x86, 'zp'), (0x96, 'zpy'), (0x8E, 'abs')); op('sty', (0x84, 'zp'), (0x94, 'zpx'), (0x8C, 'abs'))
op('jmp', (0x4C, 'abs'), (0x6C, 'ind')); op('jsr', (0x20, 'abs'))
for name, o in [('bpl', 0x10), ('bmi', 0x30), ('bvc', 0x50), ('bvs', 0x70), ('bcc', 0x90), ('bcs', 0xB0), ('bne', 0xD0), ('beq', 0xF0)]:
    MODES[o] = (name, 'rel')
for name, o in [('brk', 0x00), ('php', 0x08), ('clc', 0x18), ('plp', 0x28), ('sec', 0x38), ('rti', 0x40), ('pha', 0x48), ('cli', 0x58),
                ('rts', 0x60), ('pla', 0x68), ('sei', 0x78), ('dey', 0x88), ('txa', 0x8A), ('tya', 0x98), ('txs', 0x9A), ('tay', 0xA8),
                ('tax', 0xAA), ('clv', 0xB8), ('tsx', 0xBA), ('iny', 0xC8), ('dex', 0xCA), ('cld', 0xD8), ('inx', 0xE8), ('nop', 0xEA), ('sed', 0xF8)]:
    MODES[o] = (name, 'imp')
assert len(MODES) == 151, len(MODES)

# CPU cycles per instruction (base counts; page-crossing penalties are left out). The translated code adds them to
# c.cyc, which Cpu::frame_hook uses to run the end-of-frame UpdateRandom spin as many times as a real NES would.
RMW = {'asl', 'rol', 'lsr', 'ror', 'inc', 'dec'}
IMPLIED_CYCLES = {'pha': 3, 'php': 3, 'pla': 4, 'plp': 4, 'rts': 6, 'rti': 6, 'brk': 7}
def cycles(name, mode):
    if mode == 'rel': return 2                      # +1 when taken (added in the branch)
    if name == 'jsr': return 6
    if name == 'jmp': return 3 if mode == 'abs' else 5
    if mode in ('imp', 'acc'): return IMPLIED_CYCLES.get(name, 2)
    if name in RMW: return {'zp': 5, 'zpx': 6, 'abs': 6, 'absx': 7}[mode]
    store = name in ('sta', 'stx', 'sty')
    return {'imm': 2, 'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'absx': 5 if store else 4, 'absy': 5 if store else 4,
            'indx': 6, 'indy': 6 if store else 5}[mode]
assert all(cycles(*MODES[o]) for o in MODES)

def h2(v): return f'0x{v:02X}'
def h4(v): return f'0x{v:04X}'
def ramref(a):
    s = ramsyms.get(a)
    return f'R({s})' if s else f'ram[{h4(a)}]'

def h2(v): return f'0x{v:02X}'
def h4(v): return f'0x{v:04X}'
def ramref(a):
    s = ramsyms.get(a)
    return f'R({s})' if s else f'ram[{h4(a)}]'
def ramname(a, width):
    """name of a RAM base address in an indexed access, else the number"""
    s = ramsyms.get(a)
    return s if s else (h2(a) if width == 2 else h4(a))

CUR_BANK = None
def romname(a):
    """rom::Label / hw::Register for an absolute address >= $0800, else the number"""
    if CUR_BANK in (4, 5) and 0x8000 <= a < 0xC000 and a in romsyms[CUR_BANK]:
        return 'rom::' + romsyms[CUR_BANK][a]
    if a >= 0xC000 and a in romsyms[7]:
        return 'rom::' + romsyms[7][a]
    if a in hwsyms:
        return 'hw::' + hwsyms[a]
    return h4(a)

# ---------------------------------------------------------------- operand access
def addr_expr(mode, bs):
    """C++ expression for the effective address (uint16_t), or None for direct RAM forms."""
    v8 = bs[1] if len(bs) > 1 else 0
    v16 = bs[1] | bs[2] << 8 if len(bs) > 2 else 0
    return {
        'zp': h4(v8), 'zpx': f'uint8_t({h2(v8)} + c.X)', 'zpy': f'uint8_t({h2(v8)} + c.Y)',
        'abs': romname(v16), 'absx': f'uint16_t({romname(v16)} + c.X)', 'absy': f'uint16_t({romname(v16)} + c.Y)',
        'indx': f'c.zp16(uint8_t({h2(v8)} + c.X))', 'indy': f'uint16_t(c.zp16({h2(v8)}) + c.Y)',
    }[mode]

def read(mode, bs):
    v8 = bs[1] if len(bs) > 1 else 0
    v16 = bs[1] | bs[2] << 8 if len(bs) > 2 else 0
    if mode == 'imm': return h2(v8)
    if mode == 'zp': return ramref(v8)
    if mode == 'zpx': return f'ram[uint8_t({ramname(v8, 2)} + c.X)]'
    if mode == 'zpy': return f'ram[uint8_t({ramname(v8, 2)} + c.Y)]'
    if mode == 'abs':
        return ramref(v16) if v16 < 0x0800 else f'c.rd({romname(v16)})'
    if mode in ('absx', 'absy') and v16 + 0xFF < 0x0800:
        return f'ram[{ramname(v16, 4)} + c.{"X" if mode == "absx" else "Y"}]'
    return f'c.rd({addr_expr(mode, bs)})'

def write(mode, bs, val):
    v8 = bs[1] if len(bs) > 1 else 0
    v16 = bs[1] | bs[2] << 8 if len(bs) > 2 else 0
    if mode == 'zp': return f'{ramref(v8)} = {val};'
    if mode == 'zpx': return f'ram[uint8_t({ramname(v8, 2)} + c.X)] = {val};'
    if mode == 'zpy': return f'ram[uint8_t({ramname(v8, 2)} + c.Y)] = {val};'
    if mode == 'abs' and v16 < 0x0800: return f'{ramref(v16)} = {val};'
    if mode in ('absx', 'absy') and v16 + 0xFF < 0x0800:
        return f'ram[{ramname(v16, 4)} + c.{"X" if mode == "absx" else "Y"}] = {val};'
    return f'c.wr({addr_expr(mode, bs)}, {val});'

# ---------------------------------------------------------------- translate one bank
def bank_range(b): return (0xC000, 0x10000) if b == 7 else (0x8000, 0xC000)
AUTO = re.compile(r'^(b[45]_)?_?(label|data)_[0-9a-f]{4}(_indexed)?$')   # plain branch-target labels: no header
COL = 60   # column of the trailing "// $addr asm" comment

def branch_target(a, bs):
    v8 = bs[1]
    return (a + 2 + (v8 - 256 if v8 > 127 else v8)) & 0xFFFF

def emit_bank(b):
    global CUR_BANK
    CUR_BANK = b
    ins, labels, _, notes = code[b]
    lo, hi = bank_range(b)
    def here(a): return lo <= a < hi

    # gotos only go to instructions in this bank; give those the asm label name
    targets = set()
    for a, (m, bs, text) in ins.items():
        name, mode = MODES[bs[0]]
        if mode == 'rel': t = branch_target(a, bs)
        elif name in ('jmp', 'jsr') and mode == 'abs': t = bs[1] | bs[2] << 8
        else: continue
        if here(t) and t in ins: targets.add(t)
    gname = {}
    for t in targets:
        ok = [n for n in labels.get(t, []) if cpp_label_ok(n)]
        gname[t] = ok[0] if ok else f'L_{t:04X}'
    L = lambda a: f'L_{a:04X}'
    def goto_or_return(t):
        # direct transfer: same function -> goto; switchable range from the fixed bank -> return
        return f'goto {gname[t]};' if here(t) and t in ins else f'return {h4(t)};'

    out = []
    w = out.append
    w(f'// Generated by tools/port/translate.py from {SRCNAME}/bank{b}.asm. Do not edit.')
    w('#include "cpu.h"')
    w('#include "ram_symbols.h"')
    w('#include "rom_symbols.h"')
    w('')
    if NS: w(f'namespace {NS} {{')
    w(f'uint16_t run_bank{b}(Cpu& c, uint16_t pc) {{')
    w('    uint8_t* ram = c.ram;')
    w('dispatch:')
    w('    if (!c.in_bank(pc, %d)) return pc;' % b)
    w('    switch (pc) {')
    for a in sorted(ins):
        w(f'    case {h4(a)}: goto {L(a)};')
    w('    default: return c.unknown_pc(pc, %d);' % b)
    w('    }')

    def line(a, stmt, text):
        code_part = f'{L(a) + ":":<8}{stmt}'
        w(code_part.ljust(max(COL, len(code_part) + 2)) + f'// ${a:04X} {text}')

    for a in sorted(ins):
        m, bs, text = ins[a]
        name, mode = MODES[bs[0]]
        text = re.sub(r'\b[az]:', '', text)
        # --- labels: a header for each named routine, a C++ label where a goto lands
        for lab in labels.get(a, []):
            if not AUTO.match(lab):
                w('')
                for note in notes.get(lab, []):
                    w(f'// {note}')
                w(f'// ---- {lab} '.ljust(COL + 20, '-'))
            elif lab != gname.get(a):
                w(f'    // {lab}:')
        if a in gname and gname[a] != L(a):
            w(f'{gname[a]}:')
        v8 = bs[1] if len(bs) > 1 else 0
        v16 = bs[1] | bs[2] << 8 if len(bs) > 2 else 0
        nxt = a + len(bs)
        # --- special cases
        special = []
        if b == 7 and a == HOOKS['frame']:
            assert name == 'lda' and bs == bytes([0xA5, 0x36]), 'frame hook must be lda NmiDoneFlag'
            special.append(f'if (ram[0x36] == 0) {{ pc = c.frame_hook({h4(a)}); goto dispatch; }}   // end of frame: present, input, run NMI')
        if b == 7 and a == HOOKS['split']:
            assert name == 'bit' and bs == bytes([0x2C, 0x02, 0x20]), 'split hook must be bit PPU_STATUS'
            special.append('c.sprite0_split();   // the game is about to wait for the sprite-0 hit (status bar split)')
        if b == 7 and a == HOOKS['wide']:
            assert name == 'beq', 'widescreen hook must be the beq after the off-screen test'
            special.append('c.wide_sprite_piece();   // widescreen: note where a sprite piece culled as off screen really is')
        if b == 7 and a == HOOKS['tramp']:
            assert name == 'jmp' and bs == bytes([0x4C, 0x91, 0x01]), 'trampoline hook must be jmp $0191'
        for kind, hooks in (('difficulty', DIFF_HOOKS), ('MIDI music', MUSIC_HOOKS), ('Remix Mode', REMIX_HOOKS), ('trainer', TRAINER_HOOKS), ('boss', BOSS_HOOKS), ('leaderboards', LB_HOOKS), ('language', LANG_HOOKS)):
            for hb, ha, hbytes, hstmt, hwhy in hooks:
                if hb == b and ha == a:
                    assert bs.hex().upper() == hbytes, f'{kind} hook at ${a:04X}: expected {hbytes}, found {bs.hex().upper()}'
                    special.append(f'{hstmt}   // {kind}: {hwhy}')
        if b == 7 and a == HOOKS['tramp'] and name == 'jmp':
            special.append('c.cyc += 3 + 4 + 6;   // jmp $0191, then STA $40xx and RTS there')
            special.append('c.ram191_trampoline();   // RAM $0191: STA $40xx / RTS (self-modifying sound code)')
            special.append('pc = c.pull16() + 1; goto dispatch;')
        if special:
            w(f'{L(a)}:  // ${a:04X} {text}')
            for s in special: w('        ' + s)
            if name == 'jmp': w(''); continue
        # --- generic: one C++ statement (or statement group) per instruction
        end = False   # unconditional transfer: leave a gap after it
        if name == 'lda': s = f'c.A = {read(mode, bs)}; c.nz(c.A);'
        elif name == 'ldx': s = f'c.X = {read(mode, bs)}; c.nz(c.X);'
        elif name == 'ldy': s = f'c.Y = {read(mode, bs)}; c.nz(c.Y);'
        elif name == 'sta': s = write(mode, bs, 'c.A')
        elif name == 'stx': s = write(mode, bs, 'c.X')
        elif name == 'sty': s = write(mode, bs, 'c.Y')
        elif name in ('ora', 'and', 'eor'):
            o = {'ora': '|', 'and': '&', 'eor': '^'}[name]
            s = f'c.A {o}= {read(mode, bs)}; c.nz(c.A);'
        elif name == 'adc': s = f'c.adc({read(mode, bs)});'
        elif name == 'sbc': s = f'c.adc(uint8_t(~{read(mode, bs)}));'
        elif name == 'cmp': s = f'c.cmp(c.A, {read(mode, bs)});'
        elif name == 'cpx': s = f'c.cmp(c.X, {read(mode, bs)});'
        elif name == 'cpy': s = f'c.cmp(c.Y, {read(mode, bs)});'
        elif name == 'bit': s = f'c.bit({read(mode, bs)});'
        elif name in ('asl', 'lsr', 'rol', 'ror', 'inc', 'dec'):
            fn = name
            if mode == 'acc': s = f'c.A = c.{fn}(c.A);'
            else: s = f'{{ uint8_t v = c.{fn}({read(mode, bs)}); {write(mode, bs, "v")} }}'
        elif name in ('inx', 'iny', 'dex', 'dey'):
            r = name[2].upper(); d = '+' if name[0] == 'i' else '-'
            s = f'c.{r} = uint8_t(c.{r} {d} 1); c.nz(c.{r});'
        elif name in ('tax', 'tay', 'txa', 'tya', 'tsx'):
            src, dst = {'tax': ('A', 'X'), 'tay': ('A', 'Y'), 'txa': ('X', 'A'), 'tya': ('Y', 'A'), 'tsx': ('SP', 'X')}[name]
            s = f'c.{dst} = c.{src}; c.nz(c.{dst});'
        elif name == 'txs': s = 'c.SP = c.X;'
        elif name == 'pha': s = 'c.push(c.A);'
        elif name == 'php': s = 'c.push(c.getP(true));'
        elif name == 'pla': s = 'c.A = c.pull(); c.nz(c.A);'
        elif name == 'plp': s = 'c.setP(c.pull());'
        elif name in ('clc', 'sec', 'cli', 'sei', 'clv', 'cld', 'sed'):
            f, v = {'clc': ('C', 0), 'sec': ('C', 1), 'cli': ('I', 0), 'sei': ('I', 1), 'clv': ('V', 0), 'cld': ('D', 0), 'sed': ('D', 1)}[name]
            s = f'c.{f} = {v};'
        elif name == 'nop': s = ';'
        elif mode == 'rel':
            t = branch_target(a, bs)
            cond = {'bpl': '!c.N', 'bmi': 'c.N', 'bvc': '!c.V', 'bvs': 'c.V', 'bcc': '!c.C', 'bcs': 'c.C', 'bne': '!c.Z', 'beq': 'c.Z'}[name]
            s = f'if ({cond}) {{ c.cyc++; {goto_or_return(t)} }}'
        elif name == 'jmp' and mode == 'abs':
            s = goto_or_return(v16); end = True
        elif name == 'jmp' and mode == 'ind':
            s = f'pc = c.zp16({h2(v8)}); goto dispatch;' if v16 < 0x100 else f'pc = c.rd16({romname(v16)}); goto dispatch;'
            end = True
        elif name == 'jsr':
            s = f'c.push16({h4(nxt - 1)}); {goto_or_return(v16)}'
        elif name == 'rts':
            s = 'pc = c.pull16() + 1; goto dispatch;'; end = True
        elif name == 'rti':
            s = 'c.setP(c.pull()); pc = c.pull16(); goto dispatch;'; end = True
        elif name == 'brk':
            s = f'return c.unknown_pc({h4(a)}, {b});   // BRK is never executed by this game'; end = True
        else:
            raise SystemExit(f'unhandled {name} {mode} at ${a:04X}')
        s = f'c.cyc += {cycles(name, mode)}; ' + s
        if special: w('        ' + s)
        else: line(a, s, text)
        if end: w('')
    w('    return 0;   // not reached')
    w('}')
    if NS: w(f'}}   // namespace {NS}')
    open(f'{OUT}/bank{b}.cpp', 'w').write('\n'.join(out) + '\n')
    return len(ins)

for hb, ha, *_ in DIFF_HOOKS + MUSIC_HOOKS + REMIX_HOOKS + TRAINER_HOOKS + BOSS_HOOKS + LB_HOOKS + LANG_HOOKS:
    assert ha in code[hb][0], f'hook ${ha:04X} is not an instruction in bank {hb}'
total = sum(emit_bank(b) for b in BANKS)

# ---------------------------------------------------------------- symbol headers
with open(f'{OUT}/ram_symbols.h', 'w') as f:
    f.write(f'// Generated by tools/port/translate.py from {SRCNAME}/monsterparty.asm. Do not edit.\n#pragma once\n#include <cstdint>\n')
    if NS: f.write(f'namespace {NS} {{\n')
    for a, s in sorted(ramsyms.items()):
        f.write(f'constexpr uint16_t {s} = 0x{a:04X};\n')
    if NS: f.write(f'}}   // namespace {NS}\n')
with open(f'{OUT}/rom_symbols.h', 'w') as f:
    f.write(f'// Generated by tools/port/translate.py from {SRCNAME}/*.asm. Do not edit.\n'
            '// ROM labels (code and data) and hardware registers, as named in the disassembly.\n'
            '// Banks 4 and 5 share $8000-$BFFF; their labels carry b4_ / b5_ prefixes.\n'
            '#pragma once\n#include <cstdint>\n')
    if NS: f.write(f'namespace {NS} {{\n')
    f.write('namespace hw {\n')
    for a, s in sorted(hwsyms.items()):
        f.write(f'constexpr uint16_t {s} = 0x{a:04X};\n')
    f.write('}\nnamespace rom {\n')
    seen = set()
    for b in BANKS:
        f.write(f'// bank {b}\n')
        for a, s in sorted((a, s) for a, s in romsyms[b].items()):
            if s in seen: continue
            seen.add(s)
            f.write(f'constexpr uint16_t {s} = 0x{a:04X};\n')
    f.write('}\n')
    if NS: f.write(f'}}   // namespace {NS}\n')
print(f'translated {total} instructions from banks {BANKS} -> {OUT}')

# ---------------------------------------------------------------- --check: same code as an older translation?
if CHECK:
    TOK = re.compile(r'0x[0-9A-Fa-f]+|\d+|::|[A-Za-z_]\w*|\S')
    names = {}
    for a, s in ramsyms.items(): names[s] = a
    for a, s in hwsyms.items(): names['hw::' + s] = a
    for b in BANKS:
        for a, s in romsyms[b].items(): names.setdefault('rom::' + s, a)
    def tokens(path, gotos):
        src = open(path).read()
        src = re.sub(r'//[^\n]*', '', src)
        src = re.sub(r'#include[^\n]*', '', src)
        t = TOK.findall(src)
        out, i = [], 0
        while i < len(t):
            x = t[i]
            if x in ('rom', 'hw') and i + 2 < len(t) and t[i + 1] == '::':
                out.append(names[x + '::' + t[i + 2]]); i += 3; continue
            # drop label definitions (NAME :) - but keep "case N :" and "default :"
            if re.fullmatch(r'[A-Za-z_]\w*', x) and i + 1 < len(t) and t[i + 1] == ':' and x not in ('default', 'public'):
                i += 2; continue
            if x == 'goto' and t[i + 1] != 'dispatch':
                out += ['goto', gotos.get(t[i + 1], t[i + 1])]; i += 2; continue
            if x.startswith('0x') or x.isdigit(): out.append(int(x, 0))
            elif x in names and t[i - 1] not in ('.', '::'): out.append(names[x])
            else: out.append(x)
            i += 1
        return out
    bad = 0
    for b in BANKS:
        gotos = {n: f'L_{a:04X}' for n, a in code[b][2].items()}
        new, old = tokens(f'{OUT}/bank{b}.cpp', gotos), tokens(f'{CHECK}/bank{b}.cpp', gotos)
        if new != old:
            bad += 1
            k = next(i for i, (x, y) in enumerate(zip(new + [None], old + [None])) if x != y)
            print(f'bank{b}: code differs at token {k}: new {new[k-5:k+5]} old {old[k-5:k+5]}')
        else:
            print(f'bank{b}: same code as {CHECK} ({len(new)} tokens)')
    if bad: sys.exit(1)

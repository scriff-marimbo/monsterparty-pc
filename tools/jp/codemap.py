#!/usr/bin/env python3
"""Find the JP prototype's code: instruction starts in banks 4, 5 and 7.

Seeds: instructions matched to the US disassembly (map_us_jp.json), the vectors, and extra seeds
from a file (one 'bank $ADDR reason' per line). From each seed, trace: follow branches, jsr and jmp
(inside the same bank, or into the fixed bank), stop at rts/rti/jmp (ind)/brk/bad opcode.
Then report: overlapping instructions, CDL-logged code bytes not covered, and code bytes the CDL
logged as data. Writes codemap.json: {bank: [[addr, size, reason], ...]}.
"""
import json, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from m6502 import decode

ROM, CDL, MAP, EXTRA, OUT = sys.argv[1:6]
rom = open(ROM, 'rb').read()[16:16 + 0x20000]
cdl = open(CDL, 'rb').read()[9:]
def base(n): return 0xC000 if n == 7 else 0x8000
def mem(n): return rom[n * 0x4000:(n + 1) * 0x4000]

seeds = []
for ub, ua, jb, ja in json.load(open(MAP)):
    seeds.append((jb, ja, f'aligned US b{ub} ${ua:04X}'))
b7 = mem(7)
for v, name in ((0xFFFA, 'NMI'), (0xFFFC, 'RESET'), (0xFFFE, 'IRQ')):
    t = b7[v - 0xC000] | b7[v - 0xC000 + 1] << 8
    if t >= 0xC000: seeds.append((7, t, f'{name} vector'))
for line in open(EXTRA):
    line = line.split('#')[0].strip()
    if not line: continue
    bk, a, *why = line.split()
    seeds.append((int(bk), int(a.lstrip('$'), 16), ' '.join(why) or 'extra'))

starts = {n: {} for n in (4, 5, 7)}     # addr -> (size, reason)
owner = {n: {} for n in (4, 5, 7)}      # byte addr -> instruction start
problems = []
work = list(seeds)
while work:
    n, a, why = work.pop()
    if n not in starts: continue
    m = mem(n); b = base(n)
    while True:
        if a in starts[n]: break
        off = a - b
        if not (0 <= off < 0x4000):
            problems.append(f'b{n} ${a:04X}: outside bank ({why})'); break
        d = decode(m, off, b)
        if d is None:
            problems.append(f'b{n} ${a:04X}: not an opcode ({why})'); break
        name, mode, size, v = d
        clash = [x for x in range(a, a + size) if x in owner[n]]
        if clash:
            problems.append(f'b{n} ${a:04X}: overlaps instruction at ${owner[n][clash[0]]:04X} ({why})'); break
        starts[n][a] = (size, why)
        for x in range(a, a + size): owner[n][x] = a
        if name == 'brk':
            problems.append(f'b{n} ${a:04X}: brk ({why})')
            break
        nxt = a + size
        if mode == 'rel':
            work.append((n, v, f'branch from ${a:04X}'))
        elif name in ('jsr', 'jmp') and mode == 'abs':
            if v >= 0xC000: work.append((7, v, f'{name} from b{n} ${a:04X}'))
            elif 0x8000 <= v < 0xC000 and n != 7: work.append((n, v, f'{name} from ${a:04X}'))
        if name in ('rts', 'rti') or (name == 'jmp'): break
        a = nxt

# ---- reports
for n in (4, 5, 7):
    b = base(n)
    cov = sum(s for s, _ in starts[n].values())
    unc, run = [], None
    for off in range(0x4000):
        f = cdl[n * 0x4000 + off]
        is_code = f & 1
        a = b + off
        if is_code and a not in owner[n]:
            if run and run[1] == a - 1: run[1] = a
            else:
                run = [a, a]; unc.append(run)
    data_hit = sorted({owner[n][b + off] for off in range(0x4000)
                       if (cdl[n * 0x4000 + off] & 3) == 2 and (b + off) in owner[n]})
    print(f'bank {n}: {len(starts[n])} instructions, {cov} bytes of code; '
          f'CDL code not covered: {sum(r[1]-r[0]+1 for r in unc)} bytes in {len(unc)} runs; '
          f'instructions on CDL-data bytes: {len(data_hit)}')
    for r in unc[:40]: print(f'    uncovered CDL code ${r[0]:04X}-${r[1]:04X}')
    for a in data_hit[:20]: print(f'    instruction on CDL data: ${a:04X} ({starts[n][a][1]})')
print(f'{len(problems)} problems')
for p in problems[:80]: print('  ', p)
json.dump({n: sorted([a, s, w] for a, (s, w) in starts[n].items()) for n in starts}, open(OUT, 'w'))

#!/usr/bin/env python3
"""Code differences between the US release and the JP prototype, by function, from the alignment.
usage: diffreport.py US_ASM_DIR JP_ASM_DIR CODEMAP MAP"""
import json, re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from usasm import read_bank
USD, JPD, CM, MAP = sys.argv[1:5]
mp = json.load(open(MAP))
us_al = {(a, b) for a, b, c, d in mp}; jp_al = {(c, d) for a, b, c, d in mp}
cm = json.load(open(CM))
def runs(bank, starts_sizes, aligned, labels):
    """group unaligned instructions into runs; return [(start, end, n_ins, label_before)]"""
    out, cur = [], None
    last_lab = None
    for a, s in starts_sizes:
        if a in labels: last_lab = (labels[a][0], a)
        if (bank, a) in aligned:
            if cur: out.append(cur); cur = None
            continue
        if cur and a == cur[1]: cur = (cur[0], a + s, cur[2] + 1, cur[3])
        else:
            if cur: out.append(cur)
            cur = (a, a + s, 1, last_lab)
    if cur: out.append(cur)
    return out
for side in ('US', 'JP'):
    print(f'\n## Code only in the {side} ROM (instructions with no counterpart in the other ROM)\n')
    tot = 0
    for b in (4, 5, 7):
        B = read_bank(f'{USD if side == "US" else JPD}/bank{b}.asm', b)
        if side == 'US': ss = [(a, len(bs)) for a, bs in B.ins]
        else: ss = sorted((a, s) for a, s, _ in cm[str(b)])
        rs = runs(b, ss, us_al if side == 'US' else jp_al, B.labels)
        n = sum(r[2] for r in rs); tot += n
        print(f'### bank {b}: {n} of {len(ss)} instructions\n')
        print('| From | To | Instructions | In / after |\n|---|---|---|---|')
        for a, e, k, lab in rs:
            if k < 3: continue
            where = f'{lab[0]}' + (f'+${a - lab[1]:X}' if lab and a != lab[1] else '') if lab else '-'
            print(f'| ${a:04X} | ${e - 1:04X} | {k} | `{where}` |')
        small = sum(1 for r in rs if r[2] < 3)
        if small: print(f'\n({small} smaller runs of 1-2 instructions not listed: mostly changed constants, addressing or single inserted instructions.)')
        print()

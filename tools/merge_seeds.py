#!/usr/bin/env python3
"""Merge per-seed retrodisasm runs into one MMC1 disassembly.
usage: merge.py OUTDIR base.asm 4=seed4.asm 5=seed5.asm ...  (fixed bank taken from first seed run)"""
import re, sys, os
outdir, base, *extra = sys.argv[1:]
seedruns = {int(k): v for k, v in (e.split('=') for e in extra)}
FIXED = 7
ASSIGN = re.compile(r'^([A-Za-z_]\w*) = (\S+)')
LABEL = re.compile(r'^(_[a-z_]+?_([0-9a-f]{4})(?:_indexed)?):')
SYM = re.compile(r'\b(_(?:func|label|data|jump_table|jump_engine)_([0-9a-f]{4})(?:_indexed)?)\b')

def parse(path):
    pre, consts, segs, cur = [], {}, {}, None
    for l in open(path):
        m = ASSIGN.match(l)
        if m:
            consts.setdefault(m.group(1), m.group(2)); continue
        if l.startswith('.segment'):
            cur = l.split()[1].strip('"'); segs[cur] = [l]; continue
        (segs[cur] if cur else pre).append(l)
    return pre, consts, segs

bp, bc, bs = parse(base)
runs = {k: parse(v) for k, v in seedruns.items()}
consts = dict(bc)
for _, c, _ in runs.values():
    for k, v in c.items():
        if consts.setdefault(k, v) != v: sys.exit(f'conflicting const {k}')

banks = {}
for n in range(8):
    seg = f'PRG_BANK_{n}'
    src = runs[n][2] if n in runs else bs
    banks[n] = list(src[seg])
first = next(iter(runs))
banks[FIXED] = list(runs[first][2][f'PRG_BANK_{FIXED}'])

def is_switch(addr): return int(addr, 16) < 0xC000

# 1. prefix switchable-bank labels with their bank number, inside that bank
for n in range(FIXED):
    def ren(m, n=n):
        return f'b{n}{m.group(1)}' if is_switch(m.group(2)) else m.group(1)
    banks[n] = [SYM.sub(ren, l) for l in banks[n]]

# 2. fixed bank: references into the switchable window are ambiguous -> raw addresses
def raw(m):
    return f'${m.group(2).upper()}' if is_switch(m.group(2)) else m.group(1)
banks[FIXED] = [l if LABEL.match(l) else SYM.sub(raw, l) for l in banks[FIXED]]
banks[FIXED] = [l for l in banks[FIXED] if not (LABEL.match(l) and is_switch(LABEL.match(l).group(2)))]

# 3. fixed-bank labels referenced elsewhere but missing: insert at the instruction with that address
defined = {LABEL.match(l).group(1) for n in banks for l in banks[n] if LABEL.match(l)}
used = {m.group(1) for n in banks for l in banks[n] for m in SYM.finditer(l)
        if not LABEL.match(l) and not is_switch(m.group(2))}
missing = sorted(used - defined)
fallback = []
for name in missing:
    addr = '$' + SYM.match(name).group(2).upper()
    for i, l in enumerate(banks[FIXED]):
        if re.search(r';\s*' + re.escape(addr) + r'\b', l) and not l.lstrip().startswith('.byte'):
            banks[FIXED][i:i] = ['\n', f'{name}:\n']; break
    else:
        fallback.append(name); consts[name] = addr

os.makedirs(outdir, exist_ok=True)
stem = 'monsterparty'
with open(f'{outdir}/{stem}.asm', 'w') as f:
    f.writelines(bp)
    f.writelines(bs['HEADER'])
    f.write('\n; ---- hardware registers, RAM variables, constants ----\n')
    for k, v in consts.items(): f.write(f'{k} = {v}\n')
    for n in range(8):
        f.write(f'\n.include "bank{n}.asm"\n')
    f.write('\n.segment "TILES"\n.incbin "chr.bin"\n')
for n in range(8):
    open(f'{outdir}/bank{n}.asm', 'w').writelines(banks[n])
print('inserted labels:', len(missing) - len(fallback), 'fallback consts:', fallback)

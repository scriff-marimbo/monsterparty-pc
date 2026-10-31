#!/usr/bin/env python3
"""Align the US disassembly's instructions with the JP prototype's bytes.

Tokens ignore absolute addresses inside ROM ($8000+), which move between the builds, and keep
everything else (opcode, RAM/register addresses, immediates, branch offsets). Runs of K equal
tokens that are unique on both sides are anchors; each anchor is extended forwards and backwards
while tokens keep matching. Output: JSON map of US (bank, addr) -> JP (bank, addr) per instruction.
"""
import json, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from m6502 import decode
from usasm import read_bank

US_ASM, JP_ROM, OUT = sys.argv[1:4]
K = 6
jp = open(JP_ROM, 'rb').read()[16:16 + 0x20000]
def bank_mem(rom, n): return rom[n * 0x4000:(n + 1) * 0x4000]
def base(n): return 0xC000 if n == 7 else 0x8000

def token(d, addr, rel_ok=True):
    name, mode, n, v = d
    if mode in ('abs', 'absx', 'absy', 'ind') and v >= 0x8000:
        v = 'R'
    if mode == 'rel':
        v = v - (addr + 2)          # branch offset, position independent
    return (name, mode, v)

# ---- JP: decode at every offset; tokens + next offset
jp_tok = {}
for n in (4, 5, 7):
    mem = bank_mem(jp, n); b = base(n)
    tok = [None] * len(mem)
    for off in range(len(mem)):
        d = decode(mem, off, b)
        if d: tok[off] = (token(d, b + off), d[2])
    jp_tok[n] = tok

def jp_seq(n, off, k):
    out = []
    tok = jp_tok[n]
    for _ in range(k):
        if off >= len(tok) or tok[off] is None: return None
        t, size = tok[off]; out.append(t); off += size
    return tuple(out)

index = {}
for n in (4, 5, 7):
    for off in range(0x4000):
        s = jp_seq(n, off, K)
        if s: index.setdefault(s, []).append((n, off))

# ---- US
us = {n: read_bank(f'{US_ASM}/bank{n}.asm', n) for n in (4, 5, 7)}
us_tok = {}
for n, b in us.items():
    toks = []
    for addr, bs in b.ins:
        d = decode(bs, 0, addr)
        toks.append(token(d, addr) if d else None)
    us_tok[n] = toks
us_index = {}
for n in us:
    t = us_tok[n]
    for i in range(len(t) - K + 1):
        us_index.setdefault(tuple(t[i:i + K]), []).append((n, i))

mapping = {}   # (usbank, i) -> (jpbank, off)
used = set()   # (jpbank, off)
anchors = []
for key, uslist in us_index.items():
    if len(uslist) == 1 and len(index.get(key, [])) == 1:
        anchors.append((uslist[0], index[key][0]))
anchors.sort()

def contiguous(n, i, j):   # US instructions i and j=i+1 adjacent in memory?
    a, bs = us[n].ins[i]
    return j < len(us[n].ins) and us[n].ins[j][0] == a + len(bs)

for (un, ui), (jn, jo) in anchors:
    if (un, ui) in mapping: continue
    # forwards
    i, o = ui, jo
    while i < len(us_tok[un]) and (un, i) not in mapping and (jn, o) not in used:
        t = jp_tok[jn][o] if o < 0x4000 else None
        if t is None or t[0] != us_tok[un][i]: break
        mapping[(un, i)] = (jn, o); used.add((jn, o))
        size = t[1]
        if not contiguous(un, i, i + 1): i += 1; o += size; break
        i += 1; o += size
    # backwards: need JP instruction start before jo; try sizes 1-3
    i, o = ui - 1, jo
    while i >= 0 and (un, i) not in mapping and contiguous(un, i, i + 1):
        size = len(us[un].ins[i][1])
        po = o - size
        if po < 0: break
        t = jp_tok[jn][po]
        if t is None or t[1] != size or t[0] != us_tok[un][i] or (jn, po) in used: break
        mapping[(un, i)] = (jn, po); used.add((jn, po))
        i -= 1; o = po

out = []
for (un, ui), (jn, jo) in sorted(mapping.items()):
    out.append([un, us[un].ins[ui][0], jn, base(jn) + jo])
json.dump(out, open(OUT, 'w'))
for n in (4, 5, 7):
    tot = len(us[n].ins); m = sum(1 for (un, ui) in mapping if un == n)
    cross = sum(1 for (un, ui), (jn, jo) in mapping.items() if un == n and jn != n)
    print(f'US bank {n}: {m}/{tot} instructions matched ({100*m/tot:.1f}%), {cross} in another JP bank; anchors {sum(1 for a in anchors if a[0][0]==n)}')

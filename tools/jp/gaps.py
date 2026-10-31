#!/usr/bin/env python3
"""Explain the JP prototype's non-code bytes in banks 4, 5, 7: match them against the US data bytes."""
import json, sys, difflib
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from usasm import read_bank
from m6502 import decode
US_ASM, US_ROM, JP_ROM, CODEMAP, OUT = sys.argv[1:6]
us = open(US_ROM, 'rb').read()[16:16 + 0x20000]
jp = open(JP_ROM, 'rb').read()[16:16 + 0x20000]
cm = json.load(open(CODEMAP))
def base(n): return 0xC000 if n == 7 else 0x8000
res = {}
for n in (4, 5, 7):
    ub = read_bank(f'{US_ASM}/bank{n}.asm', n)
    us_code = set()
    for a, bs in ub.ins: us_code.update(range(a, a + len(bs)))
    jp_code = set()
    for a, s, w in cm[str(n)]: jp_code.update(range(a, a + s))
    res[n] = []
    # contiguous runs
    def runs(code, b):
        out, cur = [], None
        for a in range(b, b + 0x4000):
            if a in code: cur = None; continue
            if cur and cur[1] == a - 1: cur[1] = a
            else: cur = [a, a]; out.append(cur)
        return out
    jr = runs(jp_code, base(n))
    usd = b''.join(us[n*0x4000 + r[0]-base(n): n*0x4000 + r[1]-base(n)+1] for r in runs(us_code, base(n)))
    # also allow matching against the other switchable bank's US data
    other = {4: 5, 5: 4, 7: 7}[n]
    ub2 = read_bank(f'{US_ASM}/bank{other}.asm', other)
    c2 = set()
    for a, bs in ub2.ins: c2.update(range(a, a + len(bs)))
    usd2 = b''.join(us[other*0x4000 + r[0]-base(other): other*0x4000 + r[1]-base(other)+1] for r in runs(c2, base(other)))
    total_unexpl = 0
    for r in jr:
        seg = jp[n*0x4000 + r[0]-base(n): n*0x4000 + r[1]-base(n)+1]
        explained = bytearray(len(seg))
        for src in (usd, usd2):
            sm = difflib.SequenceMatcher(None, seg, src, autojunk=False)
            for blk in sm.get_matching_blocks():
                if blk.size >= 6:
                    for k in range(blk.a, blk.a + blk.size): explained[k] = 1
        # $FF / $00 filler runs count as explained
        k = 0
        while k < len(seg):
            j = k
            while j < len(seg) and seg[j] == seg[k]: j += 1
            if j - k >= 8 and seg[k] in (0xFF, 0x00):
                for x in range(k, j): explained[x] = 1
            k = j
        un = []
        cur = None
        for k, e in enumerate(explained):
            if e: cur = None; continue
            if cur and cur[1] == k - 1: cur[1] = k
            else: cur = [k, k]; un.append(cur)
        for u in un:
            res[n].append([r[0] + u[0], r[0] + u[1]])
            total_unexpl += u[1] - u[0] + 1
    print(f'bank {n}: {total_unexpl} non-code JP bytes with no US data counterpart, in {len(res[n])} runs')
    for a, b in res[n]:
        if b - a + 1 >= 12: print(f'   ${a:04X}-${b:04X} ({b-a+1})')
json.dump(res, open(OUT, 'w'))

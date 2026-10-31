#!/usr/bin/env python3
"""For each JP non-code run with no US counterpart: CDL data bytes, code references into it,
and how code-like it looks (linear decode)."""
import json, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from m6502 import decode
JP_ROM, CDL, CODEMAP, GAPS = sys.argv[1:5]
MIN = int(sys.argv[5]) if len(sys.argv) > 5 else 12
jp = open(JP_ROM, 'rb').read()[16:16 + 0x20000]
cdl = open(CDL, 'rb').read()[9:]
cm = json.load(open(CODEMAP)); gaps = json.load(open(GAPS))
def base(n): return 0xC000 if n == 7 else 0x8000
# operand references from all JP code
refs = {}
for n in ('4', '5', '7'):
    b = base(int(n)); m = jp[int(n)*0x4000:(int(n)+1)*0x4000]
    for a, s, w in cm[n]:
        d = decode(m, a - b, b)
        if d and d[1] in ('abs', 'absx', 'absy', 'ind') and d[3] >= 0x8000:
            refs.setdefault(d[3], []).append((int(n), a, d[0]))
for n in ('4', '5', '7'):
    nb = int(n); b = base(nb); m = jp[nb*0x4000:(nb+1)*0x4000]
    for lo, hi in gaps[n]:
        if hi - lo + 1 < MIN: continue
        cdld = sum(1 for a in range(lo, hi + 1) if cdl[nb*0x4000 + a - b] & 2)
        rin = sorted({(sb, sa, nm, t) for t in range(lo - 2, hi + 1) for (sb, sa, nm) in refs.get(t, []) if (nb == 7) == (t >= 0xC000) and (sb == nb or t >= 0xC000 or sb == 7)})
        # linear decode
        a, ok, bad, end = lo, 0, 0, None
        while a <= hi:
            d = decode(m, a - b, b)
            if not d: bad += 1; a += 1; continue
            ok += 1
            if d[0] in ('rts', 'rti') or (d[0] == 'jmp'): end = a
            a += d[2]
        print(f'b{n} ${lo:04X}-${hi:04X} ({hi-lo+1:4d}) CDLdata={cdld:4d} refs={len(rin):2d} ins={ok} bad={bad} ' +
              (' '.join(f'[b{sb} ${sa:04X} {nm} ${t:04X}]' for sb, sa, nm, t in rin[:3])))

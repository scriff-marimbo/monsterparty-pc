#!/usr/bin/env python3
"""List every indirect transfer in the JP code map (jmp (ind), and pha/pha/rts dispatch), with the
table addresses read just before it, and whether tables.json covers them."""
import json, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from m6502 import decode
rom = open('jp.nes', 'rb').read()[16:16 + 0x20000]
cm = json.load(open('codemap.json')); T = json.load(open('tables.json'))
covered = {}
for t in T:
    for e in t['entries']: covered[(t['jp_bank'], e['jp_addr'])] = t['us_label']
def base(n): return 0xC000 if n == 7 else 0x8000
for n in ('4', '5', '7'):
    nb = int(n); b = base(nb); m = rom[nb*0x4000:(nb+1)*0x4000]
    ins = sorted(cm[n])
    for i, (a, s, w) in enumerate(ins):
        d = decode(m, a - b, b)
        site = None
        if d[0] == 'jmp' and d[1] == 'ind': site = 'jmp (ind)'
        if d[0] == 'rts' and i >= 2:
            prev = [decode(m, x[0] - b, b)[0] for x in ins[max(0, i-4):i]]
            if prev.count('pha') >= 2 and prev[-1] == 'pha': site = 'pha/pha/rts'
        if not site: continue
        tabs = []
        for x in ins[max(0, i-10):i]:
            dd = decode(m, x[0] - b, b)
            if dd[0] == 'lda' and dd[1] in ('absx', 'absy') and dd[3] >= 0x8000:
                tb = nb if dd[3] < 0xC000 else 7
                tabs.append((dd[3], covered.get((tb, dd[3])) or covered.get((tb, dd[3]-1)) or '-'))
        print(f'b{n} ${a:04X} {site:12s} tables: ' + ', '.join(f'${t:04X} [{c}]' for t, c in tabs))

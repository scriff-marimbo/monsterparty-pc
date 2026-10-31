#!/usr/bin/env python3
"""peek.py ROM BANK START END: linear disassembly of a range (marks traced code starts from codemap.json)."""
import json, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from m6502 import decode
rom = open(sys.argv[1], 'rb').read()[16:16 + 0x20000]
n = int(sys.argv[2]); lo = int(sys.argv[3], 16); hi = int(sys.argv[4], 16)
b = 0xC000 if n == 7 else 0x8000
m = rom[n*0x4000:(n+1)*0x4000]
starts = {}
try:
    for a, s, w in json.load(open('codemap.json'))[str(n)]: starts[a] = w
except Exception: pass
a = lo
while a <= hi:
    d = decode(m, a - b, b)
    raw = m[a-b:a-b+(d[2] if d else 1)]
    tag = 'C' if a in starts else ' '
    if d:
        name, mode, size, v = d
        opnd = '' if v is None else (f'${v:04X}' if mode in ('abs','absx','absy','ind','rel') else (f'#${v:02X}' if mode=='imm' else f'${v:02X}'))
        opnd += {'absx': ',X', 'absy': ',Y', 'zpx': ',X', 'zpy': ',Y', 'indx': ',X)', 'indy': '),Y'}.get(mode, '')
        if mode in ('indx', 'indy'): opnd = '(' + opnd
        if mode == 'ind': opnd = '(' + opnd + ')'
        print(f'{tag} ${a:04X}  {" ".join(f"{x:02X}" for x in raw):9s} {name} {opnd}')
        a += size
    else:
        print(f'{tag} ${a:04X}  {raw[0]:02X}        .byte'); a += 1

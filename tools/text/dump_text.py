#!/usr/bin/env python3
"""Find and decode text in Monster Party using .tbl tables.

usage: dump_text.py ROM.nes [out.txt]

Tables (same folder):
  main.tbl   - title/password/ending font: 0-9, A-Z, a-z, punctuation. FE = line break, FF = end of page.
  bubble.tbl - boss speech-bubble font (A=$D7). Bubbles are stored as raw 12x8 tile grids.
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))

def load_tbl(name):
    t = {}
    for line in open(os.path.join(HERE, name), encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or '=' not in line: continue
        k, v = line.split('=', 1)
        t[int(k.lstrip('/*'), 16)] = v
    return t

def addr(i):
    bank, off = divmod(i, 0x4000)
    return bank, (0xC000 if bank == 7 else 0x8000) + off

def looks_like_text(s):
    words = [w for w in re.findall(r'[A-Za-z]+', s) if len(w) >= 3]
    if not words: return False
    def good(w):
        return (w.isupper() or w.islower() or w.istitle()) and re.search('[AEIOUYaeiouy]', w) \
            and len(set(w.lower())) > 1
    goods = [w for w in words if good(w)]
    return (len(goods) >= 2 and len(goods) >= 0.8 * len(words)
            and any(len(w) >= 4 for w in goods) and ' ' in s)

def runs(prg, tbl, minlen):
    i = 0
    while i < len(prg):
        j = i
        while j < len(prg) and prg[j] in tbl: j += 1
        k = j
        while k - i > 1 and prg[k - 1] == prg[k - 2] and prg[k - 1] in (0xFF, 0x01):
            k -= 1  # drop trailing filler
        if k - i >= minlen:
            j2, j = j, k
            s = ''.join(tbl[b] for b in prg[i:j])
            if looks_like_text(s):
                yield i, j, s
            j = j2
        i = max(j, i + 1)

def main():
    rom = open(sys.argv[1], 'rb').read()
    prg = rom[16:16 + rom[4] * 0x4000]
    out = open(sys.argv[2], 'w', encoding='utf-8') if len(sys.argv) > 2 else sys.stdout
    main_t, bub_t = load_tbl('main.tbl'), load_tbl('bubble.tbl')

    print('=== main font (main.tbl) ===', file=out)
    for i, j, s in runs(prg, main_t, 5):
        b, a = addr(i)
        print(f'\n[bank {b} ${a:04X}-${a + j - i - 1:04X}]', file=out)
        print(s.replace('<br>', '\n').replace('<end>', '\n<end>\n'), file=out)

    print('\n=== boss speech bubbles (bubble.tbl, 12x8 tiles) ===', file=out)
    for i, j, s in runs(prg, bub_t, 24):
        if '#' not in s: continue
        b, a = addr(i)
        # bubbles start at a top-left border; print 12 tiles per row
        print(f'\n[bank {b} ${a:04X}-${a + j - i - 1:04X}]', file=out)
        rows = [s[k:k + 12] for k in range(0, len(s), 12)]
        print('\n'.join(rows), file=out)

if __name__ == '__main__':
    main()

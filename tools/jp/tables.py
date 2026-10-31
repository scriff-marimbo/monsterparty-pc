#!/usr/bin/env python3
"""Map the US jump/pointer tables (data lines written with label expressions) to the JP prototype.

For each US table label, the JP table address comes from the matched instructions that read it
(`lda Table,Y` in US <-> `lda $XXXX,Y` in JP). Entries are read from the JP ROM in the same
format (.word / <lo / >hi bytes) and the same count; the target is the stored value minus the
same constant as in the US expression (e.g. +1 for RTS-trick `label-1` tables).
Writes tables.json (for the emitter) and table_seeds.txt (for codemap.py); prints checks.
"""
import json, re, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from usasm import read_bank
from m6502 import decode
US_ASM, US_ROM, JP_ROM, MAP, OUT_JSON, OUT_SEEDS = sys.argv[1:7]
us = open(US_ROM, 'rb').read()[16:16 + 0x20000]
jp = open(JP_ROM, 'rb').read()[16:16 + 0x20000]
def base(n): return 0xC000 if n == 7 else 0x8000
def jbyte(n, a): return jp[n * 0x4000 + a - base(n)]

banks = {n: read_bank(f'{US_ASM}/bank{n}.asm', n) for n in range(8)}
glob = {}
is_code = set()
for n, b in banks.items():
    for name, a in b.label_addr.items(): glob[name] = (n, a)
    for a, bs in b.ins: is_code.add((n, a))

EXPR = re.compile(r'^\s*\.(word|byte)\s+([<>]?)\(?\s*([A-Za-z_]\w*)\s*((?:[+-]\s*\d+\s*)*)\)?\s*;\s*\$([0-9A-F]{4})')
LABEL = re.compile(r'^([A-Za-z_]\w*):')
tables = {}   # (bank, label) -> list of entries
for n in range(8):
    cur = None
    for line in open(f'{US_ASM}/bank{n}.asm', encoding='utf-8'):
        m = LABEL.match(line)
        if m and not line.startswith(' '): cur = m.group(1); continue
        m = EXPR.match(line)
        if not m:
            if line.strip() and not line.startswith(';'): cur = cur if line.lstrip().startswith('.') else None
            continue
        kind = 'word' if m.group(1) == 'word' else {'<': 'lo', '>': 'hi', '': 'byte'}[m.group(2)]
        if kind == 'byte' or cur is None or m.group(3) not in glob: continue
        c = eval(m.group(4).replace(' ', '') or '0')
        tables.setdefault((n, cur), []).append({'addr': int(m.group(5), 16), 'kind': kind, 'label': m.group(3), 'c': c})

# group contiguous expression lines (even across labels) into blocks; one JP location per block
blocks = []
for (n, lab), ents in sorted(tables.items(), key=lambda kv: (kv[0][0], kv[1][0]['addr'])):
    first = ents[0]['addr']
    if blocks and blocks[-1]['bank'] == n and blocks[-1]['end'] == first and \
            {e['kind'] for e in blocks[-1]['entries']} == {e['kind'] for e in ents}:
        blk = blocks[-1]
    else:
        blk = {'bank': n, 'start': first, 'end': first, 'labels': [], 'entries': []}; blocks.append(blk)
    blk['labels'].append((lab, first))
    for e in ents:
        blk['entries'].append(e)
        blk['end'] = e['addr'] + (2 if e['kind'] == 'word' else 1)

amap = {(ub, ua): (jb, ja) for ub, ua, jb, ja in json.load(open(MAP))}
REF = re.compile(r'\b([A-Za-z_]\w*)\s*([+-]\s*\d+)?')
INS = re.compile(r'^  ([a-z]{3})\s+(.*?);\s*\$([0-9A-F]{4})')
label_block = {lab: (bi, off) for bi, b in enumerate(blocks) for lab, off in b['labels']}
votes = {}
for n in (4, 5, 7):
    for line in open(f'{US_ASM}/bank{n}.asm', encoding='utf-8'):
        m = INS.match(line)
        if not m: continue
        ua = int(m.group(3), 16)
        for r in REF.finditer(m.group(2)):
            if r.group(1) not in label_block or (n, ua) not in amap: continue
            bi, lab_addr = label_block[r.group(1)]
            k = eval((r.group(2) or '0').replace(' ', '')) + (lab_addr - blocks[bi]['start'])
            jb, ja = amap[(n, ua)]
            d = decode(jp[jb*0x4000:(jb+1)*0x4000], ja - base(jb), base(jb))
            if d and d[3] is not None and d[1] in ('abs', 'absx', 'absy'):
                tbank = jb if d[3] < 0xC000 else 7
                votes.setdefault(bi, []).append((tbank, d[3] - k))

out, seeds, bad = [], [], 0
# JP code starts known so far (for the "table runs into code" stop rule)
import os
known = set()
if os.path.exists(sys.argv[7] if len(sys.argv) > 7 else ''):
    for nb, lst in json.load(open(sys.argv[7])).items():
        for a_, s_, w_ in lst: known.add((int(nb), a_))
block_starts = set()
located = []
for bi, blk in enumerate(blocks):
    v = votes.get(bi)
    if not v:
        print(f'  no JP location for block {[l for l, _ in blk["labels"]]} (US b{blk["bank"]} ${blk["start"]:04X})'); continue
    locs = sorted(set(v))
    if len(locs) > 1: print(f'  {blk["labels"][0][0]}: conflicting JP locations {locs}')
    located.append((bi, locs[0])); block_starts.add(locs[0])
for bi, (jb, ja) in located:
    blk = blocks[bi]
    ents = []
    for e in blk['entries']:
        a = ja + (e['addr'] - blk['start'])
        size = 2 if e['kind'] == 'word' else 1
        if e['kind'] in ('lo', 'hi'):
            pass
        stop = any((jb, x) in known or ((jb, x) in block_starts and x != ja) for x in range(a, a + size))
        if stop:
            print(f'  {blk["labels"][0][0]}: JP table ends after {len(ents)} of {len(blk["entries"])} entries (runs into code/table at ${a:04X})')
            break
        if e['kind'] == 'word': val = jbyte(jb, a) | jbyte(jb, a + 1) << 8
        else: val = jbyte(jb, a)
        ents.append({'jp_addr': a, 'kind': e['kind'], 'us_label': e['label'], 'c': e['c'], 'val': val})
    out.append({'us_bank': blk['bank'], 'us_label': blk['labels'][0][0], 'us_labels': blk['labels'], 'jp_bank': jb, 'jp_addr': ja, 'entries': ents})
json.dump(out, open(OUT_JSON, 'w'), indent=1)

# targets: words directly; lo/hi pair by matching label lists
lohi = {}
for t in out:
    kinds = {e['kind'] for e in t['entries']}
    if kinds == {'word'}:
        for e in t['entries']:
            tgt = e['val'] - e['c']
            tb = t['jp_bank'] if tgt < 0xC000 else 7
            ub, ua = glob[e['us_label']]
            exp = amap.get((ub, ua))
            if exp and exp != (tb, tgt) and glob[e['us_label']] in is_code:
                bad += 1; print(f'  {t["us_label"]}: entry -> b{tb} ${tgt:04X}, alignment says {e["us_label"]} -> b{exp[0]} ${exp[1]:04X}')
            if 0x8000 <= tgt and glob[e['us_label']] in is_code:
                seeds.append(f'{tb} ${tgt:04X} table {t["us_label"]} -> {e["us_label"]}')
    else:
        lohi[(t['us_bank'], tuple(e['us_label'] for e in t['entries']))] = lohi.get((t['us_bank'], tuple(e['us_label'] for e in t['entries'])), []) + [t]
for (ub, labels), ts in lohi.items():
    lo = [t for t in ts if all(e['kind'] == 'lo' for e in t['entries'])]
    hi = [t for t in ts if all(e['kind'] == 'hi' for e in t['entries'])]
    if not lo or not hi: print('  unpaired split table', [t['us_label'] for t in ts]); continue
    lo, hi = lo[0], hi[0]
    for el, eh in zip(lo['entries'], hi['entries']):
        tgt = (el['val'] | eh['val'] << 8) - el['c']
        if lo['us_label'] in ('TaskEntryLo',): continue   # bank comes from TaskBankTable
        tb = lo['jp_bank'] if tgt < 0xC000 else 7
        g = glob[el['us_label']]; exp = amap.get(g)
        if exp and exp != (tb, tgt):
            bad += 1; print(f'  {lo["us_label"]}: entry -> b{tb} ${tgt:04X}, alignment says {el["us_label"]} -> b{exp[0]} ${exp[1]:04X}')
        if g in is_code: seeds.append(f'{tb} ${tgt:04X} table {lo["us_label"]} -> {el["us_label"]}')
open(OUT_SEEDS, 'w').write('\n'.join(seeds) + '\n')
print(f'{len(out)} tables mapped, {len(seeds)} targets, {bad} disagreements with the alignment')

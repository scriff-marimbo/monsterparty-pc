#!/usr/bin/env python3
"""Write the ca65 disassembly of the Japanese prototype, in the same format as monsterparty/.

usage: jpasm.py US_ASM_DIR JP_ROM CODEMAP MAP TABLES OUT_DIR

Names, comments above labels and end-of-line comments are carried over from the US disassembly
wherever an instruction (or a data table) was matched to its US counterpart. Unmatched labels get
auto names numbered by their JP address. A US auto name (`_label_XXXX` etc.) is renumbered to the
JP address. A US b4_/b5_ label that moved to the other bank gets that bank's prefix.
"""
import json, re, sys, os, difflib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m6502 import decode

USDIR, ROM, CODEMAP, MAP, TABLES, OUT = sys.argv[1:7]
rom = open(ROM, 'rb').read()
HDR, PRG = rom[:16], rom[16:16 + 0x20000]
CODE_BANKS = (4, 5, 7)
def base(b): return 0xC000 if b == 7 else 0x8000
def mem(b): return PRG[b * 0x4000:(b + 1) * 0x4000]
def pfx(b): return '' if b == 7 else f'b{b}_'
def target_bank(jb, v):
    if v >= 0xC000: return 7
    return jb if jb != 7 else None

# ------------------------------------------------------------------ US disassembly
INS = re.compile(r'^  ([a-z]{3})\b(.*?);\s*\$([0-9A-F]{4})\s+((?:[0-9A-F]{2} ?)+)(.*)$')
LABEL = re.compile(r'^([A-Za-z_][\w]*):')
ADDR = re.compile(r';\s*\$([0-9A-F]{4})\b')
AUTO = re.compile(r'^(?:b[0-7])?(_(?:label|func|data|jump_table|jump_engine)_)([0-9a-f]{4})(_indexed)?$')

class USBank: pass
def read_us(n):
    b = USBank(); b.ins = {}; b.labels = {}; b.notes = {}; b.datalabels = set()
    waiting, comment = [], []
    for line in open(f'{USDIR}/bank{n}.asm', encoding='utf-8'):
        m = LABEL.match(line)
        if m:
            waiting.append(m.group(1))
            if comment: b.notes[m.group(1)] = comment
            comment = []; continue
        if line.startswith(';'):
            comment.append(line.rstrip('\n')); continue
        if not line.strip(): continue
        comment = []
        a = ADDR.search(line)
        if not a: continue
        addr = int(a.group(1), 16)
        mi = INS.match(line)
        for w in waiting:
            b.labels.setdefault(addr, []).append(w)
            if not mi: b.datalabels.add(addr)
        waiting = []
        if mi:
            b.ins[addr] = (mi.group(1), mi.group(2).strip(), bytes.fromhex(mi.group(4).replace(' ', '')), mi.group(5).strip())
    return b
US = {n: read_us(n) for n in range(8)}
usrom = open(f'{USDIR}/../roms/Monster Party (USA).nes', 'rb').read()[16:16 + 0x20000] if os.path.exists(f'{USDIR}/../roms/Monster Party (USA).nes') else None

# RAM / register symbols: the US definitions, and which name the US code uses most for each address
DEF = re.compile(r'^([A-Za-z_]\w*) = \$([0-9A-Fa-f]{1,4})\b')
us_main = open(f'{USDIR}/monsterparty.asm', encoding='utf-8').read().splitlines()
defs = {}
for l in us_main:
    m = DEF.match(l)
    if m: defs.setdefault(int(m.group(2), 16), m.group(1))
OPSYM = re.compile(r'^[#(]?(?:[az]:)?([A-Za-z_]\w*(?:[+-]\d+)?)(?![\w:])')
usage = {}
for n in CODE_BANKS:
    for a, (mn, opt, bs, _) in US[n].ins.items():
        if len(bs) < 2 or bs[0] in (0x20, 0x4C) or (bs[0] & 0x1F) == 0x10 or bs[0] in (0x09, 0x29, 0x49, 0x69, 0xA9, 0xC9, 0xE9, 0xA0, 0xA2, 0xC0, 0xE0): continue
        v = bs[1] | (bs[2] << 8 if len(bs) == 3 else 0)
        if v >= 0x8000: continue
        m = OPSYM.match(opt)
        if m and not m.group(1).startswith('b'): usage.setdefault(v, {}).setdefault(m.group(1), 0); usage[v][m.group(1)] += 1
def ramname(v):
    if v in usage: return max(usage[v].items(), key=lambda kv: kv[1])[0]
    if v in defs: return defs[v]
    return None

# ------------------------------------------------------------------ JP code map and alignment
cm = json.load(open(CODEMAP))
ins = {b: {a: s for a, s, _ in cm[str(b)]} for b in CODE_BANKS}
owner = {b: {} for b in CODE_BANKS}              # address -> start of the instruction covering it
for b in CODE_BANKS:
    for a, s in ins[b].items():
        for k in range(s): owner[b][a + k] = a
jp2us = {}
for ub, ua, jb, ja in json.load(open(MAP)):
    if (ub, ua) in ((4, 0x8000), (4, 0x8003)) or (ub == 5 and ua < 0x8009): continue   # task entry jumps: rebuilt by hand
    jp2us[(jb, ja)] = (ub, ua)

# ------------------------------------------------------------------ tables (expression data)
tabs = json.load(open(TABLES))
m5 = mem(5)
tabs.append({'us_label': 'b5_BossInitLo', 'us_labels': [['b5_BossInitLo', 0x8B3C]], 'us_bank': 5, 'jp_bank': 5, 'jp_addr': 0x9B3D,
             'entries': [{'jp_addr': 0x9B3D + i, 'kind': 'lo', 'val': m5[0x1B3D + i], 'c': 0} for i in range(33)]})
tabs.append({'us_label': 'b5_BossInitHi', 'us_labels': [['b5_BossInitHi', 0x8B5D]], 'us_bank': 5, 'jp_bank': 5, 'jp_addr': 0x9B5E,
             'entries': [{'jp_addr': 0x9B5E + i, 'kind': 'hi', 'val': m5[0x1B5E + i], 'c': 0} for i in range(33)]})
byname = {t['us_label']: t for t in tabs}
expr = {}                                         # (bank, addr) -> (kind, target bank, target addr, c, size)
m7 = mem(7)
task_bank = [m7[0xC16B - 0xC000 + i] for i in range(12)]
for t in tabs:
    jb, kind0 = t['jp_bank'], t['entries'][0]['kind']
    if kind0 == 'hi': continue
    pair = None
    if kind0 == 'lo':
        pair = byname[t['us_label'][:-2] + 'Hi']
    for i, e in enumerate(t['entries']):
        if e['kind'] == 'word':
            raw = mem(jb)[e['jp_addr'] - base(jb)] | mem(jb)[e['jp_addr'] - base(jb) + 1] << 8
            assert raw == e['val'], t['us_label']
            tgt = raw - e['c']
            tb = 7 if tgt >= 0xC000 else jb
            expr[(jb, e['jp_addr'])] = ('word', tb, tgt, e['c'], 2)
        else:
            h = pair['entries'][i]
            c = -1 if t['us_label'] == 'TaskEntryLo' else e['c']
            e['c'] = h['c'] = c
            tgt = (e['val'] | h['val'] << 8) - c
            tb = 7 if tgt >= 0xC000 else (task_bank[i] if t['us_label'] == 'TaskEntryLo' else jb)
            expr[(jb, e['jp_addr'])] = ('lo', tb, tgt, e['c'], 1)
            expr[(pair['jp_bank'], h['jp_addr'])] = ('hi', tb, tgt, e['c'], 1)

# ------------------------------------------------------------------ labels
names = {}                                        # (bank, addr) -> [names]
notes = {}                                        # name -> comment lines
eol = {}                                          # (bank, addr) -> end-of-line comment
used = set()
def uniq(n):
    k, base_n = 2, n
    while n in used: n = f'{base_n}_{k}'; k += 1
    used.add(n); return n
def convert(usname, jb, ja):
    m = AUTO.match(usname)
    if m: return f'{"" if jb == 7 else f"b{jb}"}{m.group(1)}{ja:04x}{m.group(3) or ""}'
    s = re.sub(r'^b[0-7]_', '', usname)
    return pfx(jb) + s
def add(jb, ja, n, note=None):
    if any(n == x or re.sub(r'_\d+$', '', x) == n for x in names.get((jb, ja), [])): return
    n = uniq(n)
    names.setdefault((jb, ja), []).append(n)
    if note: notes[n] = note

# hand-made: task entry jump tables (the bank layout changed: see TaskEntryLo/Hi)
add(5, 0x8000, 'b5_TaskEntryJumps', ['; task entry points: slot 0 (player) $8000, slots 1-2 $8003, slots 3-5 $8006, slots 6-9 $8009 (TaskEntryLo/Hi, TaskBankTable)'])
add(4, 0x8000, 'b4_TaskEntryJumps', ['; task entry point: slot 11 (game flow)'])

# US labels on matched instructions
for (jb, ja), (ub, ua) in sorted(jp2us.items()):
    for n in US[ub].labels.get(ua, []):
        add(jb, ja, convert(n, jb, ja), US[ub].notes.get(n))
    if ua in US[ub].ins and US[ub].ins[ua][3]: eol[(jb, ja)] = US[ub].ins[ua][3]

# table entries: name an unnamed JP target after the US entry's target (same index in the same table)
def descriptive(jb, ja): return any(not AUTO.match(n) for n in names.get((jb, ja), []))
def name_by_index(jb, ja, usname, note_bank):
    if descriptive(jb, ja) or AUTO.match(usname): return
    n = convert(usname, jb, ja)
    if n in used: return
    add(jb, ja, n, US[note_bank].notes.get(usname))
for t in tabs:
    if t['us_label'].startswith('TaskEntry') or t['us_label'].startswith('b5_BossInit'): continue
    for e in t['entries']:
        if e['kind'] == 'hi' or 'us_label' not in e: continue
        kind, tb, tgt, c, sz = expr[(t['jp_bank'], e['jp_addr'])]
        ub = next((b for b in CODE_BANKS if any(e['us_label'] in v for v in US[b].labels.values())), t['us_bank'])
        name_by_index(tb, tgt, e['us_label'], ub)
if usrom:
    u5 = usrom[5 * 0x4000:6 * 0x4000]
    for (ul, uh), (jl, jh) in (((0x8B3C, 0x8B5D), (0x9B3D, 0x9B5E)), ((0x8B7E, 0x8B9F), (0x9B7F, 0x9BA0))):
        for i in range(33):
            ut = u5[ul - 0x8000 + i] | u5[uh - 0x8000 + i] << 8
            jt = m5[jl - 0x8000 + i] | m5[jh - 0x8000 + i] << 8
            for n in US[5].labels.get(ut, []): name_by_index(5, jt, n, 5)
    # vectors
    uv, jv = usrom[0x1FFFA:0x20000], PRG[0x1FFFA:0x20000]
    for k in range(0, 6, 2):
        for n in US[7].labels.get(uv[k] | uv[k + 1] << 8, []): name_by_index(7, jv[k] | jv[k + 1] << 8, n, 7)

# names from operands: a matched instruction that reads `label+k` in the US names the JP address it reads
OPLAB = re.compile(r'^(?:[az]:)?\(?([A-Za-z_]\w*)(?:\+(\d+))?')
for (jb, ja), (ub, ua) in sorted(jp2us.items()):
    usi = US[ub].ins.get(ua)
    if not usi or len(usi[2]) != 3: continue
    d = decode(mem(jb), ja - base(jb), base(jb))
    if d is None or d[3] is None or d[3] < 0x8000 or d[1] == 'rel': continue
    m = OPLAB.match(usi[1])
    if not m or m.group(1).startswith('_var') or AUTO.match(m.group(1)): continue
    v = d[3] - int(m.group(2) or 0)
    tb = target_bank(jb, v)
    if tb is None: continue
    nm = m.group(1)
    if not any(nm in x for b2 in range(8) for x in US[b2].labels.values()): continue
    name_by_index(tb, v, nm, ub)

# remaining US labels: if the US label sits between two matched instructions that are the same
# distance apart in both ROMs, the bytes between are taken to correspond one to one
pairs = {}
for (jb, ja), (ub, ua) in jp2us.items(): pairs.setdefault((ub, jb), []).append((ua, ja))
for k in pairs: pairs[k].sort()
import bisect
gapfilled = []
for ub in CODE_BANKS:
    for ua, ns in sorted(US[ub].labels.items()):
        ns = [n for n in ns if not AUTO.match(n)]
        if not ns or any(convert(n, 5, 0) in used or convert(n, 4, 0) in used or convert(n, 7, 0) in used for n in ns): continue
        best = None
        for (ub2, jb), lst in pairs.items():
            if ub2 != ub: continue
            i = bisect.bisect_left(lst, (ua, -1))
            if 0 < i < len(lst):
                (u1, j1), (u2, j2) = lst[i - 1], lst[i]
                if u2 - u1 == j2 - j1 and u2 - u1 < 0x100: best = (jb, j1 + ua - u1)
        if not best: continue
        jb, ja = best
        if jb in CODE_BANKS and ja in owner[jb] and owner[jb][ja] != ja: continue
        if (ua in US[ub].ins) != (jb in CODE_BANKS and ja in ins[jb]): continue
        for n in ns:
            add(jb, ja, convert(n, jb, ja), US[ub].notes.get(n)); gapfilled.append(n)
print('gap-filled names:', ' '.join(gapfilled))

# US labels on tables
for t in tabs:
    for n, ua in t['us_labels']:
        off = ua - t['us_labels'][0][1]
        jb, ja = t['jp_bank'], t['jp_addr'] + off
        if off and ja >= t['entries'][-1]['jp_addr'] + (2 if t['entries'][-1]['kind'] == 'word' else 1): continue
        add(jb, ja, convert(n, jb, ja), US[t['us_bank']].notes.get(n))

# bank 2 (text and speech bubbles) keeps the US layout: the JP text was written in place
for ua, ns in US[2].labels.items():
    for n in ns:
        note = [l for l in US[2].notes.get(n, []) if not l.startswith('; "') and 'main.tbl' not in l]
        add(2, ua, n, note + (['; text (JP font)'] if n.startswith('b2_Text_') else []))

# US data labels in data areas: match non-code bytes with difflib
def data_runs(b, m):
    """the bank's bytes with code bytes blanked out, as a list of (addr, byte) for non-code bytes"""
    cov = owner.get(b, {})
    return [(base(b) + i, m[i]) for i in range(len(m)) if base(b) + i not in cov]
if usrom:
    US_CODE = {n: set() for n in CODE_BANKS}
    for n in CODE_BANKS:
        for a, v in US[n].ins.items():
            for k in range(len(v[2])): US_CODE[n].add(a + k)
    src = {0: [0], 1: [1], 2: [2], 3: [3], 4: [4], 5: [4, 5], 6: [6], 7: [7]}
    for jb in range(8):
        jd = data_runs(jb, mem(jb))
        jbytes = bytes(v for _, v in jd)
        for ub in src[jb]:
            um = usrom[ub * 0x4000:(ub + 1) * 0x4000]
            ud = [(base(ub) + i, um[i]) for i in range(len(um)) if base(ub) + i not in US_CODE.get(ub, set())]
            want = {a for a in US[ub].datalabels}
            if not want: continue
            sm = difflib.SequenceMatcher(None, bytes(v for _, v in ud), jbytes, autojunk=False)
            for i, j, n in sm.get_matching_blocks():
                if n < 4: continue
                for k in range(n):
                    ua = ud[i + k][0]
                    if ua in want:
                        ja = jd[j + k][0]
                        for nm in US[ub].labels[ua]:
                            if convert(nm, jb, ja) not in used: add(jb, ja, convert(nm, jb, ja), US[ub].notes.get(nm))

# references from code
refs = {}                                        # (bank, addr) -> 'func' / 'label' / 'data'
for jb in CODE_BANKS:
    m, bs = mem(jb), base(jb)
    for a in sorted(ins[jb]):
        name, mode, size, v = decode(m, a - bs, bs)
        if mode == 'rel': refs.setdefault((jb, v), 'label')
        elif name in ('jsr', 'jmp') and mode == 'abs':
            tb = target_bank(jb, v)
            if tb is not None: refs[(tb, v)] = 'func' if name == 'jsr' else refs.get((tb, v), 'label')
        elif mode in ('abs', 'absx', 'absy') and v >= 0x8000:
            tb = target_bank(jb, v)
            if tb is not None and owner[tb].get(v, v) == v: refs.setdefault((tb, v), 'data')
for (jb, ja), (kind, tb, tgt, c, sz) in expr.items():
    refs.setdefault((tb, tgt), 'label')
    refs.setdefault((jb, ja), 'data')
for t in tabs: refs.setdefault((t['jp_bank'], t['jp_addr']), 'data')
# '+k' for a read just past a labelled spot in the same data block (hi byte of a word table etc.)
plus = {}
for (tb, v), kind in sorted(refs.items()):
    if (tb, v) in names: continue
    if kind == 'data':
        for k in (1, 2):
            if (tb, v - k) in names or ((tb, v - k) in refs and (tb, v - k) not in plus and refs[(tb, v - k)] == 'data'):
                if all(v - j not in owner.get(tb, {}) for j in range(k + 1)): plus[(tb, v)] = k; break
        if (tb, v) in plus: continue
    if kind == 'data' and v in owner.get(tb, {}): kind = 'label'
    add(tb, v, f'{pfx(tb)}_{kind}_{v:04x}' if tb == 7 else f'{pfx(tb)}{kind}_{v:04x}')
def label_of(tb, v):
    if (tb, v) in names: return names[(tb, v)][0]
    if (tb, v) in plus:
        k = plus[(tb, v)]; return f'{label_of(tb, v - k)}+{k}'
    return None

# ------------------------------------------------------------------ emit
IMM = {o for o in range(256)}
def fmt_ram(mode, v, zp):
    s = ramname(v)
    if s is None: s = f'${v:02X}' if zp else f'${v:04X}'
    p = 'z:' if zp else ('a:' if v < 0x2000 else '')
    return {'zp': f'{p}{s}', 'zpx': f'{p}{s},X', 'zpy': f'{p}{s},Y', 'abs': f'{p}{s}', 'absx': f'{p}{s},X', 'absy': f'{p}{s},Y',
            'indx': f'({s},X)', 'indy': f'({s}),Y', 'ind': f'({s})'}[mode]
def operand(jb, a, name, mode, size, v):
    if mode == 'imp': return ''
    if mode == 'acc': return 'a'
    us = jp2us.get((jb, a))
    usi = US[us[0]].ins.get(us[1]) if us else None
    same = usi and usi[2] == bytes(mem(jb)[a - base(jb):a - base(jb) + size])
    if mode == 'imm':
        return usi[1] if same and usi[1].startswith('#') and '<' not in usi[1] and '>' not in usi[1] else f'#${v:02X}'
    if mode == 'rel': return label_of(jb, v)
    if v < 0x8000:
        if same and not re.search(r'\bb[0-7]_|\$[89A-F][0-9A-F]{3}', usi[1]): return usi[1]
        return fmt_ram(mode, v, size == 2)
    tb = target_bank(jb, v)
    lab = None
    if tb is not None:
        lab = label_of(tb, v)
        if lab is None and v in owner.get(tb, {}) and (tb, owner[tb][v]) in names:
            lab = f'{label_of(tb, owner[tb][v])}+{v - owner[tb][v]}' if mode not in ('abs',) or name not in ('jsr', 'jmp') else None
    s = lab or f'${v:04X}'
    return {'abs': f'a:{s}' if name not in ('jsr', 'jmp') else s, 'absx': f'a:{s},X', 'absy': f'a:{s},Y', 'ind': f'({s})'}[mode]

def ins_line(text, a, bs, comment=''):
    t = f'  {text:<30} ; ${a:04X}  {" ".join(f"{x:02X}" for x in bs)}'
    return t + (f'  {comment}' if comment else '')
def data_line(vals, a):
    return f'.byte {", ".join(f"${x:02x}" for x in vals):<24} ; ${a:04X}'

def expr_text(kind, tb, tgt, c):
    lab = label_of(tb, tgt) or f'${tgt:04X}'
    if tb in (4, 5) and 0x8000 <= tgt < 0x800C and c == -1: lab = f'{pfx(tb)}TaskEntryJumps' + (f'+{tgt - 0x8000}' if tgt > 0x8000 else '')
    e = lab + (f'{-c:+d}'.replace('+-', '-') if c else '')
    if c == -1: e = f'{lab}-1'
    elif c: e = f'{lab}{-c:+d}'
    return {'word': f'.word {e}', 'lo': f'.byte <({e})' if c else f'.byte <{e}', 'hi': f'.byte >({e})' if c else f'.byte >{e}'}[kind]

os.makedirs(OUT, exist_ok=True)
for b in range(8):
    m, bs = mem(b), base(b)
    out = [f'.segment "PRG_BANK_{b}"', '']
    run, run_a = [], None
    def flush():
        global run, run_a
        if run:
            for i in range(0, len(run), 16): out.append(data_line(run[i:i + 16], run_a + i))
        run, run_a = [], None
    a = bs
    while a < bs + 0x4000:
        labs = names.get((b, a), [])
        if labs:
            flush()
            out.append('')
            for n in labs:
                for c in notes.get(n, []): out.append(c)
                out.append(f'{n}:')
        if b in CODE_BANKS and a in ins[b]:
            flush()
            name, mode, size, v = decode(m, a - bs, bs)
            op = operand(b, a, name, mode, size, v)
            out.append(ins_line(f'{name} {op}'.rstrip(), a, m[a - bs:a - bs + size], eol.get((b, a), '')))
            if name in ('rts', 'rti', 'jmp') : out.append('')
            a += size; continue
        if (b, a) in expr:
            flush()
            kind, tb, tgt, c, size = expr[(b, a)]
            out.append(f'{expr_text(kind, tb, tgt, c):<32} ; ${a:04X}')
            a += size; continue
        if run_a is None: run_a = a
        run.append(m[a - bs]); a += 1
        if len(run) == 16: flush()
    flush()
    txt = re.sub(r'\n{3,}', '\n\n', '\n'.join(out) + '\n')
    open(f'{OUT}/bank{b}.asm', 'w').write(txt)

# main file: the US one with the JP header (identical) and the same symbol block
import zlib
main = []
for l in us_main:
    if l.startswith('; PRG CRC32'): l = f'; PRG CRC32 checksum: {zlib.crc32(PRG):08x}'
    elif l.startswith('; CHR CRC32'): l = f'; CHR CRC32 checksum: {zlib.crc32(rom[16 + 0x20000:]):08x}'
    elif l.startswith('; Overall CRC32'): l = f'; Overall CRC32 checksum: {zlib.crc32(rom[16:]):08x}'
    elif l.startswith('; NES ROM Disassembly'): l = '; NES ROM Disassembly: Parody World - Monster Party (Japan) (Proto)'
    main.append(l)
open(f'{OUT}/monsterparty.asm', 'w').write('\n'.join(main) + '\n')
print('labels:', sum(len(v) for v in names.values()), 'carried from US:', sum(1 for v in names.values() for n in v if not AUTO.match(n)))

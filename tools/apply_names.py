#!/usr/bin/env python3
"""Apply RAM and label names from a names file to the disassembly (idempotent).

usage: apply_names.py MONSTERPARTY_DIR NAMES_FILE
"""
import re, sys, os

D, NAMES = sys.argv[1], sys.argv[2]
FILES = ['monsterparty.asm'] + [f'bank{n}.asm' for n in range(8)]
src = {f: open(os.path.join(D, f), encoding='utf-8').read().split('\n') for f in FILES}
main = src['monsterparty.asm']

# ---- parse names file (comment-only lines continue the previous entry's comment)
ram, labels, new_labels = [], [], []
for raw in open(NAMES, encoding='utf-8'):
    line = raw.rstrip('\n')
    if not line.strip() or line.lstrip().startswith('#'): continue
    body, _, comment = line.partition(';')
    parts = body.split()
    comment = comment.strip()
    if not parts:
        if ram: ram[-1]['comment'] += ' ' + comment
        continue
    if parts[0] == 'label':
        labels.append((parts[1], parts[2], comment))
    elif parts[0] == 'at':   # at bank4 $950C Name ; comment  -> new label at an address
        new_labels.append((parts[1] + '.asm', int(parts[2].lstrip('$'), 16), parts[3], comment))
    else:
        ram.append({'addr': int(parts[0].lstrip('$'), 16), 'name': parts[1],
                    'size': int(parts[2]) if len(parts) > 2 else 1, 'comment': comment})

# ---- current definitions: symbol -> address
DEF = re.compile(r'^(\w+) = \$([0-9A-Fa-f]{1,4})\b')
defs = {}
for l in main:
    m = DEF.match(l)
    if m: defs[m.group(1)] = int(m.group(2), 16)

# ---- address -> expression
expr = {}
for e in ram:
    for k in range(e['size']):
        expr[e['addr'] + k] = e['name'] if k == 0 else f"{e['name']}+{k}"

def sub_all(pattern, repl):
    rx = re.compile(pattern)
    for f in FILES:
        src[f] = [rx.sub(repl, l) for l in src[f]]

# rename existing symbols that sit at a named address
for sym, addr in defs.items():
    new = expr.get(addr)
    if new and new != sym:
        sub_all(r'(?<![\w.])' + re.escape(sym) + r'(?!\w)', new)

# raw numeric operands, e.g. z:$5A or a:$005A
for addr, new in expr.items():
    if addr < 0x100:
        sub_all(r'\bz:\$' + f'{addr:02X}' + r'(?![0-9A-Fa-f\w])', 'z:' + new)
    sub_all(r'\ba:\$' + f'{addr:04X}' + r'(?![0-9A-Fa-f\w])', 'a:' + new)

# raw RAM operands at addresses that already have a symbol (e.g. code converted from bytes later)
by_addr = {}
for sym, addr in defs.items():
    if addr < 0x0800 and re.fullmatch(r'\w+', sym) and addr not in expr:
        by_addr.setdefault(addr, sym)
for addr, sym in by_addr.items():
    if addr < 0x100:
        sub_all(r'\bz:\$' + f'{addr:02X}' + r'(?![0-9A-Fa-f\w])', 'z:' + sym)
    sub_all(r'\ba:\$' + f'{addr:04X}' + r'(?![0-9A-Fa-f\w])', 'a:' + sym)

# rebuild definition lines: drop defs that became expressions, write named ones with comments
main = src['monsterparty.asm']
names = {e['name'] for e in ram if re.fullmatch(r'\w+', e['name'])}
out = []
for l in main:
    m = re.match(r'^([\w+\-]+) = \$([0-9A-Fa-f]{1,4})\b', l)
    if m and (not re.fullmatch(r'\w+', m.group(1)) or m.group(1) in names):
        continue  # removed (expression) or re-emitted below
    out.append(l)
k = next(i for i, l in enumerate(out) if 'hardware registers' in l) + 1
block = []
for e in ram:
    if e['name'] not in names: continue
    d = f"{e['name']} = ${e['addr']:04X}"
    if e['size'] > 1: d += f"  ; {e['size']} bytes"
    if e['comment']: d = f'{d:<28} ; {e["comment"]}' if e['size'] == 1 else f'{d}. {e["comment"]}'
    block.append(d)
out[k:k] = block
src['monsterparty.asm'] = out

# labels
for old, new, comment in labels:
    sub_all(r'(?<![\w.])' + re.escape(old) + r'(?!\w)', new)
    if not re.fullmatch(r'\w+', new):
        # alias such as Table+1: the old label's own definition line is no longer needed
        for f in FILES:
            src[f] = [l for l in src[f] if l != f'{new}:']
        continue
    for f in FILES:
        lines = src[f]
        if f'{new}:' in lines and comment:
            i = lines.index(f'{new}:')
            # add the comment only if the label has none yet (keeps your own comments, avoids repeats)
            if not (i and lines[i - 1].startswith(';')):
                lines[i:i] = ['; ' + comment]

# drop exact duplicate comment lines directly above each other (left by older versions of this script)
for f in FILES:
    src[f] = [l for k, l in enumerate(src[f])
              if not (k and l.startswith(';') and l == src[f][k - 1])]

# new labels at addresses that had none (instruction line, or a split .byte row)
BYTEROW = re.compile(r'^\.byte (.*?)\s*; \$([0-9A-F]{4})\b')
for f, addr, name, comment in new_labels:
    lines = src[f]
    if f'{name}:' in lines: continue
    idx = None
    for i, l in enumerate(lines):
        m = BYTEROW.match(l)
        if m:
            start = int(m.group(2), 16)
            vals = [v.strip() for v in m.group(1).split(',') if v.strip()]
            if start == addr: idx = i; break
            if start < addr < start + len(vals):
                k = addr - start
                lines[i:i + 1] = [f".byte {', '.join(vals[:k])}".ljust(32) + f' ; ${start:04X}',
                                  f".byte {', '.join(vals[k:])}".ljust(32) + f' ; ${addr:04X}']
                idx = i + 1; break
        elif re.match(r'^  \S', l) and re.search(r';\s*\$%04X\b' % addr, l):
            idx = i; break
    if idx is None: sys.exit(f'{f}: nothing at ${addr:04X} for {name}')
    lines[idx:idx] = [''] + (['; ' + comment] if comment else []) + [f'{name}:']

# re-align comment column on instruction lines
ALIGN = re.compile(r'^(  \S.*?)\s+(; \$[0-9A-F]{4}\b.*)$')
for f in FILES:
    src[f] = [(lambda m: f'{m.group(1):<32} {m.group(2)}' if m else l)(ALIGN.match(l)) for l in src[f]]

for f in FILES:
    open(os.path.join(D, f), 'w', encoding='utf-8').write('\n'.join(src[f]))
print(f'applied {len(ram)} RAM names, {len(labels)} label names')

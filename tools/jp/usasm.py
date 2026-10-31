"""Read the (current, named) US disassembly: instructions, labels and comments per bank."""
import re
INS = re.compile(r'^  ([a-z]{3})\b(.*?);\s*\$([0-9A-F]{4})\s+((?:[0-9A-F]{2} ?)+)')
DATA = re.compile(r'^\s*\.(byte|word|addr)\b.*?;\s*\$([0-9A-F]{4})\b')
LABEL = re.compile(r'^([A-Za-z_@][\w]*):')
ADDR = re.compile(r';\s*\$([0-9A-F]{4})\b')

class Bank:
    def __init__(self, n):
        self.n = n
        self.ins = []          # (addr, bytes)
        self.ins_at = {}       # addr -> index
        self.labels = {}       # addr -> [names]
        self.label_addr = {}   # name -> addr
        self.notes = {}        # name -> comment lines above the label
        self.data_lines = []   # (addr, text)

def read_bank(path, n):
    b = Bank(n)
    waiting, comment, pending_notes = [], [], {}
    for line in open(path, encoding='utf-8'):
        m = LABEL.match(line)
        if m and not line.startswith(' '):
            waiting.append(m.group(1))
            if comment: pending_notes[m.group(1)] = comment
            comment = []
            continue
        if line.startswith(';'):
            comment.append(line.rstrip('\n')); continue
        if not line.strip():
            continue
        comment = []
        a = ADDR.search(line)
        if a and (line.startswith('  ') or line.lstrip().startswith('.')):
            addr = int(a.group(1), 16)
            for w in waiting:
                b.labels.setdefault(addr, []).append(w)
                b.label_addr[w] = addr
                if w in pending_notes: b.notes[w] = pending_notes.pop(w)
            waiting = []
            mi = INS.match(line)
            if mi:
                bs = bytes(int(x, 16) for x in mi.group(4).split())
                b.ins_at[addr] = len(b.ins)
                b.ins.append((addr, bs))
            else:
                b.data_lines.append((addr, line.rstrip('\n')))
    return b

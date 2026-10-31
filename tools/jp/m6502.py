"""6502 opcode table (official opcodes) shared by the JP proto tools."""
MODES = {}
def _op(name, *pairs):
    for o, m in pairs: MODES[o] = (name, m)
for name, base in [('ora', 0x00), ('and', 0x20), ('eor', 0x40), ('adc', 0x60), ('sta', 0x80), ('lda', 0xA0), ('cmp', 0xC0), ('sbc', 0xE0)]:
    for o, m in [(0x01, 'indx'), (0x05, 'zp'), (0x09, 'imm'), (0x0D, 'abs'), (0x11, 'indy'), (0x15, 'zpx'), (0x19, 'absy'), (0x1D, 'absx')]:
        if name == 'sta' and m == 'imm': continue
        MODES[base + o] = (name, m)
for name, base in [('asl', 0x00), ('rol', 0x20), ('lsr', 0x40), ('ror', 0x60)]:
    _op(name, (base + 0x0A, 'acc'), (base + 0x06, 'zp'), (base + 0x16, 'zpx'), (base + 0x0E, 'abs'), (base + 0x1E, 'absx'))
_op('bit', (0x24, 'zp'), (0x2C, 'abs'))
_op('cpx', (0xE0, 'imm'), (0xE4, 'zp'), (0xEC, 'abs')); _op('cpy', (0xC0, 'imm'), (0xC4, 'zp'), (0xCC, 'abs'))
_op('dec', (0xC6, 'zp'), (0xD6, 'zpx'), (0xCE, 'abs'), (0xDE, 'absx')); _op('inc', (0xE6, 'zp'), (0xF6, 'zpx'), (0xEE, 'abs'), (0xFE, 'absx'))
_op('ldx', (0xA2, 'imm'), (0xA6, 'zp'), (0xB6, 'zpy'), (0xAE, 'abs'), (0xBE, 'absy'))
_op('ldy', (0xA0, 'imm'), (0xA4, 'zp'), (0xB4, 'zpx'), (0xAC, 'abs'), (0xBC, 'absx'))
_op('stx', (0x86, 'zp'), (0x96, 'zpy'), (0x8E, 'abs')); _op('sty', (0x84, 'zp'), (0x94, 'zpx'), (0x8C, 'abs'))
_op('jmp', (0x4C, 'abs'), (0x6C, 'ind')); _op('jsr', (0x20, 'abs'))
for name, o in [('bpl', 0x10), ('bmi', 0x30), ('bvc', 0x50), ('bvs', 0x70), ('bcc', 0x90), ('bcs', 0xB0), ('bne', 0xD0), ('beq', 0xF0)]:
    MODES[o] = (name, 'rel')
for name, o in [('brk', 0x00), ('php', 0x08), ('clc', 0x18), ('plp', 0x28), ('sec', 0x38), ('rti', 0x40), ('pha', 0x48), ('cli', 0x58),
                ('rts', 0x60), ('pla', 0x68), ('sei', 0x78), ('dey', 0x88), ('txa', 0x8A), ('tya', 0x98), ('txs', 0x9A), ('tay', 0xA8),
                ('tax', 0xAA), ('clv', 0xB8), ('tsx', 0xBA), ('iny', 0xC8), ('dex', 0xCA), ('cld', 0xD8), ('inx', 0xE8), ('nop', 0xEA), ('sed', 0xF8)]:
    MODES[o] = (name, 'imp')
SIZE = {'imp': 1, 'acc': 1, 'imm': 2, 'zp': 2, 'zpx': 2, 'zpy': 2, 'indx': 2, 'indy': 2, 'rel': 2,
        'abs': 3, 'absx': 3, 'absy': 3, 'ind': 3}

def decode(mem, off, base):
    """(name, mode, size, operand) for the instruction at mem[off] (cpu address base+off), or None."""
    if off >= len(mem): return None
    o = mem[off]
    if o not in MODES: return None
    name, mode = MODES[o]
    n = SIZE[mode]
    if off + n > len(mem): return None
    if n == 1: v = None
    elif n == 2: v = mem[off + 1]
    else: v = mem[off + 1] | mem[off + 2] << 8
    if mode == 'rel':
        d = v - 256 if v >= 128 else v
        v = base + off + 2 + d          # branch target
    return name, mode, n, v

"""Readable R3000 listing of a level overlay range (or the resident executable).

Usage:
  python tools/mips_listing.py <level slot> <start hex> <end hex> [--out file]
  python tools/mips_listing.py main <start hex> <end hex>
Overlay ranges come from data/analysis/moby-dispatch/dispatch.json. Branch
targets are labelled, `jal` targets show known resident names from
data/analysis/moby-dispatch/resident_names.json when present, and GTE
(COP2) instructions are decoded by name.
"""
from pathlib import Path
import json, struct, sys

root = Path(__file__).resolve().parents[1]
REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 's8', 'ra']
GTE = {0x01: 'RTPS', 0x06: 'NCLIP', 0x0c: 'OP', 0x10: 'DPCS', 0x11: 'INTPL', 0x12: 'MVMVA', 0x13: 'NCDS', 0x14: 'CDP',
       0x16: 'NCDT', 0x1b: 'NCCS', 0x1c: 'CC', 0x1e: 'NCS', 0x20: 'NCT', 0x28: 'SQR', 0x29: 'DCPL', 0x2a: 'DPCT',
       0x2d: 'AVSZ3', 0x2e: 'AVSZ4', 0x30: 'RTPT', 0x3d: 'GPF', 0x3e: 'GPL', 0x3f: 'NCCT'}


def load(where: str):
    if where == 'main':
        data = (root / 'data/exe/main.bin').read_bytes()
        return 0x80010000, data
    if where.startswith('file:'):
        # file:PATH@BASE - any RAM dump (e.g. a decoded YOTD overlay) loaded at BASE.
        path, base = where[5:].rsplit('@', 1)
        return int(base, 16), Path(path).read_bytes()
    slot = int(where)
    return 0x8006d264, (root / f'data/overlays/overlay_{slot - 1:03d}.bin').read_bytes()


def names():
    path = root / 'data/analysis/moby-dispatch/resident_names.json'
    return {int(k, 16): v for k, v in json.loads(path.read_text()).items()} if path.exists() else {}


def decode(address: int, w: int, known):
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sa = (w >> 6) & 31; fn = w & 63
    imm = w & 0xffff; simm = imm - 0x10000 if imm & 0x8000 else imm
    target = address + 4 + simm * 4
    r = lambda i: REG[i]
    if w == 0: return 'nop', None
    if op == 0:
        special = {0x00: 'sll', 0x02: 'srl', 0x03: 'sra'}
        if fn in special: return f'{special[fn]} {r(rd)},{r(rt)},{sa}', None
        if fn in (0x04, 0x06, 0x07): return f"{ {4: 'sllv', 6: 'srlv', 7: 'srav'}[fn]} {r(rd)},{r(rt)},{r(rs)}", None
        if fn == 0x08: return f'jr {r(rs)}', None
        if fn == 0x09: return f'jalr {r(rd)},{r(rs)}', None
        if fn in (0x10, 0x12): return f"{'mfhi' if fn == 0x10 else 'mflo'} {r(rd)}", None
        if fn in (0x11, 0x13): return f"{'mthi' if fn == 0x11 else 'mtlo'} {r(rs)}", None
        if fn in (0x18, 0x19, 0x1a, 0x1b): return f"{ {0x18: 'mult', 0x19: 'multu', 0x1a: 'div', 0x1b: 'divu'}[fn]} {r(rs)},{r(rt)}", None
        alu = {0x20: 'add', 0x21: 'addu', 0x22: 'sub', 0x23: 'subu', 0x24: 'and', 0x25: 'or', 0x26: 'xor', 0x27: 'nor', 0x2a: 'slt', 0x2b: 'sltu'}
        if fn in alu:
            if fn == 0x25 and rt == 0: return f'move {r(rd)},{r(rs)}', None
            return f'{alu[fn]} {r(rd)},{r(rs)},{r(rt)}', None
        if fn == 0x0c: return 'syscall', None
        if fn == 0x0d: return 'break', None
        return f'.word 0x{w:08x}', None
    if op == 1:
        name = {0: 'bltz', 1: 'bgez', 16: 'bltzal', 17: 'bgezal'}.get(rt, 'regimm?')
        return f'{name} {r(rs)},L_{target:08x}', target
    if op in (2, 3):
        dest = (address & 0xf0000000) | ((w & 0x3ffffff) << 2)
        label = known.get(dest, '')
        return f"{'j' if op == 2 else 'jal'} 0x{dest:08x}{'  ; ' + label if label else ''}", dest if op == 2 else None
    if op in (4, 5):
        if op == 4 and rs == rt == 0: return f'b L_{target:08x}', target
        return f"{'beq' if op == 4 else 'bne'} {r(rs)},{r(rt)},L_{target:08x}", target
    if op in (6, 7): return f"{'blez' if op == 6 else 'bgtz'} {r(rs)},L_{target:08x}", target
    if op == 8: return f'addi {r(rt)},{r(rs)},{simm}', None
    if op == 9:
        if rs == 0: return f'li {r(rt)},{simm} (0x{imm:x})', None
        return f'addiu {r(rt)},{r(rs)},{simm}', None
    if op == 10: return f'slti {r(rt)},{r(rs)},{simm}', None
    if op == 11: return f'sltiu {r(rt)},{r(rs)},{simm}', None
    if op == 12: return f'andi {r(rt)},{r(rs)},0x{imm:x}', None
    if op == 13: return f'ori {r(rt)},{r(rs)},0x{imm:x}', None
    if op == 14: return f'xori {r(rt)},{r(rs)},0x{imm:x}', None
    if op == 15: return f'lui {r(rt)},0x{imm:x}', None
    if op == 0x12:
        if rs & 0x10: return f"GTE {GTE.get(w & 63, hex(w & 63))} (0x{w & 0x1ffffff:07x})", None
        return f"{ {0: 'mfc2', 2: 'cfc2', 4: 'mtc2', 6: 'ctc2'}.get(rs, 'cop2?')} {r(rt)},${rd}", None
    if op == 0x10: return f"{ {0: 'mfc0', 4: 'mtc0'}.get(rs, 'cop0')} {r(rt)},${rd}", None
    mem = {0x20: 'lb', 0x21: 'lh', 0x22: 'lwl', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x26: 'lwr', 0x28: 'sb', 0x29: 'sh',
           0x2a: 'swl', 0x2b: 'sw', 0x2e: 'swr', 0x32: 'lwc2', 0x3a: 'swc2'}
    if op in mem:
        reg = f'${rt}' if op in (0x32, 0x3a) else r(rt)
        return f'{mem[op]} {reg},{simm}({r(rs)})' if simm >= 0 else f'{mem[op]} {reg},-0x{-simm:x}({r(rs)})', None
    return f'.word 0x{w:08x}', None


def listing(where: str, start: int, end: int):
    base, data = load(where)
    known = names()
    rows = []; targets = set()
    for a in range(start, end, 4):
        w = struct.unpack_from('<I', data, a - base)[0]
        text, t = decode(a, w, known)
        rows.append((a, w, text))
        if t: targets.add(t)
    out = []
    for a, w, text in rows:
        if a in targets: out.append(f'L_{a:08x}:')
        out.append(f'  {a:08x}  {w:08x}  {text}')
    return '\n'.join(out)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    text = listing(args[0], int(args[1], 16), int(args[2], 16))
    if '--out' in sys.argv:
        Path(sys.argv[sys.argv.index('--out') + 1]).write_text(text + '\n')
    else:
        print(text)

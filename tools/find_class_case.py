"""Find a moby class's case in a level overlay's dispatcher.

The dispatchers compare the class (s0, sometimes another register) with
`li v0,CLASS` / `addiu v0,zero,CLASS` (or `ori`) and branch with
`beq REG,v0,TARGET`. This lists every such compare-and-branch for the
classes asked for, plus jump-table cases are not covered (Skelos Badlands).

Usage: python tools/find_class_case.py <slot> CLASS[,CLASS...]
Prints: class, compare address, branch address, target. The case body runs
from the target to the next case target in address order (printed as
`next`), which is the range to give ExportCaseRanges.java / mips_listing.py.
"""
from pathlib import Path
import struct, sys

root = Path(__file__).resolve().parents[1]


def main():
    slot = int(sys.argv[1]); classes = [int(c, 16) for c in sys.argv[2].split(',')]
    data = (root / f'data/overlays/overlay_{slot - 1:03d}.bin').read_bytes()
    base = 0x8006d264
    words = struct.unpack_from(f'<{len(data) // 4}I', data)
    hits = []; targets = set()
    for i, w in enumerate(words):
        op = w >> 26
        if op not in (9, 13): continue   # addiu / ori
        rs = (w >> 21) & 31; rt = (w >> 16) & 31; imm = w & 0xffff
        if rs != 0: continue
        for j in range(i + 1, min(i + 4, len(words))):
            b = words[j]
            if b >> 26 == 4 and ((b >> 16) & 31 == rt or (b >> 21) & 31 == rt):   # beq x,rt
                off = b & 0xffff; off = off - 0x10000 if off & 0x8000 else off
                target = base + (j + 1 + off) * 4
                targets.add(target)
                if imm in classes:
                    hits.append((imm, base + i * 4, base + j * 4, target))
                break
    # Case bodies end where the next class case starts: keep only the targets
    # of the dispatcher's own compare tree (the compares clustered at its start).
    import json
    disp = json.loads((root / 'data/analysis/moby-dispatch/dispatch.json').read_text()).get(str(slot), {}).get('dispatcher')
    tree = set(targets)
    if disp:
        d = int(disp, 16); tree = set()
        for i, w in enumerate(words):
            a = base + i * 4
            if not (d <= a < d + 0x1000): continue
            op = w >> 26
            if op in (9, 13) and (w >> 21) & 31 == 0:
                rt = (w >> 16) & 31
                for j in range(i + 1, min(i + 4, len(words))):
                    b = words[j]
                    if b >> 26 == 4 and ((b >> 16) & 31 == rt or (b >> 21) & 31 == rt):
                        off = b & 0xffff; off = off - 0x10000 if off & 0x8000 else off
                        tree.add(base + (j + 1 + off) * 4); break
    order = sorted(tree | {base + len(data)})
    for c, at, br, target in hits:
        nxt = next((t for t in order if t > target), None)
        print(f'class {c:x} li {at:08x} beq {br:08x} target {target:08x} next {nxt:08x}' if nxt else f'class {c:x} li {at:08x} beq {br:08x} target {target:08x}')


if __name__ == '__main__':
    main()

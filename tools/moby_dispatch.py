"""Map each level overlay's moby class dispatch to its case code ranges.

Every Spyro 2 gameplay overlay (WAD slot = level slot - 1, loaded at
0x8006d264) updates its mobys in one large function that loads the class
(`lh v1,0x36(sN)`) and walks a binary-search tree of `li v0,CLASS` /
`beq v1,v0,CASE` comparisons. Ghidra times out decompiling the whole
function, so this tool recovers the class -> case start map directly from the
instruction words and derives exclusive case ends (the next case start), which
tools/ghidra/ExportMobyCases.java then decompiles individually.

Usage:
  python tools/moby_dispatch.py [--slots 21 25 ...] [--classes 111,118]
Writes data/analysis/moby-dispatch/dispatch.json and extracts any missing
data/overlays/overlay_NNN.bin.
"""
from pathlib import Path
import argparse, json, struct, subprocess, sys

root = Path(__file__).resolve().parents[1]
BASE = 0x8006d264
LEVELS = {15: 'Summer Forest', 17: 'Glimmer', 19: 'Idol Springs', 21: 'Colossus', 23: 'Hurricos', 25: 'Aquaria Towers',
          27: 'Sunny Beach', 29: 'Ocean Speedway', 31: "Crush's Dungeon", 33: 'Autumn Plains', 35: 'Skelos Badlands',
          37: 'Crystal Glacier', 39: 'Breeze Harbor', 41: 'Zephyr', 43: 'Metro Speedway', 45: 'Scorch', 47: 'Shady Oasis',
          49: 'Magma Cone', 51: 'Fracture Hills', 53: 'Icy Speedway', 55: "Gulp's Overlook", 57: 'Winter Tundra',
          59: 'Mystic Marsh', 61: 'Cloud Temples', 63: 'Canyon Speedway', 65: 'Robotica Farms', 67: 'Metropolis',
          69: 'Dragon Shores', 71: "Ripto's Arena"}


def overlay(slot: int) -> bytes:
    path = root / f'data/overlays/overlay_{slot - 1:03d}.bin'
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([sys.executable, str(root / 'tools/spyrodisc.py'), 'wad-extract', str(root / 'data/disc/WAD.WAD'), str(slot - 1), str(path)], check=True)
    return path.read_bytes()


def words(code: bytes):
    for i in range(0, len(code) - 3, 4):
        yield BASE + i, struct.unpack_from('<I', code, i)[0]


def decode(w: int):
    op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xffff
    simm = imm - 0x10000 if imm & 0x8000 else imm
    return op, rs, rt, imm, simm


def dispatch(code: bytes):
    """Return (dispatcher address, {class: case address}) for the largest tree."""
    best = (0, {})
    ws = list(words(code))
    for index, (address, w) in enumerate(ws):
        op, rs, rt, imm, simm = decode(w)
        if not (op == 0x21 and rt == 3 and imm == 0x36):    # lh v1,0x36(base)
            continue
        cases, li = {}, None
        # Walk the following comparison tree; it spans at most a few thousand words.
        for a, x in ws[index + 1:index + 4000]:
            o, s, t, i, si = decode(x)
            if o == 0x09 and s == 0 and t == 2:              # addiu v0,zero,CLASS (li)
                li = i
            elif o == 0x04 and s == 3 and t == 2 and li is not None:   # beq v1,v0,target
                cases.setdefault(li, a + 4 + si * 4)
            elif o == 0x0a and s == 3 and t == 2:            # slti v0,v1,imm (tree split)
                pass
        if len(cases) > len(best[1]):
            best = (address, cases)
    return best


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--slots', nargs='*', type=int, default=sorted(LEVELS))
    args = parser.parse_args()
    out = {}
    for slot in args.slots:
        code = overlay(slot)
        where, cases = dispatch(code)
        starts = sorted(set(cases.values()))
        ends = {s: (starts[i + 1] if i + 1 < len(starts) else BASE + len(code)) for i, s in enumerate(starts)}
        out[slot] = {'name': LEVELS.get(slot, ''), 'overlay': slot - 1, 'dispatcher': f'{where:08x}',
                     'classes': {f'{c:x}': {'start': f'{a:08x}', 'end': f'{ends[a]:08x}'} for c, a in sorted(cases.items())}}
        print(f"{slot:3d} {LEVELS.get(slot, ''):18s} dispatcher {where:08x} classes {len(cases)}")
    dest = root / 'data/analysis/moby-dispatch'
    dest.mkdir(parents=True, exist_ok=True)
    (dest / 'dispatch.json').write_text(json.dumps(out, indent=1) + '\n')


if __name__ == '__main__':
    main()

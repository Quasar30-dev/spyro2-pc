"""List authored moby records by class for every level (record index, class,
position), mirroring decode_object_instances. Used to pick script-driven
targets for the retail-script verification captures.

Usage: python tools/retail_script_targets.py [slot ...] [--classes 111,118]
"""
from pathlib import Path
import json, struct, sys

root = Path(__file__).resolve().parents[1]


def level_records(slot: int):
    wad = (root / 'data/disc/WAD.WAD').open('rb')
    header = wad.read(0x10000)
    off, size = struct.unpack_from('<II', header, slot * 8)
    wad.seek(off)
    archive = wad.read(size)
    sec_off, sec_size = struct.unpack_from('<II', archive, 24)
    data = archive[sec_off:sec_off + sec_size]
    at = 0x2c
    for _ in range(8):
        at += struct.unpack_from('<I', data, at)[0]
    count = struct.unpack_from('<I', data, at + 4)[0]
    records = []
    for i in range(count):
        p = at + 8 + i * 88
        x, y, z = struct.unpack_from('<iii', data, p + 12)
        records.append({'index': i, 'class': struct.unpack_from('<H', data, p + 54)[0], 'x': x, 'y': y, 'z': z,
                        'state': struct.unpack_from('<b', data, p + 72)[0], 'spirit': data[p + 83] & 127})
    return records


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    classes = None
    if '--classes' in sys.argv:
        classes = {int(c, 16) for c in sys.argv[sys.argv.index('--classes') + 1].split(',')}
        args = [a for a in args if a != sys.argv[sys.argv.index('--classes') + 1]]
    coverage = json.loads((root / 'data/analysis/level-systems/coverage.json').read_text())
    slots = [int(a) for a in args] or [c['slot'] for c in coverage if c['unimplemented_enemy_classes']]
    out = {}
    for slot in slots:
        wanted = classes or {int(c, 16) for c in next(c for c in coverage if c['slot'] == slot)['unimplemented_enemy_classes']}
        out[slot] = [r for r in level_records(slot) if r['class'] in wanted]
    print(json.dumps(out, indent=1))

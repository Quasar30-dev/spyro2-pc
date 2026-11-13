"""Check corrected family metadata against original ROM records and retained geometry.

Family labels are interpretations established by the cited original routines;
the ROM is the independent oracle for every type, parameter and position.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from gba import Rom, ROM_SHA256
from soi_extract import objects
from soi_families import family

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--snapshot', type=Path, required=True)
    a = ap.parse_args()
    rom = Rom()
    assert hashlib.sha256(rom.data).hexdigest() == ROM_SHA256
    print('ORIGINAL ROM sha256', ROM_SHA256, flush=True)
    inventory = ROOT/'data/soi/inventory.json'
    print('INVENTORY', inventory.resolve(), 'sha256', sha(inventory), flush=True)
    summaries = {row['level']: row for row in json.loads(inventory.read_text())['levels']}
    portal_count = 0
    for path in sorted((ROOT/'data/soi/levels').glob('*/level.json')):
        data = json.loads(path.read_text()); level = data['summary']['level']
        expected = objects(rom, level, list(rom.bytes(0x081ab8f0, 4)))
        assert data['objects'] == expected, (level, 'source object records or family metadata differ')
        counts = dict(sorted(Counter(f"{o['family']}: {o['family_name']}" for o in expected['objects']).items()))
        assert data['summary']['families'] == counts
        assert summaries[level] == data['summary']
        before = json.loads((a.snapshot/path.relative_to(ROOT)).read_text())
        after = json.loads(path.read_text())
        for obj in (before, after):
            obj['summary'].pop('families')
            for record in obj['objects']['objects']:
                for key in ('family', 'family_name', 's2', 's2_placeholder'):
                    record.pop(key, None)
        assert before == after, (level, 'non-family data changed')
        portal_count += sum(o['family'] == 'portal' for o in expected['objects'])
        print('LEVEL', level, path.resolve(), 'sha256', sha(path),
              'source records exact; all non-family data equal pre-edit snapshot', flush=True)
    assert all(family(t)['family'] == 'portal' for t in range(40, 82))
    mesa = json.loads((ROOT/'data/soi/levels/06-market-mesa/level.json').read_text())
    portal = mesa['objects']['objects'][148]
    assert portal['type'] == 41 and portal['family'] == 'portal' and portal['pos'] == [816., 51., 384.]
    print('FAMILY METADATA CHECK PASS', sha(inventory), '17 source levels;', portal_count,
          'portal records; Mesa index148 type41 is portal; geometry/placements unchanged', flush=True)


if __name__ == '__main__':
    main()

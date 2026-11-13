"""Compare the corrected exporter with retained original-ARM execution evidence.

This reuses the coordinator's immutable manifests; it does not rerun or replace
the independent ARM interpreter, and proves no semantic animation labels.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from gba import Rom, ROM_SHA256
import soi_sprites as sprites

ROOT = Path(__file__).resolve().parents[2]
ORACLES = {
    'critter': ('critter', 'c6f1331482ca3b22edd71558c601734e9b554e42c7237e7c38529097cb38bca1'),
    'spear-guard': ('spear-guard', 'dace02ff4e8fa8b2a1b57fe57441666dc2ecd927e06a7db08e7dc2833095c9fb'),
    'robed-rhynoc': ('robed-rhynoc', '910b330bd38eb9e8eb5ab2ca79657af0b786224b897bcf9aa909ba3355793e10'),
    'return-portal': ('return-portal', '26f40d6b828b740cabc490799de0fbba999370fd2330bb81c3f0c2684b92bdae'),
    'grendor-arena15': ('grendor-final', 'b7d642b041fd0c97dbbce14614c93c17c500851dcdaf811d1a4545b389b6c5e1'),
    'grendor-arena16': ('grendor-final', 'b7d642b041fd0c97dbbce14614c93c17c500851dcdaf811d1a4545b389b6c5e1'),
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--manifest', type=Path, default=ROOT/'data/soi/content-20261105/source-manifest.json')
    a = ap.parse_args()
    print('EXPORT', a.manifest.resolve(), 'sha256', sha(a.manifest), flush=True)
    print('EXPORTER', Path(sprites.__file__).resolve(), 'sha256', sha(Path(sprites.__file__)), flush=True)
    rom = Rom()
    assert hashlib.sha256(rom.data).hexdigest() == ROM_SHA256
    print('ORIGINAL ROM sha256', ROM_SHA256, flush=True)
    manifest = json.loads(a.manifest.read_text())
    assert manifest['sha256'] == ROM_SHA256
    assert {b['name'] for b in manifest['banks']} == set(ORACLES)
    total = 0
    for row in manifest['banks']:
        folder, digest = ORACLES[row['name']]
        path = ROOT/'data/soi/content-20261105/coordinator-sprite-oracle'/folder/'original-arm-manifest.json'
        assert sha(path) == digest, path
        oracle = json.loads(path.read_text())
        assert oracle['rom_sha256'] == ROM_SHA256 and oracle['bank'] == row['source']
        assert len(row['frames']) == len(oracle['frames'])
        bank = sprites.bank(rom, int(row['source'], 16))
        assert bank['header'] == row['header']
        for frame, expected in zip(row['frames'], oracle['frames']):
            assert frame['frame'] == expected['frame']
            assert frame['dimensions'] == expected['dimensions']
            assert frame['origin'] == expected['origin']
            assert frame['palette_bank'] == expected['palette_bank'] == row['initial_palette_bank']
            rgba = np.asarray(Image.open(frame['path']).convert('RGBA'))
            saved_oracle = np.asarray(Image.open(expected['path']).convert('RGBA'))
            assert hashlib.sha256(saved_oracle.tobytes()).hexdigest() == expected['rgba_sha256']
            assert hashlib.sha256(rgba.tobytes()).hexdigest() == frame['rgba_sha256'] == expected['rgba_sha256']
            assert np.array_equal(rgba, saved_oracle), (row['name'], frame['frame'])
        total += len(row['frames'])
        print('ORIGINAL ARM EXPORT EQUALITY', row['name'], len(row['frames']), 'oracle', path.resolve(),
              'sha256', digest, flush=True)
    assert total == 646  # 398 distinct frames; the identical 248-frame arena bank is exported twice.
    print('SOURCE EXPORT CHECK PASS', sha(a.manifest), total,
          'frame exports exact RGBA/dimensions/origins/initial palettes against retained original ARM evidence', flush=True)
    print('LIMIT initial palettes only; no semantic clips, full GBA runtime or game integration proved', flush=True)


if __name__ == '__main__':
    main()

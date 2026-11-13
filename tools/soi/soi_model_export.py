"""Hash-bound local model export; the C++ probe executes original S2 instructions.

The source bank census is reused. No generators, game session or source asset
modifications. A decoded bank alone does not prove behavior/clip/sound import.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('slot', type=int)
    ap.add_argument('classes')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--probe', type=Path, default=ROOT/'build/soi-content-sol-phase2/yotd_model_export.exe')
    a = ap.parse_args()
    assert not a.out.exists(), 'retain prior exports; choose a new destination'
    census_path = ROOT/'data/soi/content-20261105/yotd-model-census.json'
    census = json.loads(census_path.read_text())
    wad, exe = Path(census['source_wad']), Path(census['source_exe'])
    assert sha(wad) == census['source_wad_sha256']
    assert sha(exe) == census['source_exe_sha256']
    print('ORIGINAL WAD', wad.resolve(), 'sha256', sha(wad), flush=True)
    print('ORIGINAL EXE', exe.resolve(), 'sha256', sha(exe), flush=True)
    print('S2 INSTRUCTION ORACLE', (ROOT/'data/disc/SCUS_944.25').resolve(), 'sha256',
          sha(ROOT/'data/disc/SCUS_944.25'), flush=True)
    print('PROBE', a.probe.resolve(), 'sha256', sha(a.probe), flush=True)
    print('CENSUS', census_path.resolve(), 'sha256', sha(census_path), flush=True)
    result = subprocess.run([str(a.probe.resolve()), str(wad.parent), str(a.slot), str(a.out.resolve()), a.classes],
                            cwd=ROOT, timeout=240)
    assert result.returncode == 0, 'original consumer/export failed'
    path = a.out/'models.json'; model_data = json.loads(path.read_text())
    references = {row['class']: row for row in census['models'] if row['slot'] == a.slot}
    for model in model_data['models']:
        row = references[model['class']]
        assert model['source_offset'] == row['bank_begin'] and model['source_bytes'] == row['bytes']
        assert len(model['clips']) == max(0, row['clip_count'])
        print('SOURCE BANK', hex(model['class']), 'sha256', row['bank_sha256'], flush=True)
    rgba = a.out/'atlas.rgba'
    image = Image.frombytes('RGBA', (model_data['texture_width'], model_data['texture_height']), rgba.read_bytes())
    image.save(a.out/'atlas.png')
    print('MODEL EXPORT', path.resolve(), 'sha256', sha(path), flush=True)
    print('ATLAS', (a.out/'atlas.png').resolve(), 'sha256', sha(a.out/'atlas.png'), flush=True)
    print('LIMIT original near-vertex slices only; far/face/CLUT, identity and engine integration require separate evidence', flush=True)


if __name__ == '__main__':
    main()

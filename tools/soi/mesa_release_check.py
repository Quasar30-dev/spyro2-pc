"""Targeted release invariants for the test05 Moneybags-only gameplay change."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import retire_mesa
from restore_mesa_baselines import PINNED
from spyrodisc import wad_index

ROOT=Path(__file__).resolve().parents[2]
STAGE=Path('D:/Theia-staging/spyro2-mod')


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--package',type=Path)
    a=ap.parse_args()
    image=STAGE/'Spyro2-SoI-MarketMesa-test05.bin'; digest=sha(image)
    print('ARTIFACT',image,'sha256',digest)
    report=json.loads((ROOT/'data/soi/ps1/mesa-disc-report.json').read_text())
    assert report['sha256']==digest and report['fixture'] is False and report['readback_slot_equal']
    archive=Path(report['archive']); new=archive.read_bytes()
    old_path=STAGE/'work/sol-retry-20261105/final/slot17-mesa.bin'; old=old_path.read_bytes()
    assert hashlib.sha256(old).hexdigest()=='2bde6d08a64ad79e6b6ee797780e8007cdf99845b3ebf445c5321528a52e24b4'
    assert sha(archive)==report['archive_sha256']
    assert len(old)==len(new)
    delta=[i for i,(x,y) in enumerate(zip(old,new)) if x!=y]
    ch=wad_index(new[:65536],len(new)); offset=ch[3].offset+0x327c+8+88*9+0x48
    assert delta==[offset] and (old[offset],new[offset])==(0,0xfd),delta
    print('ARCHIVE DELTA PASS',digest,'archive sha256',sha(archive),'vs test04',sha(old_path),
          'one byte at',hex(offset),'0x00 -> 0xfd; terrain, textures, visibility and remaining gameplay bytes identical')
    before=json.loads((ROOT/'data/soi/ps1/review-test05-before.json').read_text())
    for path,expected in before.items():
        actual=sha(Path(path)); assert actual==expected,(path,actual,expected)
        print('PRESERVATION PASS',digest,path,'sha256',actual)
    package=a.package or Path('C:/Users/Shadow/Documents/Theia/dist/spyro2-soi-marketmesa-test05-20261105.zip')
    for label,(bin_sha,cue_sha) in PINNED.items():
        baseline=STAGE/f'Spyro2-SoI-MarketMesa-{label}.bin'
        assert sha(baseline)==bin_sha and sha(baseline.with_suffix('.cue'))==cue_sha
        for path in (baseline,baseline.with_suffix('.cue'),package.with_name(f'spyro2-soi-marketmesa-{label}-20261105.zip')):
            try: retire_mesa.validate_retirement(path,image,package)
            except AssertionError as e: assert str(e)=='explicitly protected baseline'
            else: raise AssertionError('protected path accepted: '+str(path))
        print('BASELINE PROTECTION PASS',baseline,'sha256',bin_sha,'CUE',cue_sha)
    for p in (ROOT/'data/soi/ps1/recovery-20261105',ROOT/'data/soi/ps1/recovery-sol-20261105'):
        assert p.is_dir() and any(p.iterdir()); print('RETAINED recovery',p)
    if a.package:
        receipt=json.loads((STAGE/'patcher'/f'{package.stem}-verify.json').read_text())
        assert sha(package)==receipt['zip_sha256'] and receipt['target']['sha256']==digest
        assert all(receipt['end_to_end'][key]['sha256']==digest for key in ('gui','script'))
        print('RELEASE PACKAGE IDENTITY PASS',digest,package,'sha256',sha(package))
    print('RELEASE INVARIANTS PASS',digest)


if __name__=='__main__': main()

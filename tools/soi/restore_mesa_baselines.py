"""Restore only the explicitly protected test02/test03 images from local backups."""
import hashlib
import subprocess
import sys
from pathlib import Path

STAGE=Path('D:/Theia-staging/spyro2-mod')
ROOT=Path(__file__).resolve().parents[2]
PINNED={
    'test02':('2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb',
              '478e39bf4639a6750953970c56759e621fccb9791fa9f0aad9f3f0fc983a1da4'),
    'test03':('60680946e3be8fe3546c4173909ae8a31b54e9316935a0a3e105496d7e68308c',
              '1074a4ebc2b4d475c027e204a7b1ca01ea499019aef52a60d591b6678f39aba5')}


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    backup=STAGE/'work/slot17-mesa-test02.bin'
    assert sha(backup)=='b7150b290b88ff15a9efe555f12261bced9e77a64f6f872606d5ab7640e4df8b'
    print('BACKUP',backup,'sha256',sha(backup),flush=True)
    target=STAGE/'Spyro2-SoI-MarketMesa-test02.bin'
    if not target.exists():
        assert not target.with_suffix('.cue').exists()
        subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('mesa_disc.py')),'test02',
                        '--archive',str(backup),'--report',str(ROOT/'data/soi/ps1/restored-test02-report.json')],check=True)
    target=STAGE/'Spyro2-SoI-MarketMesa-test03.bin'
    if not target.exists():
        archive=STAGE/'Spyro2-SoI-MarketMesa-test03.rar'
        print('BACKUP',archive,'sha256',sha(archive),flush=True)
        subprocess.run(['C:/Program Files/WinRAR/UnRAR.exe','e','-o-','-y',str(archive),target.name,str(STAGE)+'/'],
                       check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    for label,(bin_sha,cue_sha) in PINNED.items():
        image=STAGE/f'Spyro2-SoI-MarketMesa-{label}.bin'
        cue=image.with_suffix('.cue')
        assert sha(image)==bin_sha,(image,sha(image))
        # The pinned originals use CRLF. Explicit bytes avoid shell/locale changes.
        if not cue.exists():
            cue.write_bytes(f'FILE "{image.name}" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n'.encode('ascii'))
        assert sha(cue)==cue_sha,(cue,sha(cue))
        print('BASELINE RESTORE PASS',image,'sha256',bin_sha,'CUE',cue_sha,flush=True)
    print('PROTECTED test02 and test03 BIN/CUE; recovery snapshots retained; test01 remains retired',flush=True)


if __name__=='__main__': main()

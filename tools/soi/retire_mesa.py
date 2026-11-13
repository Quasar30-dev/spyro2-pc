"""Retire explicit verified Market Mesa artifacts; test02/test03 are protected.

No wildcard discovery. Hold DELETE-only sharing handles while safe_delete runs;
an existing reader prevents retirement, without inspecting any user process.
"""
import argparse
import ctypes as c
import ctypes.wintypes as w
import hashlib
import json
import subprocess
import sys
from pathlib import Path

STAGE=Path('D:/Theia-staging/spyro2-mod')
DIST=Path('C:/Users/Shadow/Documents/Theia/dist')
HELPER=Path('C:/Users/Shadow/Documents/Theia/tools/claude_guard/safe_delete.py')
PROTECTED=('test02','test03')


def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def validate_retirement(p,current,package):
    p=p.resolve()
    assert p not in (current.resolve(),current.with_suffix('.cue').resolve(),package.resolve()),'current artifact protected'
    assert not any(label in p.name.lower() for label in PROTECTED),'explicitly protected baseline'
    image=p.parent==STAGE.resolve() and p.name.startswith('Spyro2-SoI-MarketMesa-') and p.suffix in ('.bin','.cue')
    zipped=p.parent==DIST.resolve() and p.name.startswith('spyro2-soi-marketmesa-') and p.suffix=='.zip'
    assert image or zipped,'not an exact Market Mesa image or ZIP in its declared output root'
    return p


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--current',type=Path,required=True)
    ap.add_argument('--package',type=Path,required=True)
    ap.add_argument('--retire',type=Path,action='append',required=True)
    a=ap.parse_args()
    current=a.current.resolve(); package=a.package.resolve()
    verify=json.loads((STAGE/'patcher'/f'{package.stem}-verify.json').read_text())
    digest=sha(current)
    assert verify['target']['sha256']==digest and verify['zip_sha256']==sha(package)
    assert Path(verify['zip']).resolve()==package
    assert all(verify['end_to_end'][key]['sha256']==digest for key in ('gui','script'))
    groups=[]
    for p in a.retire:
        p=validate_retirement(p,current,package)
        group=[p.with_suffix(s) for s in ('.bin','.cue')] if p.suffix in ('.bin','.cue') else [p]
        group=[validate_retirement(q,current,package) for q in group]
        if group not in groups: groups.append(group)
    print('VERIFIED CURRENT',current,'sha256',digest,'patcher',sha(package),flush=True)
    print('PROTECTED test02/test03 images, patchers, retained recovery snapshots and user archives',flush=True)
    k=c.WinDLL('kernel32',use_last_error=True)
    k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
    k.CreateFileW.restype=w.HANDLE; k.CloseHandle.argtypes=[w.HANDLE]
    k.GetFileSizeEx.argtypes=[w.HANDLE,c.POINTER(c.c_longlong)]
    k.ReadFile.argtypes=[w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p]
    for group in groups:
        paths=[p for p in group if p.exists()]; handles=[]; locked=False
        try:
            for p in paths:
                handle=k.CreateFileW(str(p),0x80000000,4,None,3,0x80,None)
                if handle==c.c_void_p(-1).value:
                    print('RETAINED in-use/unavailable group',','.join(map(str,paths)),'Win32',c.get_last_error(),flush=True)
                    locked=True; break
                handles.append(handle)
            if locked or not paths: continue
            identities={}; buf=c.create_string_buffer(4<<20)
            for p,handle in zip(paths,handles):
                size=c.c_longlong(); assert k.GetFileSizeEx(handle,c.byref(size))
                digest_=hashlib.sha256()
                for at in range(0,size.value,len(buf)):
                    got=w.DWORD(); want=min(len(buf),size.value-at)
                    assert k.ReadFile(handle,buf,want,c.byref(got),None) and got.value==want
                    digest_.update(buf.raw[:got.value])
                identities[str(p)]=digest_.hexdigest()
            subprocess.run([sys.executable,str(HELPER),*map(str,paths)],check=True)
            for p in paths:
                assert not p.exists()
                print('RETIRED',p,'sha256',identities[str(p)],'via',HELPER,flush=True)
        finally:
            for h in handles: k.CloseHandle(h)


if __name__=='__main__': main()

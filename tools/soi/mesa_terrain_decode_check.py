"""Compare existing native decoder pixels with the original MIPS UV consumer."""
import argparse
import hashlib
import json
import struct
import subprocess
from collections import Counter
from pathlib import Path
import numpy as np
import mesa_uv_check as oracle
from spyrodisc import wad_index
import s2scene_build


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive',type=Path);ap.add_argument('--probe',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    print('ARTIFACT',a.archive.resolve(),'sha256',sha(a.archive),flush=True)
    print('NATIVE PROBE',a.probe.resolve(),'sha256',sha(a.probe),flush=True)
    print('ORIGINAL ORACLE',oracle.mipsdis.EXE,'sha256',sha(oracle.mipsdis.EXE),flush=True)
    atlas=a.out/'native-atlas.rgba'
    p=subprocess.run([str(a.probe.resolve()),str(a.archive.resolve()),str(atlas.resolve())],capture_output=True,text=True,check=True)
    print(p.stdout.strip(),flush=True)
    arc=a.archive.read_bytes();ch=wad_index(arc[:65536],len(arc))
    sc=s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size]);count=struct.unpack_from('<I',sc['parts'][0],4)[0]
    native=np.frombuffer(atlas.read_bytes(),np.uint8).reshape(-1,1024,4)
    vram=np.frombuffer(arc,'<u2',512*512,struct.unpack_from('<I',arc)[0]).reshape(512,512)
    bad=Counter();seen=Counter();pixels=0;examples=[]
    for m in range(count):
        for q in range(4):
            d=sc['parts'][0][8+m*48+16+q*8:8+m*48+24+q*8];flag=d[7]&0x70
            words=oracle.decode(d,vram)
            expected=np.stack([((((words>>(5*k))&31)<<3)|(((words>>(5*k))&31)>>2)).astype(np.uint8) for k in range(3)]+[np.where(words==0,0,np.where(words&0x8000,128,255)).astype(np.uint8)],-1)
            x=m%16*64+q%2*32;y=m//16*64+q//2*32
            actual=native[y:y+32,x:x+32]
            errors=int(np.count_nonzero(np.any(expected!=actual,axis=-1)))
            seen[hex(flag)]+=1;bad[hex(flag)]+=errors;pixels+=1024
            if errors and len(examples)<12:examples.append(dict(material=m,quadrant=q,flag=hex(flag),bad_pixels=errors))
    result=dict(archive_sha256=sha(a.archive),probe_sha256=sha(a.probe),native_atlas_sha256=sha(atlas),
                pixels=pixels,cells_by_flag=dict(seen),bad_pixels_by_flag=dict(bad),examples=examples)
    path=a.out/'native-original-pixels.json';path.write_text(json.dumps(result,indent=2)+'\n')
    print('FLAG COMPARISON',json.dumps(result),flush=True)
    assert not sum(bad.values()),'native decoder differs from actual original UV consumer'
    print('NATIVE/ORIGINAL TERRAIN PIXELS PASS',sha(a.archive),pixels,'RGBA texels exact through both consumers;',path,flush=True)


if __name__=='__main__':main()

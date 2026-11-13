"""Retail-format measurements and old/new far-vertex comparison (offline)."""
import argparse
import hashlib
import struct
from collections import Counter
from pathlib import Path
import numpy as np
import mesa_ps1 as mesa
import s2scene_build as scene
import s2regions as regions
from s2scene import read_archive
from spyrodisc import wad_index


def split(arc):
    ch=wad_index(arc[:65536],len(arc))
    return ch,scene.split(arc[ch[1].offset:ch[1].offset+ch[1].size])


def geometry(arc):
    _,sc=split(arc)
    rs=regions.decode(sc['parts'][1])['records']
    foreign=0; count=0; faces=0
    for r in rs:
        near=set(struct.unpack('<'+'I'*(len(r.hp_verts)//4),r.hp_verts))
        far=struct.unpack('<'+'I'*(len(r.lp_verts)//4),r.lp_verts)
        foreign+=sum(v not in near for v in far); count+=len(far); faces+=r.header[18]
    return dict(regions=len(rs),far_vertices=count,far_faces=faces,far_vertices_absent_from_near=foreign)


def near_polygons(arc):
    _,sc=split(arc); out=Counter()
    for r in regions.decode(sc['parts'][1])['records']:
        y,x,_,z=struct.unpack_from('<4H',r.header,8)
        origin=(x*16,y*16,z*16)
        pos=[]
        for (v,) in struct.iter_unpack('<I',r.hp_verts):
            pos.append((origin[0]+(v>>21)*16,origin[1]+((v>>10)&2047)*16,origin[2]+(v&1023)*32))
        for j in range(r.header[22]):
            f=r.hp_faces[16*j:16*j+16]
            inds=(3,2,1) if f[0]==f[1] else (3,2,1,0) # front outline
            loop=tuple(pos[f[k]] for k in inds)
            out[min(loop[i:]+loop[:i] for i in range(len(loop)))]+=1
    return out


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('candidate',type=Path); ap.add_argument('baseline',type=Path)
    a=ap.parse_args()
    print('CANDIDATE',a.candidate,'sha256',hashlib.sha256(a.candidate.read_bytes()).hexdigest())
    print('BASELINE',a.baseline,'sha256',hashlib.sha256(a.baseline.read_bytes()).hexdigest())
    old=geometry(a.baseline.read_bytes()); new=geometry(a.candidate.read_bytes())
    print('BASELINE far geometry',old); print('CANDIDATE far geometry',new)
    assert old['far_vertices_absent_from_near']>0 and new['far_vertices_absent_from_near']==0
    assert near_polygons(a.baseline.read_bytes())==near_polygons(a.candidate.read_bytes())
    print('CHECK PASS eliminates displaced far vertices without changing near vertex coordinates')
    total=0; equal=0; samples=0; max_error=0; descriptor_bad=0; maxima=[0]*7
    for slot in (15,17,29,33,35,47):
        arc=read_archive(mesa.DISC,slot); _,sc=split(arc)
        vram=np.frombuffer(arc,'<u2',512*512,struct.unpack_from('<I',arc)[0]).reshape(512,512)
        nm=struct.unpack_from('<I',sc['parts'][0],4)[0]
        for m in range(nm):
            for k in range(6):
                d=sc['parts'][0][8+(m*6+k)*8:16+(m*6+k)*8]
                descriptor_bad+=d[4]-d[0]!=31 or d[5]-d[1]!=31
            clut=struct.unpack_from('<H',sc['parts'][0],10+m*48)[0]
            x,y=(clut&63)*16-512,(clut>>6)&511
            actual=vram[y:y+16,x:x+16]; fitted=mesa.far_clut_fade(actual[0])
            aa=np.stack([actual&31,(actual>>5)&31,(actual>>10)&31],-1).astype(int)
            bb=np.stack([fitted&31,(fitted>>5)&31,(fitted>>10)&31],-1).astype(int)
            diff=np.abs(aa[2:15]-bb[2:15])
            equal+=int((diff==0).sum()); samples+=diff.size; max_error=max(max_error,int(diff.max()))
            total+=1
        for r in regions.decode(sc['parts'][1])['records']:
            counts=[r.header[i] for i in (16,17,18,20,21,22,23)]
            maxima=[max(x,y) for x,y in zip(maxima,counts)]
        print('RETAIL slot',slot,'archive sha256',hashlib.sha256(arc).hexdigest(),'materials',nm)
    print('RETAIL observed maxima [lv,lc,lf,hv,hc,hf,lw]',maxima)
    assert descriptor_bad==0
    print('CHECK PASS all original descriptors are full 32x32 cells; materials',total)
    print('MEASUREMENT fade fit rows 2..14 RGB5 channels exact',equal,'/',samples,
          f'({100*equal/samples:.4f}%)','maximum channel error',max_error,
          '; fitted design, not a recovered exact generation formula')
    print('ORACLE STRUCTURAL CHECK PASS')


if __name__=='__main__': main()

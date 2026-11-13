"""Check actual converted archive bytes against retained original donor banks.

Executes the original S2 loader and bounded near/far vertex instructions via the
task-owned C++ probe. Samples every texel in each used face rectangle, including
transparent/STP values, and checks copied SPU streams and descriptor metadata.
This is an asset ABI check; it does not claim actor AI or original GPU playback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
import soi_model_convert as fmt
from s2scene import read_archive
from spyrodisc import wad_index

ROOT=Path(__file__).resolve().parents[2]
def sha(b): return hashlib.sha256(b).hexdigest()


def sample(vram,page,clut,u,v):
    depth=(page>>7)&3;assert depth<=1
    ppw=2 if depth else 4;bits=8 if depth else 4
    words=vram[((page>>4)&1)*256+v,(page&15)*64+u//ppw-512].astype(np.uint32)
    indices=(words>>((u%ppw)*bits))&((1<<bits)-1)
    return vram[(clut>>6)&511,(clut&63)*16-512+indices]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path)
    ap.add_argument('--probe',type=Path,default=ROOT/'build/soi-content-sol-phase2/soi_model_abi_check.exe')
    ap.add_argument('--container-proof',type=Path)
    a=ap.parse_args();out=a.directory.resolve()
    path=out/'model-conversion.json';report=json.loads(path.read_text())
    container=None
    if a.container_proof:
        container=json.loads(a.container_proof.read_text())
        assert container['base_archive_sha256']==report['archive_sha256']
        report.update(archive=container['archive'],archive_sha256=container['archive_sha256'])
        print('CONTAINER PROOF',a.container_proof.resolve(),'sha256',sha(a.container_proof.read_bytes()),flush=True)
    arc=Path(report['archive']).read_bytes()
    digest=sha(arc);assert digest==report['archive_sha256']
    print('ARCHIVE',Path(report['archive']).resolve(),'sha256',digest,flush=True)
    print('REPORT',path,'sha256',sha(path.read_bytes()),flush=True)
    print('PROBE',a.probe.resolve(),'sha256',sha(a.probe.read_bytes()),flush=True)
    for name in ('data/disc/SCUS_944.25','tools/soi/soi_model_abi_check.cpp','tools/soi/yotd_model_export.cpp'):
        p=ROOT/name;print('ORACLE/PROBE SOURCE',p,'sha256',sha(p.read_bytes()),flush=True)
    loader=read_archive(ROOT/'data/disc',1);print('ORIGINAL LOADER1 SLOT1 sha256',sha(loader),flush=True)
    base=Path(report['base_archive']).read_bytes();assert sha(base)==report['base_archive_sha256']
    bch=wad_index(base[:65536],len(base));ch=wad_index(arc[:65536],len(arc))
    expected_gameplay=bytearray(base[bch[3].offset:bch[3].offset+bch[3].size])
    for change in report.get('inactive_glimmer_records',[]):
        assert change['state']==0xfd and expected_gameplay[change['offset']]==change['previous']
        expected_gameplay[change['offset']]=change['state']
    actual_gameplay=arc[ch[3].offset:ch[3].offset+ch[3].size]
    if container:
        table=0x327c;end=table+fmt.u32(expected_gameplay,table);insert=end+fmt.u32(expected_gameplay,end)
        count=fmt.u32(expected_gameplay,table+4);assert count==220 and fmt.u32(actual_gameplay,table+4)==225
        changes={e['old_index']:e for e in container['containers']};assert len(changes)==5
        expected_prefix=bytearray(expected_gameplay[:end]);fmt.put32(expected_prefix,table+4,225)
        for old_index,e in changes.items():
            p=table+8+88*old_index;assert expected_prefix[p+72]==0
            original_record=bytes(expected_prefix[p:p+88]);expected_prefix[p+72]=0xfd
            newp=table+8+88*e['index'];assert count<=e['index']<225 and e['parameter']==insert+64+28*(e['index']-count)
            clone=bytearray(original_record);struct.pack_into('<H',clone,54,0xc9);fmt.put32(clone,0,e['parameter'])
            assert not any(expected_prefix[newp:newp+88]);expected_prefix[newp:newp+88]=clone
        assert actual_gameplay[:end]==expected_prefix
        assert fmt.u32(actual_gameplay,end)==fmt.u32(expected_gameplay,end)+204
        assert actual_gameplay[end+4:insert]==expected_gameplay[end+4:insert]
        assert sha(actual_gameplay[insert:insert+64])==container['wobble_sha256']
        for e in changes.values():
            p=e['parameter'];expected=bytearray(28);fmt.put32(expected,24,insert)
            assert actual_gameplay[p:p+28]==expected
        assert actual_gameplay[insert+204:insert+212]==expected_gameplay[insert:insert+8]
        nr=fmt.u32(expected_gameplay,insert+8);oldrefs=list(struct.unpack_from('<'+'I'*nr,expected_gameplay,insert+12))
        newnr=fmt.u32(actual_gameplay,insert+212);assert newnr==nr+10
        newrefs=list(struct.unpack_from('<'+'I'*newnr,actual_gameplay,insert+216))
        assert newrefs[:nr]==oldrefs
        assert set(newrefs[nr:])=={x for e in changes.values() for x in (table+8+88*e['index'],e['parameter']+24)}
        assert not any(actual_gameplay[insert+216+4*newnr:])
    else:
        assert actual_gameplay==expected_gameplay
    sc=fmt.s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size])
    old=fmt.s2scene_build.split(base[bch[1].offset:bch[1].offset+bch[1].size])
    assert sc['parts'][1:]==old['parts'][1:] and sc['collision']==old['collision'] and sc['sky']==old['sky'] and sc['words']==old['words']
    _,_,_,tdesc,tsamples=fmt.sound_table(arc,False)
    target_vram=np.frombuffer(arc,'<u2',512*512,ch[0].offset).reshape(512,512)
    census=json.loads((ROOT/'data/soi/content-20261105/yotd-model-census.json').read_text())
    wad=Path(census['source_wad']);assert hashlib.file_digest(wad.open('rb'),'sha256').hexdigest()==report['source_wad_sha256']
    all_texels=0;all_faces=0;all_sounds=0
    for model in report['models']:
        cls=model['target_class'];slot=model['slot'];source_class=model['source_class']
        original_arc=read_archive(wad.parent,slot)
        ref=next(m for m in census['models'] if m['slot']==slot and m['class']==source_class)
        original=original_arc[ref['bank_begin']:ref['bank_begin']+ref['bytes']]
        assert sha(original)==model['source_bank_sha256']==ref['bank_sha256']
        target=(out/f'{cls:03x}-s2.bank').read_bytes()
        assert sha(target)==model['bank_sha256']
        index=next(i for i in range(1,63) if struct.unpack_from('<H',arc,0x140+2*i)[0]==cls)
        begin,end=struct.unpack_from('<II',arc,64+4*index)
        assert arc[begin:end]==target
        source_vram=np.frombuffer(original_arc,'<u2',512*512,fmt.u32(original_arc,0)).reshape(512,512)
        texels=0;packets=0
        for p,textured in fmt.faces(original):
            if not textured:continue
            uv=np.array([(original[p+k],original[p+k+1]) for k in (8,12,16,18)],int)
            newuv=np.array([(target[p+k],target[p+k+1]) for k in (8,12,16,18)],int)
            delta=newuv-uv;assert np.all(delta==delta[0])
            lo,hi=uv.min(0),uv.max(0)
            u,v=np.meshgrid(np.arange(lo[0],hi[0]+1),np.arange(lo[1],hi[1]+1))
            sp,scp=struct.unpack_from('<H',original,p+14)[0],struct.unpack_from('<H',original,p+10)[0]
            tp,tcp=struct.unpack_from('<H',target,p+14)[0],struct.unpack_from('<H',target,p+10)[0]
            assert (sp&~31)==(tp&~31), 'GPU depth/blend/STP flags'
            assert np.array_equal(sample(source_vram,sp,scp,u,v),sample(target_vram,tp,tcp,u+delta[0,0],v+delta[0,1]))
            texels+=u.size;packets+=1
        _,_,_,sd,ss=fmt.sound_table(original_arc,True)
        for at in range(4,16):
            if original[at]==255:assert target[at]==255;continue
            s,t=sd[original[at]],tdesc[target[at]]
            assert s[4:]==t[4:]
            assert fmt.sample_bytes(ss,fmt.u32(s,0))==fmt.sample_bytes(tsamples,fmt.u32(t,0))
            all_sounds+=1
        assert (out/f'{cls:03x}-source.bank').read_bytes()==original
        print('BANK',hex(cls),'sha256',sha(target),'original',sha(original),'slot/class',slot,hex(source_class),flush=True)
        subprocess.run([str(a.probe.resolve()),str(out/f'{cls:03x}-source.bank'),str(out/f'{cls:03x}-s2.bank'),f'{cls:x}'],cwd=ROOT,timeout=180,check=True)
        print('EXACT FACE TEXELS',hex(cls),packets,'packets',texels,'RGB5/zero/STP values',flush=True)
        all_texels+=texels;all_faces+=packets
    print('MODEL CONVERSION ABI PASS',digest,len(report['models']),'banks;',all_faces,'textured packets;',all_texels,'face-rectangle texels;',all_sounds,'SPU references;',
          'terrain polygons/collision/sky exact base; declared inactive states checked; other gameplay exact base',flush=True)
    print('LIMIT no class AI, story/dialogue, original GPU/audio playback or full content completion claim',flush=True)


if __name__=='__main__':main()

"""Check an expanded Mesa image against every original retail WAD payload.

Includes a tiny linear evaluator for this mod's generated metadata installer;
the original table bytes and Ghidra dispatch remain the reference. This does
not substitute for original CPU portal, return, save or cinematic checks.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
import ps1disc
import mipsdis
import s2scene_build
import s2regions
from mesa_extra_loader import BASE,SCENE,ORDINAL,MENU,CALLBACK_CALL


def sha(b):return hashlib.sha256(b).hexdigest()
def u32(b,p=0):return struct.unpack_from('<I',b,p)[0]
def slot(model,i):
    offset,size=struct.unpack_from('<II',model.raw[500,24:24+2048],i*8)
    data=bytes(model.raw[500+offset//2048:500+(offset+size+2047)//2048,24:24+2048].reshape(-1))[:size] if size else b''
    return data,(offset,size)


def evaluate_installer(loader,pc):
    registers=[0]*32;writes=[]
    for _ in range(256):
        w=u32(loader,pc-BASE);pc+=4;op=w>>26;s=w>>21&31;t=w>>16&31;imm=w&65535
        if op==15:registers[t]=imm<<16
        elif op==13:registers[t]=registers[s]|imm
        elif op in (40,43):
            address=(registers[s]+(imm if imm<32768 else imm-65536))&0xffffffff
            writes.append((address,1 if op==40 else 4,registers[t]&255 if op==40 else registers[t]))
        elif w==0:pass
        elif w==0x03e00008:
            assert u32(loader,pc-BASE)==0
            return writes
        else:raise AssertionError(('unexpected installer instruction',hex(pc-4),hex(w)))
    raise AssertionError('installer finite bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--manifest',type=Path,required=True);ap.add_argument('--portal-report',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    model=ps1disc.DiscModel(a.cue.resolve());retail=ps1disc.DiscModel(ps1disc.RETAIL_CUE)
    digest=ps1disc.sha256_file(model.bin);source=ps1disc.sha256_file(retail.bin)
    assert source=='b3fd9fdb24b115fee4583dbe3c36ab788fd8679fb4592dbec8736392dec64551'
    print('ARTIFACT',model.bin,'sha256',digest,flush=True)
    print('RETAIL ORACLE',retail.bin,'sha256',source,flush=True)
    manifest=json.loads(a.manifest.read_text());proof=manifest['extra_scene'];portal=json.loads(a.portal_report.read_text())
    assert proof['scene_id']==14 and proof['scene_id_hex']=='0xe' and proof['progress_ordinal']==29
    assert proof['new_scene_absent_from_original_campaign_and_dispatch'] and 14 not in proof['original_dispatch_case_ids']
    assert proof['cinematic70_shared_ordinal29_metadata_restored']
    overrides={int(i):item for i,item in manifest['replacements'].items()};assert set(overrides)=={1,15,198,199}
    identical=[]
    for i in range(200):
        current,_=slot(model,i)
        if i in overrides:
            item=overrides[i];expected=Path(item['path']).read_bytes();assert sha(expected)==item['sha256'] and current==expected
        elif i<197:
            original,_=slot(retail,i);assert current==original,('original WAD payload changed',i);identical.append(i)
        else:assert not current,('unexpected added payload',i)
    assert 16 in identical and 17 in identical and len(identical)==195
    print('ORIGINAL WAD PAYLOADS PASS',digest,'195 undeclared original payloads exact, including Glimmer16/17; loader1 and Summer15 explicitly extended; only new198/199',flush=True)
    exe=next(f for f in model.files if f.path=='SCUS_944.25')
    resident=bytes(model.raw[exe.lba:exe.lba+(exe.size+2047)//2048,24:24+2048].reshape(-1))[:exe.size]
    assert sha(resident)==proof['original_executable_sha256']==sha(mipsdis.EXE.read_bytes())
    loader,_=slot(model,1);old_loader,_=slot(retail,1)
    delta={i for i,(x,y) in enumerate(zip(loader,old_loader)) if x!=y}
    assert delta<=set(range(MENU-BASE,MENU-BASE+8))|set(range(CALLBACK_CALL-BASE,CALLBACK_CALL-BASE+4))
    stores=evaluate_installer(loader,int(proof['metadata_install'],16));base,body=mipsdis.exe_image()
    fields={0x80064940+SCENE:29,0x80064994+ORDINAL:14,0x80064b84+ORDINAL:10,
            0x80060140+ORDINAL:1,0x80064a34+ORDINAL:0,0x80061bb8+ORDINAL:112}
    assert {(addr,value) for addr,width,value in stores if width==1}==set(fields.items())
    assert all(not 0x80064670<=addr<0x80064710 for addr,_,_ in stores),'learned purchase flags modified in full mode'
    assert all(addr not in (0x80019928,0x8001ac20,0x8001a6f8) for addr,_,_ in stores),'standalone restriction leaked'
    assert [(i,body[0x80064994-base+i]) for i in range(29)]==[(i,s) for i,s in enumerate(proof['original_campaign_scene_ids'])]
    print('EXTRA ID AND FULL MODE PASS',digest,'14decimal=0x0e unused in original campaign/dispatch; original29 campaign IDs/progress indices retained; resident code exact; installer has no ability/exit/title restriction writes; cinematic70 row29 restoration declared',flush=True)
    summer,_=slot(model,15);assert sha(summer)==portal['archive_sha256']
    old,_=slot(retail,15);o=u32(old,8);n=u32(old,12);s=u32(summer,8);sn=u32(summer,12)
    original_scene=s2scene_build.split(old[o:o+n]);scene=s2scene_build.split(summer[s:s+sn])
    previous=s2regions.decode(original_scene['parts'][1]);new=s2regions.decode(scene['parts'][1])
    assert new['records'][:160]==previous['records']
    for r in new['records'][160:]:
        assert r.lp_verts==r.hp_verts
        nv,nc,nf=r.header[16:19]
        assert nv+nc+nf*2==r.header[23] and r.header[18]==r.header[22]
        for at in range(0,len(r.lp_faces),8):
            vw,cw=struct.unpack_from('<II',r.lp_faces,at)
            assert all((vw>>shift&127)<nv for shift in (24,17,10,3))
            assert all((cw>>shift&127)<nc for shift in (25,18,11,4))
    assert sum(r.header[22] for r in new['records'])==8183
    print('PORTAL NEAR/FAR STRUCTURE PASS',digest,'8160 retail faces retained +23 new; total8183 within original8191 limit; new far7-bit indices bounded and packed vertices exact near copies; original160 region records exact',flush=True)
    result=dict(image_sha256=digest,retail_sha256=source,original_exact_slots=identical,scene_id=14,scene_id_hex='0x0e',
                original_scene_mappings=proof['original_defined_scene_mappings'],metadata_installer_stores=stores,
                original_executable_sha256=sha(resident),manifest_sha256=sha(a.manifest.read_bytes()),
                portal_sha256=sha(summer),release_eligible=False,
                limits=['Original CPU normal portal/return/save/cinematic/music and native integration gates still required.'])
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('EXPANDED STATIC CHECK PASS',digest,'receipt',a.out.resolve(),flush=True)


if __name__=='__main__':main()

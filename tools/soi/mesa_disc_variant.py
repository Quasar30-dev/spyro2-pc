"""Build a private WAD variant from explicit hash-bound slot replacements.

Keeps the original resident executable and every undeclared WAD payload exact.
This is a development image builder, never a release/launcher promotion.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
import ps1disc
from mesa_disc import find_record


def sha(data):return hashlib.sha256(data).hexdigest()
def align(n):return (n+2047)&~2047


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('manifest',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--name',required=True);ap.add_argument('--cue',type=Path,default=ps1disc.RETAIL_CUE)
    ap.add_argument('--music-report',type=Path,help='Hash-bound appended MESA.XA report to retain through a WAD-only rebuild')
    a=ap.parse_args();assert not a.out.exists();spec=json.loads(a.manifest.read_text())
    replacements={}
    for key,item in spec['replacements'].items():
        slot=int(key);data=Path(item['path']).read_bytes();assert sha(data)==item['sha256']
        assert 0<=slot<255 and data,'slot must be representable in the original 0x800-byte WAD header'
        replacements[slot]=data
    model=ps1disc.DiscModel(a.cue);input_sha=ps1disc.sha256_file(model.bin)
    inherited_music=None
    if a.music_report:
        inherited_music=json.loads(a.music_report.read_text())
        assert inherited_music['sha256']==input_sha and inherited_music['new_file']=='MESA.XA'
        mf=next(f for f in model.files if f.path=='MESA.XA')
        assert mf.lba==inherited_music['new_file_lba'] and mf.size==inherited_music['new_file_sectors']*2048
    print('INPUT DISC',model.bin,'sha256',input_sha,flush=True)
    user,stats=ps1disc.rebuild(model,log=lambda *args:None)
    wad=next(f for f in stats['files'] if f['path']=='WAD.WAD');assert wad['lba']==500
    old=bytes(user[500:500+wad['sectors'],:2048].reshape(-1))[:wad['size']]
    old_toc=stats['wad']['toc'];count=max(len(old_toc),max(replacements)+1)
    new=bytearray(old[:2048]);toc=[];hashes=[]
    for slot in range(count):
        offset,size=old_toc[slot] if slot<len(old_toc) else (0,0)
        data=replacements.get(slot,old[offset:offset+size])
        new.extend(bytes(align(len(new))-len(new)));target=len(new)
        struct.pack_into('<II',new,slot*8,target,len(data));toc.append((target,len(data)))
        new.extend(data);new.extend(bytes(align(len(new))-len(new)))
        hashes.append(sha(data))
    assert count*8+8<=2048
    struct.pack_into('<II',new,count*8,0,0)
    sectors=len(new)//2048;next_file=min(f['lba'] for f in stats['files'] if f['lba']>500)
    assert 500+sectors<=next_file,'WAD growth exceeds the proven empty gap'
    gap=slice(500+wad['sectors'],500+sectors)
    if sectors>wad['sectors']:assert not np.any(model.raw[gap,24:24+2324]),'nonempty post-WAD gap'
    mid=model.sub[501].copy();last=model.sub[500+wad['sectors']-1].copy()
    model.sub[500:500+sectors]=mid;model.sub[500+sectors-1]=last
    model.form2[500:500+sectors]=False;model.edc_zero[500:500+sectors]=False
    user[500:500+sectors,:2048]=np.frombuffer(new,np.uint8).reshape(-1,2048)
    ds,do=find_record(user,b'WAD.WAD;1');directory=bytearray(user[ds,:2048])
    struct.pack_into('<I',directory,do+10,len(new));struct.pack_into('>I',directory,do+14,len(new))
    user[ds,:2048]=np.frombuffer(directory,np.uint8)
    a.out.mkdir(parents=True);image=a.out/f'Spyro2-SoI-MarketMesa-{a.name}.bin';cue=image.with_suffix('.cue')
    digest,changed,first=ps1disc.stream_image(model,user,image)
    cue.write_text(f'FILE "{image.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n',encoding='ascii')
    back=ps1disc.DiscModel(cue);head=bytes(back.raw[500,24:24+2048])
    for slot,(offset,size) in enumerate(toc):
        assert struct.unpack_from('<II',head,slot*8)==(offset,size)
        data=bytes(back.raw[500+offset//2048:500+(offset+size+2047)//2048,24:24+2048].reshape(-1))[:size] if size else b''
        assert sha(data)==hashes[slot],('WAD readback',slot)
    exe=next(f for f in model.files if f.path=='SCUS_944.25')
    resident=bytes(back.raw[exe.lba:exe.lba+(exe.size+2047)//2048,24:24+2048].reshape(-1))[:exe.size]
    original_exe=Path(__file__).resolve().parents[2]/'data/disc/SCUS_944.25'
    assert sha(resident)==sha(original_exe.read_bytes()),'resident executable changed'
    result=dict(image=str(image.resolve()),cue=str(cue.resolve()),sha256=digest,cue_sha256=sha(cue.read_bytes()),
                manifest=str(a.manifest.resolve()),manifest_sha256=sha(a.manifest.read_bytes()),
                input_sha256=input_sha,replaced_slots=sorted(replacements),wad_slots=count,wad_bytes=len(new),
                wad_toc=toc,slot_sha256=hashes,original_executable_sha256=sha(resident),
                original_executable_unchanged=True,undeclared_wad_payloads_exact=True,
                sectors_changed_vs_input=changed,first_changed=first,fixture=False,
                kind=spec.get('kind','development'),release_eligible=False,
                limits=['Development image only; exact-image original runtime and final target gates required.',
                        'All undeclared retail assets remain present; this is not yet a compact single-level ROM.'])
    if inherited_music:
        for key in ('new_file','new_file_lba','new_file_sectors','selected_sector_lbas','xa_sha256','new_music_start','speech_lba'):
            result[key]=inherited_music[key]
        for filename in ('MESA.XA','SPEECH.STR'):
            old_file=next(f for f in model.files if f.path==filename);new_file=next(f for f in back.files if f.path==filename)
            assert old_file==new_file,(filename,'streaming extent moved')
            sectors=(old_file.size+2047)//2048
            assert np.array_equal(model.raw[old_file.lba:old_file.lba+sectors],back.raw[new_file.lba:new_file.lba+sectors]),filename
        result['music_inherited_from_report_sha256']=sha(a.music_report.read_bytes())
        result['original_streaming_files_exact']=True
    (a.out/'disc-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('OUTPUT DISC',image.resolve(),'sha256',digest,flush=True)
    print('DECLARED WAD REPLACEMENTS',json.dumps(sorted(replacements)),flush=True)
    print('VARIANT DISC READBACK PASS',digest,count,'slots independently hash-checked; original resident executable exact; undeclared WAD payloads exact',flush=True)
    if inherited_music:print('INHERITED XA FILES EXACT PASS',digest,'MESA.XA and original SPEECH.STR physical extents/bytes exact against hash-bound input',input_sha,flush=True)
    print('LIMIT',json.dumps(result['limits']),flush=True)


if __name__=='__main__':main()

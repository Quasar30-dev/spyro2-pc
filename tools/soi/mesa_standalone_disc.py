"""Build a physically stripped Mesa-only disc with the original resident EXE.

Only shared WAD0/interface4, the standalone loader, Mesa's overlay and archive have
payloads. Other retail level slots have zero sizes. The ISO contains just boot,
resident, WAD and XA streaming files. Shared speech is retained deliberately so
active original consumers cannot acquire missing voice dependencies.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
import ps1disc
import mesa_standalone_boot as boot


def sha(data):return hashlib.sha256(data).hexdigest()
def align(value):return (value+2047)&~2047
def endian_pair(data,at,value):
    struct.pack_into('<I',data,at,value);struct.pack_into('>I',data,at+4,value)


def pack_wad(parts):
    out=bytearray(2048);toc=[]
    for slot in range(197):
        data=parts.get(slot,b'');offset=len(out)
        struct.pack_into('<II',out,8*slot,offset,len(data));toc.append([offset,len(data)])
        out+=data;out+=bytes(align(len(out))-len(out))
    return out,toc


def root_records(model):
    pvd=bytearray(model.user(16,17)[0,:2048])
    _,lba,size,_,_=ps1disc.Disc.record(pvd,156)
    assert size==2048
    raw=bytes(model.user(lba,lba+1)[0,:2048]);result={};at=0
    while at<len(raw) and raw[at]:
        count,_,_,_,name=ps1disc.Disc.record(raw,at)
        result[name]=bytearray(raw[at:at+count]);at+=count
    return pvd,lba,result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive',type=Path,required=True);ap.add_argument('--overlay',type=Path,required=True)
    ap.add_argument('--audio-report',type=Path,required=True);ap.add_argument('--render-report',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--name',required=True)
    a=ap.parse_args();assert not a.out.exists(),'new private output directory required'
    model=ps1disc.DiscModel(ps1disc.RETAIL_CUE);input_hash=ps1disc.sha256_file(model.bin)
    original_slots={}
    wad_file=next(f for f in model.files if f.path=='WAD.WAD')
    with ps1disc.Disc(model.cue) as disc:
        original_wad=disc.read(wad_file.lba,wad_file.size)
    for slot in (0,1,4):
        offset,size=struct.unpack_from('<II',original_wad,slot*8)
        original_slots[slot]=original_wad[offset:offset+size]
    archive=a.archive.read_bytes();overlay_source=a.overlay.read_bytes()
    overlay=overlay_source+bytes((-len(overlay_source))%2048)
    loader,_,_=boot.build(minimal_menu=True)
    parts={0:original_slots[0],1:loader,4:original_slots[4],16:overlay,17:archive}
    wad,_=pack_wad(parts);speech_lba=500+len(wad)//2048+16
    loader,resident,bootstrap=boot.build(speech_lba=speech_lba,minimal_menu=True)
    parts[1]=loader;wad,toc=pack_wad(parts)
    assert speech_lba==500+len(wad)//2048+16
    codec=json.loads(a.audio_report.read_text());render=json.loads(a.render_report.read_text())
    extent=render['retail_extent'];mix=next(r for r in render['outputs'] if r['enhanced'])
    assert codec['source_sha256']==mix['sha256'] and codec['source_frames']==extent['frames']
    audio=Path(codec['xa']);assert ps1disc.sha256_file(audio)==codec['xa_sha256']
    xa=np.frombuffer(audio.read_bytes(),np.uint8).reshape(-1,2352)
    speech=next(f for f in model.files if f.path=='SPEECH.STR');speech_sectors=(speech.size+2047)//2048
    start=speech.lba+extent['start_sector'];end=start+extent['physical_sectors']
    assert sha(model.raw[start:end].tobytes())==extent['extent_sha256']
    offsets=np.asarray(extent['matched_sector_offsets'],int)
    assert len(xa)==len(offsets)==extent['xa_sectors']
    assert np.array_equal(np.flatnonzero((model.raw[start:end,16]==1)&(model.raw[start:end,17]==extent['channel'])&((model.raw[start:end,18]&0x24)==0x24)),offsets)
    count=speech_lba+speech_sectors
    user=np.zeros((count,2324),np.uint8);sub=np.zeros((count,4),np.uint8)
    form2=np.zeros(count,bool);edc_zero=np.zeros(count,bool)
    # Preserve original boot/license/global resident extents at their LBAs.
    user[:500]=model.user(0,500);sub[:500]=model.sub[:500]
    form2[:500]=model.form2[:500];edc_zero[:500]=model.edc_zero[:500]
    sub[500:speech_lba]=model.sub[501]
    user[500:500+len(wad)//2048,:2048]=np.frombuffer(wad,np.uint8).reshape(-1,2048)
    sub[500+len(wad)//2048-1]=model.sub[500+(wad_file.size+2047)//2048-1]
    sl=slice(speech_lba,count);old=slice(speech.lba,speech.lba+speech_sectors)
    user[sl]=model.user(old.start,old.stop);sub[sl]=model.sub[old]
    form2[sl]=model.form2[old];edc_zero[sl]=model.edc_zero[old]
    selected=speech_lba+extent['start_sector']+offsets
    user[selected]=xa[:,24:24+2324];sub[selected]=xa[:,16:20]
    form2[selected]=True;edc_zero[selected]=False
    pvd,root_lba,records=root_records(model)
    keep=(b'\0',b'\1',b'SCUS_944.25;1',b'SPEECH.STR;1',b'SYSTEM.CNF;1',b'WAD.WAD;1')
    endian_pair(records[b'SPEECH.STR;1'],2,speech_lba)
    endian_pair(records[b'WAD.WAD;1'],10,len(wad))
    root=b''.join(records[name] for name in keep)
    user[root_lba,:2048]=0;user[root_lba,:len(root)]=np.frombuffer(root,np.uint8)
    endian_pair(pvd,80,count);endian_pair(pvd,132,10)
    # A one-directory ISO needs exactly its root path-table record. Both
    # endian copies and both optional tables are rebuilt, never left pointing
    # to the physically removed demo directory.
    for at,endian in ((140,'<'),(144,'<'),(148,'>'),(152,'>')):
        lba=struct.unpack_from(endian+'I',pvd,at)[0]
        if not lba:continue
        assert 16<lba<root_lba
        entry=bytes([1,0])+struct.pack(endian+'IH',root_lba,1)+b'\0\0'
        user[lba,:2048]=0;user[lba,:len(entry)]=np.frombuffer(entry,np.uint8)
    user[16,:2048]=np.frombuffer(pvd,np.uint8)
    model.n=count;model.sub=sub;model.form2=form2;model.edc_zero=edc_zero
    a.out.mkdir(parents=True);image=a.out/f'Spyro2-SoI-MarketMesa-{a.name}.bin';cue=image.with_suffix('.cue')
    digest,changed,_=ps1disc.stream_image(model,user,image)
    cue.write_text(f'FILE "{image.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n',encoding='ascii')
    back=ps1disc.DiscModel(cue)
    assert {f.path for f in back.files}=={'SYSTEM.CNF','SCUS_944.25','WAD.WAD','SPEECH.STR'}
    with ps1disc.Disc(cue) as disc:
        w=disc.read(500,len(wad));exe=next(f for f in back.files if f.path=='SCUS_944.25')
        exe_bytes=disc.read(exe.lba,exe.size)
    for slot,(offset,size) in enumerate(toc):
        assert struct.unpack_from('<II',w,slot*8)==(offset,size)
        assert w[offset:offset+size]==parts.get(slot,b''),('slot identity',slot)
    assert {slot for slot,(_,size) in enumerate(toc) if size}=={0,1,4,16,17}
    assert sha(exe_bytes)==bootstrap['original_executable_sha256']
    selected_back=back.raw[selected].copy();selected_back[:,:16]=xa[:,:16]
    assert sha(selected_back.tobytes())==codec['xa_sha256']
    # Whole raw-sector checksum readback, no second BIN fixture retained.
    encoded,sector_errors,_=ps1disc.stream_image(back,back.user(0,count),None)
    assert encoded==digest and sector_errors==0
    bootstrap['loader']=str((a.out/'slot01-standalone-bootstrap.bin').resolve())
    (a.out/'slot01-standalone-bootstrap.bin').write_bytes(loader)
    (a.out/'resident-restart.bin').write_bytes(resident)
    (a.out/'bootstrap.json').write_text(json.dumps(bootstrap,indent=2)+'\n')
    result=dict(image=str(image.resolve()),cue=str(cue.resolve()),sha256=digest,
                cue_sha256=ps1disc.sha256_file(cue),bytes=image.stat().st_size,sectors=count,
                input_sha256=input_hash,nonempty_wad_slots=[0,1,4,16,17],wad_toc=toc,
                retained_slot_sha256={str(i):sha(data) for i,data in parts.items()},
                archive_sha256=sha(archive),overlay_sha256=sha(overlay),
                overlay_source_sha256=sha(overlay_source),overlay_source_bytes=len(overlay_source),overlay_bytes=len(overlay),
                original_executable_sha256=sha(exe_bytes),original_executable_unchanged=True,
                speech_lba=speech_lba,speech_sectors=speech_sectors,
                xa_sha256=codec['xa_sha256'],music_sectors=selected.tolist(),
                files=[dict(path=f.path,lba=f.lba,size=f.size) for f in back.files],
                removed_level_slots=[i for i in range(197) if i not in parts],
                release_eligible=False,fixture=False,
                limits=['All other level WAD payloads and demos are physically absent.',
                        'Shared speech streaming data retained to preserve original active voice consumers.',
                        'Exact-image cold boot, restart, abilities, exit, content, music and PC checks remain required.'])
    (a.out/'disc-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('STRIPPED DISC',image.resolve(),'sha256',digest,'bytes',image.stat().st_size,flush=True)
    print('RETAINED PAYLOADS',json.dumps(result['retained_slot_sha256']),flush=True)
    print('STRIPPED DISC STRUCTURE PASS',digest,'192 other WAD payloads absent; only shared0/1/interface4 and Mesa16/17 nonempty; four ISO files; resident executable exact; whole-image EDC/ECC reencode exact; relocated speech LBA',speech_lba,flush=True)
    print('APPROVED XA READBACK PASS',digest,codec['xa_sha256'],len(xa),'sectors; original physical cadence retained',flush=True)
    print('LIMIT',json.dumps(result['limits']),flush=True)


if __name__=='__main__':main()

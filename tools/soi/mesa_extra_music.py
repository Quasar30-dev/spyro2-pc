"""Append Mesa's XA as a new ISO file, preserving every retail file extent.

Original PS1 CD streaming uses absolute sectors, independent of ISO file names.
The expanded loader selects the new extent only for Mesa and restores original
track1 metadata on all other scene loads. Glimmer's original music stays exact.
"""
import argparse
import json
import struct
from pathlib import Path
import numpy as np
import ps1disc
from mesa_disc import find_record


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--loader-report',type=Path,required=True);ap.add_argument('--audio-report',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--name',required=True)
    a=ap.parse_args();assert not a.out.exists()
    model=ps1disc.DiscModel(a.cue.resolve());before=ps1disc.sha256_file(model.bin)
    loader=json.loads(a.loader_report.read_text());codec=json.loads(a.audio_report.read_text())
    speech=next(f for f in model.files if f.path=='SPEECH.STR')
    assert loader['new_music_start']==model.n+1-speech.lba
    xa_path=Path(codec['xa']);assert ps1disc.sha256_file(xa_path)==codec['xa_sha256']
    xa=np.frombuffer(xa_path.read_bytes(),np.uint8).reshape(-1,2352)
    assert len(xa)==3806 and codec['source_frames']==7672896
    lead=model.n;count=30449;selected=lead+1+np.arange(len(xa))*8
    old_user,stats=ps1disc.rebuild(model,log=lambda *x:None)
    user=np.concatenate([old_user,np.zeros((count,2324),np.uint8)])
    del old_user
    sub=np.concatenate([model.sub,np.tile(np.array([0,0,0x20,0],np.uint8),(count,1))])
    form2=np.r_[model.form2,np.ones(count,bool)];edc_zero=np.r_[model.edc_zero,np.zeros(count,bool)]
    user[selected]=xa[:,24:24+2324];sub[selected]=xa[:,16:20]
    pvd=bytearray(user[16,:2048]);total=lead+count
    struct.pack_into('<I',pvd,80,total);struct.pack_into('>I',pvd,84,total)
    user[16,:2048]=np.frombuffer(pvd,np.uint8)
    ds,at=find_record(user,b'SPEECH.STR;1');directory=bytearray(user[ds,:2048])
    template=directory[at:at+directory[at]];name=b'MESA.XA;1'
    old_name_end=33+template[32];old_name_end+=(old_name_end&1)
    record=bytearray(template[:33])+name+bytes((33+len(name))&1)+template[old_name_end:]
    assert len(record)%2==0 and len(record)<256
    record[0]=len(record);record[32]=len(name)
    struct.pack_into('<I',record,2,lead);struct.pack_into('>I',record,6,lead)
    struct.pack_into('<I',record,10,count*2048);struct.pack_into('>I',record,14,count*2048)
    end=0
    while directory[end]:end+=directory[end]
    assert end+len(record)<2048
    directory[end:end+len(record)]=record;user[ds,:2048]=np.frombuffer(directory,np.uint8)
    model.n=total;model.sub=sub;model.form2=form2;model.edc_zero=edc_zero
    a.out.mkdir(parents=True);image=a.out/f'Spyro2-SoI-MarketMesa-{a.name}.bin';cue=image.with_suffix('.cue')
    digest,changed,_=ps1disc.stream_image(model,user,image)
    cue.write_text(f'FILE "{image.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n',encoding='ascii')
    back=ps1disc.DiscModel(cue);new_file=next(f for f in back.files if f.path=='MESA.XA')
    assert new_file.lba==lead and new_file.size==count*2048
    delta=[]
    for i in range(0,lead,8192):
        n=min(8192,lead-i);delta.extend((np.flatnonzero(np.any(back.raw[i:i+n]!=model.raw[i:i+n],axis=1))+i).tolist())
    assert set(delta)<={16,ds},delta[:20]
    # This preserves original SPEECH.STR and every retail WAD/data/demo sector
    # exactly relative to the input expanded ROM; only the ISO directory/PVD
    # and the appended new file differ.
    restored=back.raw[selected].copy();restored[:,:16]=xa[:,:16]
    import hashlib
    assert hashlib.sha256(restored.tobytes()).hexdigest()==codec['xa_sha256']
    assert ps1disc.sha256_file(model.bin)==before
    result=dict(image=str(image.resolve()),cue=str(cue.resolve()),sha256=digest,bytes=image.stat().st_size,
                input_sha256=before,new_file='MESA.XA',new_file_lba=lead,new_file_sectors=count,
                selected_sector_lbas=selected.tolist(),xa_sha256=codec['xa_sha256'],
                new_music_start=loader['new_music_start'],speech_lba=speech.lba,
                original_extent_sectors_changed=delta,all_original_files_and_glimmer_score_exact=True,
                original_executable_unchanged=True,release_eligible=False,fixture=False,
                limits=['Exact full-image original Mesa/Glimmer audio and runtime checks required.',
                        'Native extra-scene/XA selection integration remains separate.'])
    (a.out/'disc-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('EXPANDED MUSIC DISC',image.resolve(),'sha256',digest,'bytes',image.stat().st_size,flush=True)
    print('APPENDED XA STRUCTURE PASS',digest,codec['xa_sha256'],'3806 audio sectors in new MESA.XA file; all original file sectors and Glimmer score exact; only existing PVD/root directory sectors changed',flush=True)
    print('LIMIT',json.dumps(result['limits']),flush=True)


if __name__=='__main__':main()

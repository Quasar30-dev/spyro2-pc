"""Apply only the measured Glimmer XA channel to a NEW private Market Mesa disc.

Retains every original executable/table byte, all other XA channels and the
input image. Requires an exact source-extent match and hash-bound codec/render
receipts. This structural step does not claim original-engine playback.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
import ps1disc


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--audio-report',type=Path,required=True);ap.add_argument('--render-report',type=Path,required=True)
    ap.add_argument('--out-cue',type=Path,required=True);ap.add_argument('--report',type=Path,required=True)
    a=ap.parse_args();out=a.out_cue.with_suffix('.bin')
    assert not out.exists() and not a.out_cue.exists(),'refusing to overwrite an existing image'
    codec=json.loads(a.audio_report.read_text());render=json.loads(a.render_report.read_text());extent=render['retail_extent']
    audio=Path(codec['xa']);assert ps1disc.sha256_file(audio)==codec['xa_sha256']
    mix=next(r for r in render['outputs'] if r['enhanced'])
    assert codec['source_sha256']==mix['sha256'] and codec['source_frames']==extent['frames']
    xa=np.frombuffer(audio.read_bytes(),np.uint8).reshape(-1,2352)
    assert len(xa)==extent['xa_sectors'] and codec['padded_frames']==extent['frames']
    assert np.all(xa[:,16:18]==[1,extent['channel']]) and np.all(xa[:,19]==1)
    model=ps1disc.DiscModel(a.cue);before=ps1disc.sha256_file(model.bin)
    speech=next(f for f in model.files if f.path=='SPEECH.STR')
    start=speech.lba+extent['start_sector'];end=start+extent['physical_sectors']
    current=model.raw[start:end]
    assert hashlib.sha256(current.tobytes()).hexdigest()==extent['extent_sha256'],'input soundtrack extent already changed'
    offsets=np.asarray(extent['matched_sector_offsets'],int)
    actual=np.flatnonzero((current[:,16]==1)&(current[:,17]==extent['channel'])&((current[:,18]&0x24)==0x24))
    assert np.array_equal(offsets,actual)
    out.parent.mkdir(parents=True,exist_ok=True)
    with model.bin.open('rb') as src,out.open('xb') as dst:shutil.copyfileobj(src,dst,8<<20)
    target=np.memmap(out,np.uint8,mode='r+').reshape(-1,2352)
    target[start+offsets,16:]=xa[:,16:];target.flush()
    rebuilt=target[start+offsets].copy();rebuilt[:,:16]=xa[:,:16]
    assert hashlib.sha256(rebuilt.tobytes()).hexdigest()==codec['xa_sha256']
    changed=[]
    for i in range(0,model.n,8192):
        delta=np.flatnonzero(np.any(target[i:i+8192]!=model.raw[i:i+8192],axis=1))
        changed.extend((delta+i).tolist())
    assert set(changed).issubset(set((start+offsets).tolist()))
    assert ps1disc.sha256_file(model.bin)==before,'input changed during patch'
    digest=ps1disc.sha256_file(out)
    a.out_cue.write_text(f'FILE "{out.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n',encoding='ascii')
    result={'input_cue':str(a.cue.resolve()),'input_bin':str(model.bin),'input_sha256':before,
            'output_cue':str(a.out_cue.resolve()),'output_bin':str(out.resolve()),'output_sha256':digest,
            'cue_sha256':ps1disc.sha256_file(a.out_cue),'audio_sha256':codec['xa_sha256'],
            'render_report_sha256':ps1disc.sha256_file(a.render_report),'codec_report_sha256':ps1disc.sha256_file(a.audio_report),
            'speech_lba':speech.lba,'track':extent['track'],'channel':extent['channel'],
            'changed_sectors':changed,'xa_sectors':len(xa),'frames':extent['frames'],
            'original_executable_and_all_other_sectors_unchanged':True,
            'limits':['Structural private image only; exact-image PC decode and original PS1 controller/SPU playback still required.',
                      'No manual gameplay/listening acceptance claimed.']}
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2))
    print('INPUT',model.bin,'sha256',before,flush=True)
    print('AUDIO',audio,'sha256',codec['xa_sha256'],flush=True)
    print('OUTPUT',out,'sha256',digest,flush=True)
    print('MUSIC SECTOR PATCH PASS',digest,len(changed),'selected channel sectors changed; every executable/table/other sector byte unchanged; exact XA payload readback',flush=True)
    print('REPORT',a.report.resolve(),'sha256',ps1disc.sha256_file(a.report),flush=True)
    print('LIMIT','; '.join(result['limits']),flush=True)


if __name__=='__main__':main()

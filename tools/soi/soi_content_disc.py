"""Assemble a private content candidate replacing only WAD slots 16 and 17.

No launcher, patcher, score, retail executable or global release report changes.
Every other WAD extent is independently read back and compared with retail.
"""
import argparse
import json
from pathlib import Path
import struct

import numpy as np
import mesa_disc
import soi_model_convert as fmt


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('conversion',type=Path);ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();out=a.out.resolve();assert not out.exists();out.mkdir(parents=True)
    proof=json.loads(a.conversion.read_text())
    replacements={16:Path(proof['overlay']).read_bytes(),17:Path(proof['archive']).read_bytes()}
    assert fmt.sha(replacements[16])==proof['overlay_sha256'] and fmt.sha(replacements[17])==proof['archive_sha256']
    ps=mesa_disc.ps1disc;model=ps.DiscModel(ps.RETAIL_CUE);user,stats=ps.rebuild(model,log=lambda *args:None)
    wad=next(x for x in stats['files'] if x['path']=='WAD.WAD');assert wad['lba']==500
    toc=stats['wad']['toc'];old=bytes(user[500:500+wad['sectors'],:2048].reshape(-1))[:wad['size']]
    added=sum(fmt.align(len(data))-fmt.align(toc[slot][1]) for slot,data in replacements.items())
    assert added>=0
    new=bytearray(wad['size']+added);new[:2048]=old[:2048];shift=0;ntoc=[]
    for slot,(offset,size) in enumerate(toc):
        target=offset+shift if offset else offset
        data=replacements.get(slot,old[offset:offset+size])
        if size:new[target:target+len(data)]=data
        ntoc.append((target,len(data) if size else size))
        if slot in replacements:shift+=fmt.align(len(data))-fmt.align(size)
    for slot,(offset,size) in enumerate(ntoc):struct.pack_into('<II',new,slot*8,offset,size)
    gap_end=min(x['lba'] for x in stats['files'] if x['lba']>500)
    sectors=fmt.align(len(new))//2048;assert 500+sectors<=gap_end
    gap=slice(500+wad['sectors'],500+sectors)
    assert not np.any(model.raw[gap,24:24+2324])
    mid,last=model.sub[501].copy(),model.sub[500+wad['sectors']-1].copy()
    model.sub[500+wad['sectors']-1]=mid;model.sub[gap]=mid;model.sub[500+sectors-1]=last
    model.form2[gap]=False;model.edc_zero[gap]=False
    new.extend(bytes(fmt.align(len(new))-len(new)))
    user[500:500+sectors,:2048]=np.frombuffer(new,np.uint8).reshape(-1,2048)
    ds,do=mesa_disc.find_record(user,b'WAD.WAD;1');sector=bytearray(user[ds,:2048])
    struct.pack_into('<I',sector,do+10,wad['size']+added);struct.pack_into('>I',sector,do+14,wad['size']+added)
    user[ds,:2048]=np.frombuffer(sector,np.uint8)
    image=out/'Spyro2-SoI-MarketMesa-content04-assets-wip.bin';cue=image.with_suffix('.cue')
    digest,differing,first=ps.stream_image(model,user,image)
    cue.write_text(f'FILE "{image.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    back=ps.DiscModel(cue);head=bytes(back.raw[500,24:24+2048]);errors=[]
    for slot,(offset,size) in enumerate(ntoc):
        assert struct.unpack_from('<II',head,slot*8)==(offset,size)
        if not size:continue
        data=bytes(back.raw[500+offset//2048:500+(offset+size+2047)//2048,24:24+2048].reshape(-1))[:size]
        expected=replacements.get(slot,old[toc[slot][0]:toc[slot][0]+toc[slot][1]])
        if data!=expected:errors.append(slot)
    assert not errors
    used=[(o,n) for o,n in ntoc if o or n];assert all(a[0]+a[1]<=b[0] for a,b in zip(used,used[1:]))
    result=dict(image=str(image),sha256=digest,cue=str(cue),cue_sha256=fmt.sha(cue.read_bytes()),
         archive=proof['archive'],archive_sha256=proof['archive_sha256'],overlay=proof['overlay'],overlay_sha256=proof['overlay_sha256'],
         replaced_slots=[16,17],other_slots_exact=True,readback_exact=True,sectors_changed_vs_retail=differing,
         release_eligible=False,fixture=False,music='Parent approved score must be reapplied; not a release')
    (out/'disc-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PRIVATE CONTENT IMAGE',image,'sha256',digest)
    print('SLOT16 sha256',proof['overlay_sha256'],'SLOT17 sha256',proof['archive_sha256'])
    print('CONTENT DISC READBACK PASS',digest,'slots 16/17 exact; every other WAD slot exact retail; monotone TOC; no test fixture')
    print('LIMIT experimental content candidate; parent owns approved score, combined release gates and packaging')


if __name__=='__main__':main()

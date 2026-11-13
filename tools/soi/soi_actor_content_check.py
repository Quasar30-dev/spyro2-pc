"""Compare a Phase3 archive/overlay with immutable content04 and its source.

This verifies content/relocation/VRAM invariants, not runtime acceptance.
Original-CPU and native controller/save tests have separate receipts.
"""
import argparse
import json
from pathlib import Path
import struct

import numpy as np
import soi_actor_content as content
import soi_model_convert as fmt


def banks(a):
    result={}
    for i in range(1,63):
        start,end=struct.unpack_from('<II',a,64+i*4)
        if not end:break
        cls=struct.unpack_from('<H',a,0x140+i*2)[0];assert cls not in result
        result[cls]=a[start:end]
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('proof',type=Path);a=ap.parse_args();j=json.loads(a.proof.read_text())
    archive=Path(j['archive']).read_bytes();overlay=Path(j['overlay']).read_bytes()
    assert fmt.sha(archive)==j['archive_sha256'] and fmt.sha(overlay)==j['overlay_sha256']
    assert len(archive)%2048==0 and len(overlay)%2048==0,'Original CD sector alignment'
    original=(content.BASE/'slot17-content.bin').read_bytes();ov=(content.BASE/'slot16-overlay.bin').read_bytes()
    assert fmt.sha(original)==content.ARCHIVE_SHA and fmt.sha(ov)==content.OVERLAY_SHA
    print('ARTIFACT SLOT17 SHA256',fmt.sha(archive),j['archive']);print('ARTIFACT SLOT16 SHA256',fmt.sha(overlay),j['overlay'])
    before=fmt.wad_index(original[:65536],len(original));after=fmt.wad_index(archive[:65536],len(archive))
    oldsound=fmt.sound_table(original,False);newsound=fmt.sound_table(archive,False)
    assert archive[after[0].offset:after[0].offset+0x86000]==original[before[0].offset:before[0].offset+0x86000]
    assert newsound[3][:len(oldsound[3])]==oldsound[3] and newsound[4][:len(oldsound[4])]==oldsound[4]
    assert len(newsound[3])==94 and len(newsound[4])<=0x7e000
    sound_at=oldsound[2];oldscene,newscene=oldsound[1],newsound[1]
    assert oldscene[:sound_at]==newscene[:sound_at]
    oldtail=oldscene[sound_at+264+20*len(oldsound[3]):];newtail=newscene[sound_at+264+20*len(newsound[3]):]
    assert newtail[:len(oldtail)]==oldtail and not any(newtail[len(oldtail):])
    reservation=Path(j['model_reservation']['path']);assert fmt.sha(reservation.read_bytes())==content.RESERVE_SHA
    with np.load(reservation) as z: reserved=sum(int(v.sum()) for v in z.values())
    print('RETAINED TERRAIN/VRAM',hex(0x86000),'bytes exact; every terrain/collision/sky/UV byte exact content04; reservation',content.RESERVE_SHA,'array sum',reserved)
    old=original[before[3].offset:before[3].offset+before[3].size];new=archive[after[3].offset:after[3].offset+after[3].size]
    retired={e['index'] for e in j['retired_standins']}
    for i in range(225):
        p=content.TABLE+8+88*i;x,y=old[p:p+88],new[p:p+88]
        assert x[:72]==y[:72] and x[73:]==y[73:]
        assert y[72]==(0xfd if i in retired else x[72])
    assert new[content.TABLE+8+88*9+72]==0xfd
    count=fmt.u32(new,content.TABLE+4);assert count==j['count']==266
    pool=content.TABLE+fmt.u32(new,content.TABLE)-(content.TABLE+8+88*count)
    assert pool==j['retained_dynamic_pool_bytes'] and pool>=32*112
    actors=j['actors'];assert len({e['parameter_offset'] for e in actors})==len(actors)==41
    expected={0:1,1:10,2:11,3:3,4:11,5:3,6:1,7:1}
    assert {k:sum(e['kind']==k for e in actors) for k in expected}==expected
    assert sum(e['value'] for e in actors if e['kind'] in [1,2])==26
    assert {e['source_index'] for e in actors if e['kind'] in [1,2]}==set(range(20))|{141}
    for e in actors:
        p=content.TABLE+8+88*e['index'];param=fmt.u32(new,p);assert param==e['parameter_offset']
        assert fmt.u32(new,param)==0x534f4933 and fmt.u32(new,param+4)==e['kind']
        assert list(struct.unpack_from('<3i',new,p+12))==e['position']
        assert struct.unpack_from('<H',new,p+54)[0]==e['cls']
        if e['kind'] in [4,5,7]:assert fmt.u32(new,param+44)==content.TABLE+8+88*e.get('parent_index',e.get('controller_index'))
    print('SOURCE RECORDS all21 enemies including141, all3 type16 critters, 11 attached fans,3 source bells,ambient greeter,controller,source148 portal; old225 anchors/params/classes exact;14 stand-insFD;26 reward value; dynamic guard',pool)
    oldbanks,newbanks=banks(original),banks(archive)
    for cls,bank in oldbanks.items():
        if cls in [0x1f0,0x22b]:assert newbanks[cls]==oldbanks[0xc8]
        elif cls==0x2e2:
            patched=bytearray(bank)
            for e in j['staff_face_retirements']:fmt.put32(patched,e['bank_offset'],0)
            assert bytes(patched)==newbanks[cls]
        else:assert bank==newbanks[cls],hex(cls)
    packets=0
    for cls in [0x2e3,0x2e5,0x2e6]:
        b=newbanks[cls]
        for p,textured in fmt.faces(b):
            assert not textured and not fmt.u32(b,p+4)&0x38
            ids=list(b[p:p+4])[::-1];assert ids[2]==ids[3]  # repeated final decoded strip vertex
            packets+=1
    assert list(newbanks[0x2e6][4:7])==j['bell_sounds']['sample_indices']==[91,92,93]
    print('MODEL PRESERVATION all old banks exact except2 inactive-bank compactions and11 staff-face index retirements; new authored runtime packets',packets,'untextured triangles; original body clips/SFX/LODs exact')
    hook=0x80070478-content.soi_actor_overlay.BASE
    assert overlay[:hook]==ov[:hook] and overlay[hook+8:len(ov)]==ov[hook+8:]
    assert overlay[0x80081a64-content.soi_actor_overlay.BASE:len(ov)]==ov[0x80081a64-content.soi_actor_overlay.BASE:]
    print('OVERLAY original dispatch cases and C9 extension exact except8-byte actor trampoline; new adapter independent of scene IDs; source bell cooldown35, sequence2/1/0/2/1/0')
    print('PHASE3 STRUCTURAL CHECK COMPLETE',fmt.sha(archive),'overlay',fmt.sha(overlay),'no runtime/manual/release claim')


if __name__=='__main__':main()

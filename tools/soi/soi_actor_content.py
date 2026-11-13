"""Append source actors to immutable content04; never rebuild terrain/models.

Creates a new slot16/17 pair. All old record positions/classes/parameter
pointers survive; fourteen stand-ins are retired in place. New params have
their own relocations and are never borrowed from a native enemy.
"""
import argparse
import json
import math
from pathlib import Path
import struct

import numpy as np
import soi_model_convert as fmt
import soi_actor_overlay
from mesa_container_check import floor_query

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'data/soi/content-20261105/phase2-containers/content04'
ARCHIVE_SHA='a39b76c1b726e5d2ea6a2ee80f4da337b5e510d08e6bc6bae9586b7e330d6bc0'
OVERLAY_SHA='c3e06f3b15d36fd557826f8b899767585be0e1187a79c6968058f6c9b1839534'
SOURCE_SHA='4f372fcb202e333b9f6d2339c2a2dce763337e2c459c6b43de0a5f13f1e96f5f'
RESERVE_SHA='f72383eaccf1483e7267528a2906f08160d9613e687a8b2eb6aba59289342487'
TABLE=0x327c


def repack(old,gameplay,extra_banks=None,compact=None):
    ch=fmt.wad_index(old[:65536],len(old));head=bytearray(old[:ch[0].offset])
    first=fmt.u32(old,68);model=bytearray(old[ch[2].offset:first]);rel={};saved=[]
    for i in range(1,63):
        start,end=struct.unpack_from('<II',old,64+i*4)
        if not end:
            terminal=start-ch[2].offset
            break
        rel[i]=len(model);cls=struct.unpack_from('<H',old,0x140+i*2)[0]
        original=old[start:end];bank=(compact or {}).get(cls,original);model.extend(bank)
        if bank!=original:saved.append(dict(cls=cls,old_sha256=fmt.sha(original),old_bytes=len(original),new_sha256=fmt.sha(bank),new_bytes=len(bank)))
    if extra_banks:
        for cls,bank in extra_banks:
            i=len(rel)+1;rel[i]=len(model);struct.pack_into('<H',head,0x140+i*2,cls);model.extend(bank)
        rel[len(rel)+1]=len(model)
    else:rel[len(rel)+1]=len(model)
    children=[old[ch[0].offset:ch[0].offset+ch[0].size],old[ch[1].offset:ch[1].offset+ch[1].size],bytes(model),bytes(gameplay)]
    output=bytearray(head);offsets=[]
    for i,data in enumerate(children):
        output.extend(bytes((-len(output))%2048));offsets.append(len(output))
        struct.pack_into('<II',output,i*8,len(output),fmt.align(len(data)))
        output.extend(data);output.extend(bytes((-len(output))%2048))
    fmt.put32(output,64,fmt.u32(old,64)-ch[2].offset+offsets[2])
    for i,offset in rel.items():fmt.put32(output,64+i*4,offsets[2]+offset)
    fmt.put32(output,64+(max(rel)+1)*4,0)
    return bytes(output),offsets,[fmt.align(len(x)) for x in children],saved


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--critter-bank',type=Path)
    ap.add_argument('--fan-bank',type=Path)
    ap.add_argument('--bell-bank',type=Path)
    ap.add_argument('--bell-sounds',type=Path)
    a=ap.parse_args();out=a.out.resolve();assert not out.exists()
    old=(BASE/'slot17-content.bin').read_bytes();overlay=(BASE/'slot16-overlay.bin').read_bytes()
    source=(ROOT/'data/soi/content-20261105/source-manifest.json').read_bytes()
    assert fmt.sha(old)==ARCHIVE_SHA and fmt.sha(overlay)==OVERLAY_SHA and fmt.sha(source)==SOURCE_SHA
    build=json.loads((BASE/'mesa-ps1-report.json').read_text());manifest=json.loads(source)
    ch=fmt.wad_index(old[:65536],len(old));g=bytearray(old[ch[3].offset:ch[3].offset+ch[3].size])
    size,count=struct.unpack_from('<II',g,TABLE);assert count==225
    rec=lambda i:TABLE+8+88*i
    prior=bytes(g);end=TABLE+size;psize=fmt.u32(g,end);insert=end+psize;assert struct.unpack_from('<II',g,insert)==(8,0)
    rel=insert+8;nr=fmt.u32(g,rel);refs=list(struct.unpack_from('<'+'I'*nr,g,rel+4));assert not any(g[rel+4+nr*4:])
    floor=floor_query(old);off=np.array(build['frame_offset'])
    def position(e):
        x,y,z=e['pos'];return [int(x*math.sqrt(6)/24*1024+off[0]),int(-y*math.sqrt(6)/24*1024+off[1]),int(z/24*1024+off[2])]
    enemies=[e for e in manifest['market_mesa_actors'] if e['family'].startswith('enemy')];assert len(enemies)==21
    standins=[]
    for i in range(count):
        p=rec(i);cls=struct.unpack_from('<H',g,p+54)[0]
        if cls in (0x1f0,0x22b) and g[p+72]<128:
            pos=struct.unpack_from('<3i',g,p+12)
            if pos!=(2048,2048,512):standins.append(dict(index=i,cls=cls,position=list(pos)))
    assert len(standins)==14
    actors=[]
    for e in enemies:
        pos=position(e);matches=[s for s in standins if np.linalg.norm(np.array(s['position'][:2])-pos[:2])<=2]
        assert len(matches)<=1
        if matches:pos=matches[0]['position']
        # Source z is retained for missing placements. Existing source anchors
        # retain the exact terrain-support adjustment already authored in v2g.
        actors.append(dict(source_index=e['index'],source_type=e['type'],source_param=e['param'],
                           kind=1 if e['family']=='enemy_180' else 2,cls=0x2e1 if e['family']=='enemy_180' else 0x2e2,
                           position=pos,value=e['drop_gem_value'],old_index=matches[0]['index'] if matches else None))
    assert {x['old_index'] for x in actors if x['old_index'] is not None}=={s['index'] for s in standins}
    # Author a supported habitat rectangle around each exact source anchor.
    # Every sampled footprint shares that terrace's collision height; the MIPS
    # approach/lunge never crosses this rectangle, including at island edges.
    for e in actors:
        x,y,z=e['position'];axis=list(range(-384,385,96));safe=np.zeros((9,9),bool)
        for yy,dy in enumerate(axis):
            for xx,dx in enumerate(axis):
                points=[floor(x+dx+px,y+dy+py,z) for px,py in [(0,0),(192,0),(-192,0),(0,192),(0,-192)]]
                safe[yy,xx]=all(v is not None and abs(v-z)<=128 for v in points)
        choices=[]
        if safe[4,4]:
            for x0 in range(5):
                for x1 in range(4,9):
                    for y0 in range(5):
                        for y1 in range(4,9):
                            if safe[y0:y1+1,x0:x1+1].all():choices.append(((x1-x0)*(y1-y0),x0,x1,y0,y1))
        if choices:
            _,x0,x1,y0,y1=max(choices);bounds=[x+axis[x0],x+axis[x1],y+axis[y0],y+axis[y1]]
        else:bounds=[x,x,y,y]
        e['movement_bounds']=bounds;e['movement_support']=dict(grid_step=96,footprint_radius=192,max_height_difference=128,anchor_supported=bool(safe[4,4]))
    for s in standins:g[rec(s['index'])+72]=0xfd
    if a.critter_bank:
        for e in manifest['market_mesa_actors']:
            if e['family']=='critter':actors.append(dict(source_index=e['index'],source_type=16,source_param=e['param'],kind=3,cls=0x2e3,position=position(e),value=255,old_index=None))
    # Authored, terrain-supported ambient greeter; this is not a SoI story spawn.
    start=np.array(build['spawn']);occupied=[]
    for i in range(count):
        p=rec(i)
        if g[p+72]<128:occupied.append(np.array(struct.unpack_from('<3i',g,p+12)))
    occupied.extend(np.array(x['position']) for x in actors)
    candidates=[]
    for radius in (1800,2400,3000,3600):
        for k in range(16):
            xy=start[:2]+radius*np.array([math.cos(k*math.pi/8),math.sin(k*math.pi/8)])
            z=floor(*xy,start[2]-64)
            if z is None:continue
            supports=[floor(xy[0]+dx,xy[1]+dy,z) for dx,dy in [(0,0),(512,0),(-512,0),(0,512),(0,-512)]]
            if any(v is None or abs(v-z)>64 for v in supports):continue
            clearance=min(np.linalg.norm(p[:2]-xy) for p in occupied if abs(p[2]-z)<1800)
            if clearance>=1000:candidates.append((clearance-radius*.15,[int(xy[0]),int(xy[1]),int(z)]))
    assert candidates,'No clear supported ambient entrance point'
    greeting=max(candidates)[1]
    actors.append(dict(source_index=None,source_type=None,source_param=None,kind=0,cls=0x2e0,position=greeting,value=255,old_index=None,
                       authorship='Ambient entrance greeter; terrain-supported authored point; no dialogue, quest, or original SoI story claim.'))
    if a.fan_bank:
        for parent_j,e in list(enumerate(actors)):
            if e['kind']==2:actors.append(dict(source_index=e['source_index'],source_type=e['source_type'],source_param=e['source_param'],kind=4,cls=0x2e5,
                position=e['position'],value=255,old_index=None,parent_index=count+parent_j,authorship='Blender-authored closed cyan fan; copies parent clip/frame/transform; old staff near faces retired'))
    quest=None
    if a.bell_bank:
        from gba import Rom,ROM_SHA256
        rom=Rom();assert fmt.sha(rom.data)==ROM_SHA256
        literal=rom.u32(0x08029f88);sequence=list(rom.bytes(0x08063d64,6));assert sequence==[2,1,0,2,1,0]
        controller_index=count+len(actors)+3;bell_indices=[]
        for k in range(3):
            raw=struct.unpack('<3i',rom.bytes(literal+k*12,12));pos=position(dict(pos=[v/1024 for v in raw]));bell_indices.append(count+len(actors))
            actors.append(dict(source_index=None,source_type='source quest bell',source_param=k,kind=5,cls=0x2e6,position=pos,value=255,old_index=None,controller_index=controller_index,bell_ordinal=k,source_raw=list(raw)))
        actors.append(dict(source_index=None,source_type='Mesa quest controller',source_param=None,kind=6,cls=0x2e7,position=build['spawn'],value=255,old_index=None))
        portal=next(e for e in manifest['market_mesa_actors'] if e['index']==148);assert portal['type']==41
        portal_index=count+len(actors);actors.append(dict(source_index=148,source_type=41,source_param=portal['param'],kind=7,cls=0x2e4,position=position(portal),value=255,old_index=None,controller_index=controller_index))
        quest=dict(source_rom_sha256=ROM_SHA256,controller_index=controller_index,bell_indices=bell_indices,portal_index=portal_index,sequence=sequence,source_positions_address=hex(literal),
                   source_sequence_address='08063d64',source_portal=dict(index=148,type=41,destination_soi=2),
                   gate='Source bell sequence opens the source-position portal visual; completion word registered with resident persistence API',
                   travel_binding='Parent owns final full-game/standalone portal routing; no target scene or level ID is invented in this overlay.')
    new_count=count+len(actors);reserve=end-rec(new_count);assert reserve>=32*112
    assert not any(g[rec(count):rec(new_count)])
    added=96*len(actors);g[insert:insert]=bytes(added);fmt.put32(g,end,psize+added)
    newrefs=[]
    for field in refs:
        shifted=field+added if field>=insert else field;value=fmt.u32(g,shifted)
        if value>=insert:fmt.put32(g,shifted,value+added)
        newrefs.append(shifted)
    for j,e in enumerate(actors):
        e['index']=count+j;p=rec(e['index']);param=insert+96*j;e['parameter_offset']=param
        record=bytearray(88);struct.pack_into('<I',record,0,param);struct.pack_into('<3i',record,12,*e['position'])
        struct.pack_into('<hH',record,52,-1,e['cls']);record[58]=255;record[63]=1;record[64]=0x30
        record[67]=255  # independent active-list entry, not retail group zero
        record[72]=0;record[74]=255 if e['kind'] in [3,6] else 16;record[76]=0 if e['kind'] in [6,7] else 32;record[79]=32;record[80]=e['value'];record[81]=255;record[84]=127
        if e['kind'] in [5,6]:record[63]=0;record[64]=0
        g[p:p+88]=record;newrefs.append(p)
        struct.pack_into('<II3i4I',g,param,0x534f4933,e['kind'],*e['position'],0,0,0,e['value'])
        fmt.put32(g,param+40,e['source_index'] if e['source_index'] is not None else 0xffffffff)
        if e['kind']==4:
            fmt.put32(g,param+44,rec(e['parent_index']));newrefs.append(param+44)
        if e['kind']==6:fmt.put32(g,param+32,0)
        if e['kind'] in [5,7]:
            fmt.put32(g,param+44,rec(e['controller_index']));newrefs.append(param+44)
        if e['kind']==5:fmt.put32(g,param+36,e['bell_ordinal'])
        if e['kind'] in [1,2]:struct.pack_into('<4i',g,param+48,*e['movement_bounds'])
        # Passive body cylinder; original query creates hit mailboxes. No
        # damaging donor collision bank survives outside the swing envelope.
        struct.pack_into('<IIhhIII',g,param+64,64,0x02000301,0,1536,0,512,0)
        if e['kind']==3:struct.pack_into('<IIhhIII',g,param+64,48,0x01400301,0,1408,0,320,0)
    fmt.put32(g,TABLE+4,new_count);rel+=added;fmt.put32(g,rel,len(newrefs));struct.pack_into('<'+'I'*len(newrefs),g,rel+4,*newrefs)
    # Old records retain positions/classes/parameter pointers, including all C9s.
    for i in range(count):
        p=rec(i);before=prior[p:p+88];after=g[p:p+88]
        assert before[:72]==after[:72] and before[73:]==after[73:]
        assert before[72]==after[72] or i in {s['index'] for s in standins}
    extra=[(0x2e3,a.critter_bank.read_bytes())] if a.critter_bank else None
    staff_changes=[]
    if a.fan_bank:
        extra=(extra or [])+[(0x2e5,a.fan_bank.read_bytes())]
        # Keep every body clip, compressed pose, sound and palette byte. Only
        # retire the identified original untextured staff polygons; the fan
        # is a separate authored mesh bound to those exact source poses.
        patched=bytearray(old)
        for i in range(1,63):
            begin,finish=struct.unpack_from('<II',old,64+i*4)
            if not finish:break
            if struct.unpack_from('<H',old,0x140+i*2)[0]!=0x2e2:continue
            bank=old[begin:finish];common=fmt.u32(bank,56);weapon={0,3,*range(133,143)};seen=set()
            for clip in range(fmt.u32(bank,0)):
                d=fmt.u32(bank,60+clip*4);at=common+fmt.u32(bank,d+16)
                if at in seen:continue
                seen.add(at);p=at+4;limit=at+4+fmt.u32(bank,at)
                while p<limit:
                    flags=fmt.u32(bank,p+4);ids=list(bank[p:p+4])[::-1];n=20 if flags&0x80000000 else 8
                    if set(ids)<=weapon:
                        assert n==8;staff_changes.append(dict(bank_offset=p,old_indices=ids));fmt.put32(patched,begin+p,0)
                    p+=n
            assert len(staff_changes)==11,staff_changes
        old=bytes(patched)
    if a.bell_bank:
        original_banks={}
        for i in range(1,63):
            begin,finish=struct.unpack_from('<II',old,64+i*4)
            if not finish:break
            original_banks[struct.unpack_from('<H',old,0x140+i*2)[0]]=old[begin:finish]
        extra=(extra or [])+[(0x2e6,a.bell_bank.read_bytes()),(0x2e7,original_banks[0x1e2]),(0x2e4,original_banks[0x110])]
    # These two banks became unused only after replacing all fourteen stand-ins.
    # Keep a valid retained static cylinder bank: the native decoder still
    # decodes 1F0/22B's cylinder even when every record is retired.
    # This recovers original PS1 heap/GPU room without changing any source actor.
    banks={}
    for i in range(1,63):
        begin,finish=struct.unpack_from('<II',old,64+i*4)
        if not finish:break
        banks[struct.unpack_from('<H',old,0x140+i*2)[0]]=old[begin:finish]
    for cls in [0x1f0,0x22b]:
        assert all(g[rec(i)+72]>=128 for i in range(count) if struct.unpack_from('<H',g,rec(i)+54)[0]==cls)
    compact={cls:banks[0xc8] for cls in [0x1f0,0x22b]}
    archive,offsets,sizes,saved=repack(old,g,extra,compact)
    bell_sounds=None
    if a.bell_sounds:
        assert a.bell_bank
        from soi_quest_sound import install
        archive,offsets,sizes,bell_sounds=install(archive,a.bell_sounds)
    new_overlay,extension=soi_actor_overlay.build(overlay)
    out.mkdir(parents=True);arcpath=out/'slot17-content.bin';ovpath=out/'slot16-overlay.bin';arcpath.write_bytes(archive);ovpath.write_bytes(new_overlay)
    reservation=ROOT/'data/soi/content-20261105/phase2-models/converted03/model-vram-reservation.npz'
    assert fmt.sha(reservation.read_bytes())==RESERVE_SHA
    proof=dict(kind='Phase 3 source actors, private development candidate',archive=str(arcpath),archive_sha256=fmt.sha(archive),overlay=str(ovpath),overlay_sha256=fmt.sha(new_overlay),
               base_archive_sha256=ARCHIVE_SHA,base_overlay_sha256=OVERLAY_SHA,original_count=count,count=new_count,actors=actors,retired_standins=standins,
               retained_dynamic_pool_bytes=reserve,minimum_dynamic_pool_bytes=32*112,enemy_gem_total=sum(e['value'] for e in actors if e['kind'] in (1,2)),
               extension=extension,quest=quest,bell_sounds=bell_sounds,retired_standin_bank_compaction=saved,staff_face_retirements=staff_changes,model_reservation=dict(path=str(reservation),sha256=RESERVE_SHA),release_eligible=False,
               limits=['Authored SoI behavior adapters; original engine runtime validation required.',
                       'Source critters not installed.' if not a.critter_bank else 'Authored critter model and health release require original/native verification.',
                       'Staff/fan silhouette and source quest gate still unfinished.',
                       'No approved music, parent stone composition, release or packaging.'])
    (out/'actor-conversion.json').write_text(json.dumps(proof,indent=2)+'\n')
    build.update(archive=str(arcpath),archive_sha256=fmt.sha(archive),actor_conversion=str(out/'actor-conversion.json'),actor_records=actors,release_eligible=False)
    build['objects']['count']=new_count;build['archive_layout'].update(offsets=offsets,sizes=sizes,total=len(archive))
    (out/'mesa-ps1-report.json').write_text(json.dumps(build,indent=1)+'\n')
    print('ARTIFACT SLOT17 SHA256',fmt.sha(archive),arcpath);print('ARTIFACT SLOT16 SHA256',fmt.sha(new_overlay),ovpath)
    print('INSTALLED',len(enemies),'source enemies; retired',len(standins),'stand-ins; appended',len(actors),'actors; dynamic reserve',reserve,'bytes')
    print('AUTHORED AMBIENT BIANCA',greeting,'source spawn',build['spawn'],'no story/dialogue integration')
    print('LIMIT',json.dumps(proof['limits']))


if __name__=='__main__':main()

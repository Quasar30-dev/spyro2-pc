"""Build the private US retail PSX Boss Training Ground WAD replacement."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/soi')]
import mesa_ps1 as terrain
import mesa_vram
import mesa_visibility
import mesa_standalone_boot
import s2collision
import s2regions
import s2scene_build
from s2scene import read_archive
from soi_actor_overlay import Asm, BASE, REG
from soi_model_convert import Pack, convert_materials, sound_table, sample_bytes, u32, put32, align
from spyrodisc import wad_index

DISC = ROOT / 'data/disc'
WORLD = ROOT / 'data/custom-levels/boss-training/boss-training.s2world'
SPAWN = (24576, 20480, 8192)
ACTORS = (0x8f, 0x191, 0x2af)
PROJECTILES = (0x288, 0x196, 0x2c2)
BODY_RECORD = 0
SHOT_RECORD = 1
HOOK = 0x800703dc

def point(x, y, z):
    return np.rint(np.array([x,z,y])*1024/16).astype(int)*16 + SPAWN

def world():
    lines = WORLD.read_text().splitlines()
    assert lines[0] == 'S2WORLD 1'
    count = int(lines[6])
    triangles = []
    for line in lines[7:7+count]:
        flags, material, *values = map(float, line.split())
        vs = np.reshape(values,(3,8))
        triangles.append((int(flags), np.array([point(*v[:3]) for v in vs]), vs[0,3:6]))
    actors = [list(map(float,row.split())) for row in lines[8+count:8+count+int(lines[7+count])]]
    return triangles, actors

def bank_at(archive, cls):
    for i in range(1, 63):
        start, end = struct.unpack_from('<II', archive, 64+i*4)
        if not end:
            break
        if struct.unpack_from('<H',archive,0x140+i*2)[0] == cls:
            return archive[start:end]
    raise ValueError(hex(cls))

def scene(child, triangles):
    data = s2scene_build.split(child)
    # One original material; all visible distinctions come from vertex light.
    material = data['parts'][0][8:56]
    data['parts'][0] = struct.pack('<II',56,1) + material
    records = []
    face_number = 1
    for flags, vertices, color in triangles:
        pieces = [piece*16 for piece in terrain.split_tri(vertices//16)]
        for first in range(0,len(pieces),12):
            group=pieces[first:first+12]
            all_vertices=np.concatenate(group)
            origin=all_vertices.min(axis=0)
            origin[2]&=~31
            center=(origin+all_vertices.max(axis=0))//2
            if max(all_vertices.max(axis=0)-origin)>=32000 and len(group)>1:
                # Long perimeter strips need smaller local origins.
                groups=[[part] for part in group]
            else:
                groups=[group]
            for group in groups:
                all_vertices=np.concatenate(group)
                origin=all_vertices.min(axis=0);origin[2]&=~31
                center=(origin+all_vertices.max(axis=0))//2
                radius=int(np.linalg.norm(all_vertices-center,axis=1).max())+32
                header=struct.pack('<8H',center[1]//16,center[0]//16,radius//16|0x2000,center[2]//16,
                                   origin[1]//16,origin[0]//16,face_number,origin[2]//16)
                header+=bytes((0,0,0,0,len(all_vertices),1,len(group),0))+struct.pack('<I',0xffffffff)
                verts=b''.join(struct.pack('<I',terrain.pack_vertex(v,origin)) for v in all_vertices)
                light=terrain.rgbx(color*200)
                faces=b''
                for j in range(0,len(all_vertices),3):
                    faces+=bytes((j+2,j+2,j+1,j,0,0,0,0))+struct.pack('<II',0,0x800)
                records.append(s2regions.Record(header,b'',b'',b'',verts,light+light,faces))
                face_number+=len(group)
    data['parts'][1] = s2regions.encode(b'',records)
    data['parts'][2] = mesa_visibility.encode(len(records))
    tris = b''.join(terrain.pack_tri(piece) for flags,vertices,_ in triangles if flags&2
                    for piece in terrain.split_tri(vertices[[0,2,1]]//16))
    data['collision'] = s2collision.encode(tris,b'',b'',terrain.cells_exact(tris))
    return s2scene_build.join(data),material

def gameplay(child, actors):
    data = bytearray(child)
    struct.pack_into('<iii',data,0,*SPAWN)
    data[0x0e] = 64
    at = 0x2c
    for i in range(8):
        size = u32(data,at)
        if i<7:
            data[at+4:at+size] = bytes(size-4)
        at += size
    count = u32(data,at+4)
    records = [at+8+i*88 for i in range(count)]
    baskets = [bytes(data[p:p+88]) for p in records if struct.unpack_from('<H',data,p+54)[0]==0xc8]
    for p in records:
        data[p+72] = 0xfd
    state_offset = len(data)
    data.extend(bytes(32))
    runtime = [0x8f,0x288]
    for i, cls in enumerate(runtime):
        p = records[i]
        data[p:p+88] = bytes(88)
        put32(data,p,state_offset)
        struct.pack_into('<iii',data,p+12,*SPAWN)
        struct.pack_into('<H',data,p+54,cls)
        data[p+59]=255
        data[p+70]=64
        data[p+72]=0
        data[p+76]=0
        data[p+79]=32
        data[p+80]=255
        data[p+82]=255
    for p,actor,donor in zip(records[2:],(a for a in actors if a[0]==200),baskets):
        _,_,x,y,z,yaw,_ = actor
        data[p:p+88]=donor
        struct.pack_into('<iii',data,p+12,*point(x,y,z))
        data[p+70]=round(64-yaw*128/np.pi)&255
    return data

def boss_banks(base, safe, material):
    _,_,_,descriptors,samples=sound_table(base,False)
    sounds=bytearray(samples)
    material_use=mesa_vram.Ownership()
    for at in range(0,48,8):
        u,v,low,high,u1,v1,page,_=material[at:at+8]
        clut=low|high<<8
        material_use.texture('material',page,clut,[(u,v),(u1,v1)],False)
        material_use.palette('material',clut,256 if page&128 else 16,1 if at>=16 else 16)
    pixels=np.frombuffer(base,'<u2',512*512,u32(base,0)).reshape(512,512).copy()
    pack=Pack(safe&~material_use.masks['material'],pixels)
    result={}
    for slot,cls in zip((31,55,71,31,55,71),ACTORS+PROJECTILES):
        arc=read_archive(DISC,slot)
        bank=bytearray(bank_at(arc,cls))
        _,_,_,donor_descriptors,donor_samples=sound_table(arc,False)
        for i in range(4,16):
            old=bank[i]
            bank[i]=255
            if cls not in ACTORS or i not in (4,5):
                continue
            if old==255:
                continue
            source_descriptor=donor_descriptors[old]
            if len(descriptors)>=255:
                bank[i]=255
                continue
            descriptor=bytearray(source_descriptor)
            put32(descriptor,0,len(sounds))
            waveform=sample_bytes(donor_samples,u32(source_descriptor,0))
            bank[i]=len(descriptors)
            descriptors.append(bytes(descriptor));sounds.extend(waveform)
        # Ripto's unused clip 12 has patch-indexed face variants. Keep the base
        # mesh for that clip in this build so its textures share the same packing.
        if cls==0x2af:
            desc=u32(bank,60+12*4)
            for i in range(bank[desc]):
                offset=desc+36+i*8
                put32(bank,offset,u32(bank,offset)|0x80000000)
        source=np.frombuffer(arc,'<u2',512*512,u32(arc,0)).reshape(512,512)
        result[cls]=convert_materials(bank,source,pack,hex(cls))[0]
    return pixels,result,descriptors,sounds

def archive():
    triangles, actors = world()
    base, _, _, safe = mesa_vram.inventory()
    children=wad_index(base,len(base))
    payloads=[base[e.offset:e.offset+e.size] for e in children[:4]]
    payloads[1],material=scene(payloads[1],triangles)
    payloads[3]=gameplay(payloads[3],actors)
    pixels,banks,descriptors,samples=boss_banks(base,safe,material)
    old_scene=payloads[1]
    at=0
    for _ in range(6): at+=u32(old_scene,at)
    at+=12
    for _ in range(5): at+=u32(old_scene,at)
    at+=32
    old_size=u32(old_scene,at)
    prefix=bytearray(old_scene[at:at+264])
    put32(prefix,0,264+20*len(descriptors));put32(prefix,260,len(descriptors))
    payloads[1]=old_scene[:at]+prefix+b''.join(descriptors)+old_scene[at+old_size:]
    payloads[0]=pixels.tobytes()+payloads[0][0x80000:0x86000]+samples
    assert len(samples)<=0x7e000
    # Retain the original model prefix and append the six donor banks.
    model=bytearray(payloads[2])
    head=bytearray(base[:children[0].offset])
    last=next(i for i in range(1,63) if not u32(head,68+i*4))
    original_offsets=[u32(head,64+i*4)-children[2].offset for i in range(last)]
    for i,(cls,bank) in enumerate(banks.items(),last):
        model.extend(bytes((-len(model))%4))
        original_offsets.append(len(model))
        struct.pack_into('<H',head,0x140+i*2,cls)
        model.extend(bank)
    original_offsets.append(len(model))
    payloads[2]=model
    output=head
    for i,payload in enumerate(payloads):
        off=len(output)
        struct.pack_into('<II',output,8*i,off,align(len(payload)))
        output.extend(payload)
        output.extend(bytes((-len(output))%2048))
        if i==2:
            for j,relative in enumerate(original_offsets):
                put32(output,64+4*j,off+relative)
            put32(output,64+4*len(original_offsets),0)
    assert len(output)<=2181120, len(output)
    return output

def sll(a,rd,rt,shift):
    a.emit(REG[rt]<<16|REG[rd]<<11|shift<<6)

def table(a,reg,operand,labels):
    for index,label in enumerate(labels,1):
        a.li(reg,index)
        a.beq(operand,reg,label)

def build_overlay(original):
    assert u32(original,HOOK-BASE)==0x0c0103a5
    scratch = align(len(original),16)
    output=bytearray(original)+bytes(scratch-len(original))
    a=Asm(BASE+scratch)
    start=a.pc
    for value in [0]*10: a.emit(value) # guise, attack age, attack, shot life, velocity XYZ, cooldown, probe
    entry=a.pc
    a.addiu('sp','sp',-64)
    for i,r in enumerate(('ra','s0','s1','s2','s3','s4','s5','s6')):
        a.sw(r,60-4*i,'sp')
    a.li('s0',start)
    a.li('t0',0x80066f14);a.lw('s1',0,'t0')
    a.addiu('s6','s1',88)
    a.li('s2',0x80069ff0)
    a.lw('t0',32,'s0');a.bne('t0','zero','spawned')
    a.li('t0',1);a.sw('t0',32,'s0')
    a.li('t1',0x8006a11c)
    for i,coordinate in enumerate(SPAWN):
        a.li('t0',coordinate+(356 if i==2 else 0));a.sw('t0',4*i,'s2');a.sw('t0',4*i,'t1')
    a.li('t0',1024);a.sw('t0',0x6c,'s2')
    a.label('spawned')
    a.lw('s3',0,'s0')
    a.li('t0',0x800683a4);a.lw('s4',0,'t0')
    a.imm(12,'t0','s4',0x100)
    a.beq('t0','zero','no_switch')
    # Select cycles only near the marked spawn pad.
    for off,coord in ((0,SPAWN[0]),(4,SPAWN[1])):
        a.lw('t0',off,'s2');a.li('t1',coord-2457);a.subu('t0','t0','t1')
        a.imm(11,'t0','t0',4914);a.beq('t0','zero','no_switch')
    a.addiu('s3','s3',1);a.imm(12,'s3','s3',3);a.sw('s3',0,'s0')
    a.sw('zero',4,'s0');a.sw('zero',8,'s0');a.sw('zero',12,'s0')
    a.label('no_switch')
    a.beq('s3','zero','spyro')
    a.li('t0',0x8006a250);a.li('t1',1);a.sw('t1',0,'t0');a.sw('t1',28,'s0')
    a.li('t1',0x40);a.sb('t1',76,'s1')
    for off in range(0,12,4):
        a.lw('t0',off,'s2')
        if off==8:a.addiu('t0','t0',-356)
        a.sw('t0',12+off,'s1')
    a.lw('t0',0x6c,'s2');a.shift('t0','t0',4);a.sb('t0',70,'s1')
    a.lw('s5',8,'s0')
    a.bne('s5','zero','attack')
    a.imm(12,'t0','s4',0x20);a.bne('t0','zero','ranged')
    a.imm(12,'t0','s4',0x10);a.beq('t0','zero','walk')
    a.li('s5',1);a.jump('arm')
    a.label('ranged');a.li('s5',2)
    a.label('arm');a.sw('s5',8,'s0');a.sw('zero',4,'s0')
    a.move('a0','s1');a.move('a1','zero');a.jal(0x80050648)

    a.label('attack')
    a.lw('t0',4,'s0');a.addiu('t0','t0',1);a.sw('t0',4,'s0')
    a.imm(11,'t1','t0',33);a.bne('t1','zero','choose')
    a.sw('zero',8,'s0');a.move('s5','zero');a.jump('walk')
    a.label('walk')
    a.li('t0',0x800683a0);a.lw('s5',0,'t0');a.imm(12,'s5','s5',0xf080)
    a.label('choose')
    table(a,'t0','s3',('crush','gulp','ripto'))
    a.jump('done')
    for name,cls,projectile,walk,close,ranged,launch in (
        ('crush',0x8f,0x288,3,4,1,13),('gulp',0x191,0x196,8,2,10,19),('ripto',0x2af,0x2c2,1,5,2,24)):
        a.label(name)
        a.li('t0',cls);a.lh('t1',54,'s1')
        a.beq('t0','t1',name+'_same')
        a.sh('t0',54,'s1');a.li('t0',255);a.sb('t0',61,'s1')
        a.label(name+'_same')
        a.li('t0',0x2a605050 if name=='gulp' else 0);a.sw('t0',84,'s1')
        a.li('t0',projectile);a.sh('t0',54,'s6')
        a.li('t0',1);a.beq('s5','t0',name+'_close')
        a.li('t0',2);a.beq('s5','t0',name+'_ranged')
        a.move('a2','zero');a.beq('s5','zero','pose')
        a.li('a2',walk);a.jump('pose')
        a.label(name+'_close')
        a.li('a2',close)
        a.lw('t0',4,'s0');a.li('t1',8 if name=='ripto' else 20)
        a.bne('t0','t1','pose')
        a.sw('a2',24,'sp');a.li('a1',1350 if name!='ripto' else 900)
        a.li('t0',0xb0000);a.sw('t0',16,'sp');a.sw('zero',20,'sp')
        a.addiu('a0','s1',12);a.move('a2','zero');a.move('a3','zero');a.jal(0x8004055c)
        a.lw('a2',24,'sp');a.jump('pose')
        a.label(name+'_ranged')
        a.li('a2',ranged);a.lw('t0',4,'s0');a.li('t1',launch)
        a.bne('t0','t1','pose')
        a.sw('a2',24,'sp');a.jal('launch');a.lw('a2',24,'sp');a.jump('pose')
    a.label('pose')
    a.lbu('t0',61,'s1');a.beq('t0','a2','rotate')
    for offset in (60,61):a.sb('a2',offset,'s1')
    a.sh('zero',62,'s1')
    a.li('t0',0x172);a.sh('t0',64,'s1')
    a.label('rotate')
    a.move('a0','s1');a.li('a1',4);a.jal(0x80041538)
    a.lw('t0',12,'s0');a.beq('t0','zero','hide_shot')
    a.addiu('t0','t0',-1);a.sw('t0',12,'s0')
    for i in range(3):
        a.lw('t0',12+i*4,'s6');a.lw('t1',16+i*4,'s0')
        a.addu('t0','t0','t1');a.sw('t0',12+i*4,'s6')
    a.lw('t0',20,'s6');a.li('t1',SPAWN[2]);a.slt('t0','t0','t1');a.bne('t0','zero','hide_shot')
    for offset, lower, width in ((12,SPAWN[0]-18*1024,36*1024),(16,SPAWN[1]-12*1024,36*1024)):
        a.lw('t0',offset,'s6');a.li('t1',lower);a.subu('t0','t0','t1')
        a.li('t1',width);a.r(0x2b,'t0','t0','t1');a.beq('t0','zero','hide_shot')
    a.li('t0',0x970000);a.sw('t0',16,'sp');a.sw('zero',20,'sp')
    a.addiu('a0','s6',12);a.li('a1',600);a.move('a2','zero');a.move('a3','zero');a.jal(0x8004055c)
    a.bne('v0','zero','hide_shot')
    a.li('t0',0x40);a.sb('t0',76,'s6')
    a.jump('done')
    a.label('spyro');a.sb('zero',76,'s1')
    a.lw('t0',28,'s0');a.beq('t0','zero','hide_shot')
    a.li('t0',0x8006a250);a.sw('zero',0,'t0');a.sw('zero',28,'s0')
    a.label('hide_shot');a.sb('zero',76,'s6');a.sw('zero',12,'s0')
    a.label('done')
    for i,r in enumerate(('ra','s0','s1','s2','s3','s4','s5','s6')):
        a.lw(r,60-4*i,'sp')
    a.addiu('sp','sp',64)
    a.jump(0x80040e94)
    a.label('launch')
    a.addiu('sp','sp',-16);a.sw('ra',12,'sp')
    a.li('t0',32);a.sw('t0',12,'s0')
    for i in range(3):
        a.lw('t0',12+i*4,'s1')
        if i==2:a.addiu('t0','t0',640)
        a.sw('t0',12+i*4,'s6')
    a.lbu('t0',70,'s1');a.sb('t0',70,'s6')
    sll(a,'t0','t0',1)
    for i,trig in enumerate((0x80061c58,0x80061bd8)):
        a.li('t1',trig);a.addu('t1','t0','t1');a.lh('t1',0,'t1')
        a.shift('t1','t1',3,True);a.sw('t1',16+i*4,'s0')
    a.li('t0',-18);a.sw('t0',24,'s0')
    a.sw('zero',60,'s6');a.li('t0',0x172);a.sh('t0',64,'s6')
    a.move('a0','s6');a.li('a1',4);a.jal(0x80041538)
    a.move('a0','s1');a.li('a1',1);a.move('a2','zero');a.move('a3','zero');a.jal(0x80050648)
    a.lw('ra',12,'sp');a.addiu('sp','sp',16);a.jr('ra')
    output.extend(a.finish())
    put32(output,HOOK-BASE,(3<<26)|((entry&0x0fffffff)>>2))
    # The two controller records are not encounter AI. Other Glimmer actors
    # still run via the original switch, including the basket targets.
    intercept=0x80070478
    a=Asm(BASE+len(output))
    dispatch=a.pc
    a.li('t0',0x80066f14);a.lw('t0',0,'t0')
    a.beq('s4','t0',0x8007e1ac)
    a.addiu('t0','t0',88);a.beq('s4','t0',0x8007e1ac)
    a.label('original');a.li('v0',0x1aa);a.beq('v1','v0',0x80075024)
    a.words[-1]=0x286201ab
    a.jump(intercept+12)
    output.extend(a.finish())
    put32(output,intercept-BASE,(2<<26)|((dispatch&0x0fffffff)>>2))
    put32(output,intercept+4-BASE,0)
    output.extend(bytes((-len(output))%2048))
    return output,start

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    scene_data=archive()
    overlay,state=build_overlay(read_archive(DISC,16))
    replacements={}
    loader,_,_=mesa_standalone_boot.build(minimal_menu=False)
    for slot,payload in ((1,loader),(16,overlay),(17,scene_data)):
        path=(args.out/f'slot{slot}.bin').resolve()
        path.write_bytes(payload)
        replacements[str(slot)]={'path':str(path),'sha256':hashlib.sha256(payload).hexdigest()}
    spec={'kind':'Boss Training Ground PSX development image','replacements':replacements,'controller_state':hex(state)}
    (args.out/'manifest.json').write_text(json.dumps(spec,indent=2)+'\n')
    print(spec)

if __name__=='__main__':
    main()

"""Temporary research decoder for retail Spyro's packed vertex animation.

Grounding: SCUS_944.25 8001c510 (relocation), 80041dbc (render/decode).
Outputs remain in data/ and are never bundled with the PC executable.
"""
from pathlib import Path
import json
import struct
from spyrodisc import u32, wad_index


def sx(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits-1)) else value


def unpack(word):
    return [sx(word >> 21,11),sx(word >> 10,11),sx(word,10)*2]


def decode(data, animation, frame, lookup):
    model = 0x800
    common = model + u32(data,model+56)
    info = model + u32(data,model+60+animation*4)
    header = data[info:info+4]
    base_vertices = common + u32(data,info+4)
    stream = common + u32(data,info+16)
    state = info+20+frame*20
    control, splits, root = (u32(data,state+i) for i in (0,4,8))
    offset = control & 0xffff
    verts = []
    for part,(start,end) in enumerate(((0,header[2]),(header[2],header[3]))):
        wide = (control >> (16+part)) & 1
        shorts = stream+offset+(0 if part==0 else splits&1023)
        bytes_at = stream+offset+((splits>>10)&1023 if part==0 else splits>>20)
        pos = [0,0,0]
        for i in range(start,end):
            packed = u32(data,base_vertices+i*4)
            if not wide:
                code=data[bytes_at];bytes_at+=1;wide=code&1
                if code>>1>=125: raise ValueError(f'Invalid lookup index {code>>1}')
                delta=unpack((packed+lookup[code>>1])&0xffffffff)
                pos=[p+d for p,d in zip(pos,delta)]
            else:
                code=struct.unpack_from('<H',data,shorts)[0];shorts+=2;wide=code&1
                if code&2:
                    next_word=struct.unpack_from('<H',data,shorts)[0];shorts+=2
                    pos=[sx(next_word,16)>>5,-((sx(next_word,6)<<5)|((code&0xf000)>>11)),-sx(code>>1,11)]
                else:
                    delta=unpack(packed)
                    delta=[delta[0]+(sx(code,16)>>9),delta[1]+sx(code>>4,7),delta[2]+sx(code>>1,5)*4]
                    pos=[p+d for p,d in zip(pos,delta)]
            translated=[p+r for p,r in zip(pos,unpack(root))] if part else pos[:]
            verts.append(translated)
    faces_at=common+u32(data,info+8)
    faces=[]
    for at in range(faces_at+4,faces_at+4+u32(data,faces_at),20):
        word=u32(data,at)
        indices=[(word>>shift)&255 for shift in (24,16,8,0)]
        if indices[2]==indices[3]:indices.pop()
        if max(indices)>=len(verts):raise ValueError('Face vertex out of range')
        faces.append(indices)
    return verts,faces


def main():
    archive=Path('data/disc/WAD.WAD')
    with archive.open('rb') as f:
        entry=wad_index(f.read(2048),archive.stat().st_size)[0]
        f.seek(entry.offset);data=f.read(entry.size)
    exe=Path('data/disc/SCUS_944.25').read_bytes()
    lookup=[u32(exe,0x800+0x52674+i*4) for i in range(125)]
    root=Path('data/model_probe');root.mkdir(exist_ok=True)
    result=[]
    for animation in range(62):
        pointer=u32(data,0x800+60+animation*4)
        if pointer==0xffffffff:continue
        count=data[0x800+pointer]
        ranges=[]
        for frame in range(count):
            verts,faces=decode(data,animation,frame,lookup)
            ranges.append([[min(v[i] for v in verts),max(v[i] for v in verts)] for i in range(3)])
            with (root/f'animation_{animation:02}_{frame:02}.obj').open('w') as out:
                for x,y,z in verts:out.write(f'v {y/1024} {z/1024} {x/1024}\n')
                for face in faces:
                    # PSX strip-style quad order, split on the 1-2 diagonal.
                    out.write('f '+' '.join(str(i+1) for i in face[:3])+'\n')
                    if len(face)==4:out.write('f '+' '.join(str(face[i]+1) for i in (1,3,2))+'\n')
        result.append({'animation':animation,'frames':count,'vertices':len(verts),'faces':len(faces),'ranges':ranges})
        print(animation,count,'frames',len(verts),'verts',len(faces),'faces','first bounds',ranges[0])
    (root/'summary.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()

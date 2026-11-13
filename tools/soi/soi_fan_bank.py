"""Pack the Blender fan using the original absolute vertex instruction format."""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
import soi_model_convert as fmt


def absolute(p):
    x,forward,height=np.rint(np.array(p)*512).astype(int)
    x=int(round(x/2)*2);forward=int(round((forward-(x<0))/2)*2+(x<0));z=int(round((height-(x<0))/4)*4)
    assert -1024<=forward<1024 and -1024<=x<1024 and -2048<=z<2048,(x,forward,z)
    nextword=((forward&2047)<<5)|((x//32)&31)
    code=((x%32)<<11)|((-z)&0xffc)|3
    return struct.pack('<HH',code,nextword),[x/512,forward/512,(z+(x<0))/512]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--name',default='2e5-cyan-fan');a=ap.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
    j=json.loads(a.source.read_text());n,nf=len(j['near']['vertices']),len(j['far']['vertices']);assert n<=255 and nf<n
    def faces(lod):
        b=bytearray()
        for f,c in zip(lod['faces'],lod['materials']):
            assert len(f) in (3,4)
            # Keep the editable Blender perimeter, but emit triangles. The
            # original animated-face consumer tests bits4/5 as billboard /
            # special material controls (80045124 -> 800456e4); encoding a
            # fourth colour there made the PS1 consume 20 bytes from our
            # eight-byte packet and then walk into the pose stream. The
            # native decoder alone did not expose that incompatibility.
            for tri in ([f] if len(f)==3 else [(f[0],f[1],f[2]),(f[0],f[2],f[3])]):
                ids=[tri[0],tri[2],tri[1],tri[1]]
                flags=(c<<24)|(c<<17)|(c<<10)
                assert not flags&0x38
                b.extend(struct.pack('<II',sum(v<<(24-8*k) for k,v in enumerate(ids)),flags))
        return struct.pack('<I',len(b))+b
    common=bytearray(bytes(4*n));na=len(common);common.extend(faces(j['near']));fa=len(common);common.extend(faces(j['far']));pa=len(common)
    common.extend(b''.join(bytes(c)+b'\0' for c in j['colours']));stream=len(common);descriptors=[];quantized=[]
    for clip in j['clips']:
        d=bytearray(36+8*len(clip['frames']));d[:6]=bytes([len(clip['frames']),len(j['colours']),1,0,n,nf]);struct.pack_into('<IIIIII',d,12,0,na,pa,fa,pa,stream)
        qframes=[]
        for fi,f in enumerate(clip['frames']):
            offset=(len(common)-stream)//2;verts=[]
            for p in f['vertices']:
                raw,q=absolute(p);common.extend(raw);verts.append(q)
            # The parent retains the original sound events. The attached
            # authored mesh has no sound slots and must use the signed silent
            # sentinel, never a zero tag that asks the PS1 to play slot FF.
            struct.pack_into('<II',d,36+fi*8,0x82000000|(2*n<<16)|offset,f['metadata']|0x00800000);qframes.append(verts)
        descriptors.append(d);quantized.append(qframes)
    head=bytearray(60+4*len(descriptors));fmt.put32(head,0,len(descriptors));head[4:16]=b'\xff'*12
    for i,d in enumerate(descriptors):fmt.put32(head,60+4*i,len(head));head.extend(d)
    fmt.put32(head,56,len(head));bank=bytes(head+common);p=a.out/(a.name+'.bank');p.write_bytes(bank)
    (a.out/'fan-quantized-poses.json').write_text(json.dumps(quantized,separators=(',',':'))+'\n')
    proof=dict(source=str(a.source.resolve()),source_sha256=fmt.sha(a.source.read_bytes()),bank=str(p.resolve()),bank_sha256=fmt.sha(bank),bytes=len(bank),near=n,far=nf,clips=len(descriptors),frames=sum(len(c['frames']) for c in j['clips']),vram_changes=False,runtime_faces='Triangles; no special face control bits; source Blender quad surfaces preserved',
               quantization='Original absolute stream, shift1: X 4 units; forward 4 units with X sign parity; height 8 units with original GTE borrow')
    (a.out/'fan-bank.json').write_text(json.dumps(proof,indent=2)+'\n');print('CYAN FAN BANK SHA256',fmt.sha(bank),'bytes',len(bank),'near',n,'far',nf,'source frames',proof['frames'])


if __name__=='__main__':main()

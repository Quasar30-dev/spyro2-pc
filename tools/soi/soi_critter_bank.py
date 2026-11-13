"""Encode authored Blender rabbit with native near/far prefix and hop frames."""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
import soi_model_convert as fmt


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    assert not a.out.exists();a.out.mkdir(parents=True);j=json.loads(a.source.read_text());near,far=j['lods'];n,nf=len(near['vertices']),len(far['vertices']);assert 0<nf<n<=255
    # Differential base: source Blender Z is height; native object coordinates
    # swap its Y/Z. Quantize to the original base stream's even vertical grid.
    pos=np.rint(np.asarray(near['vertices'])*1024).astype(int)
    internal=np.stack([pos[:,1],-pos[:,0],pos[:,2]-(pos[:,0]<0)],axis=1)
    internal[:,2]=np.rint(internal[:,2]/2).astype(int)*2
    d=np.diff(np.vstack([np.zeros((1,3),int),internal]),axis=0);assert np.abs(d[:,:2]).max()<1024 and np.abs(d[:,2]).max()<1024
    base=b''.join(struct.pack('<I',((int(x)&2047)<<21)|((int(y)&2047)<<10)|((int(z)//2)&1023)) for x,y,z in d)
    palette=b''.join(bytes(c)+b'\0' for c in j['colours'])
    def faces(lod):
        result=bytearray()
        for f,c in zip(lod['faces'],lod['materials']):
            # Mirror winding when Blender Z-up becomes native Y-up.
            aa,bb,cc=f;word=aa<<24|cc<<16|bb<<8|bb;colour=(c<<24)|(c<<17)|(c<<10)
            result.extend(struct.pack('<II',word,colour))
        return struct.pack('<I',len(result))+result
    common=bytearray(base);near_at=len(common);common.extend(faces(near));far_at=len(common);common.extend(faces(far));pal_at=len(common);common.extend(palette)
    stream_at=len(common);controls=[]
    # Wide control 1 adds zero to the differential base. First-vertex controls
    # 5/9/13 translate every following vertex by 8/16/24 units in height.
    for h in j['hop_units']:
        offset=(len(common)-stream_at)//2;codes=[1+(h//4)*2]+[1]*(n-1)
        assert all((c&3)==1 and c<16 for c in codes)
        common.extend(struct.pack('<'+'H'*n,*codes));controls.append(0x82000000|(n<<16)|offset)
    descriptor=bytearray(36+8*j['frames']);descriptor[:6]=bytes([j['frames'],len(j['colours']),0,0,n,nf])
    struct.pack_into('<IIIIII',descriptor,12,0,near_at,pal_at,far_at,pal_at,stream_at)
    # 800411cc treats metadata bits16..23 as a signed sound-slot index.
    # Zero requests slot0; bit23 marks an explicitly silent authored frame.
    for i,c in enumerate(controls):struct.pack_into('<II',descriptor,36+i*8,c,(3<<9)|0x00800000)
    head=bytearray(64);struct.pack_into('<I',head,0,1);head[4:16]=b'\xff'*12;fmt.put32(head,56,64+len(descriptor));fmt.put32(head,60,64)
    bank=bytes(head+descriptor+common);path=a.out/'2e3-rabbit.bank';path.write_bytes(bank)
    proof=dict(source=str(a.source.resolve()),source_sha256=fmt.sha(a.source.read_bytes()),bank=str(path.resolve()),bank_sha256=fmt.sha(bank),near=n,far=nf,frames=j['frames'],bytes=len(bank),
               material='Native untextured palette packets; authored UV/swatches remain editable',model_vram_changes=False)
    (a.out/'rabbit-bank.json').write_text(json.dumps(proof,indent=2)+'\n');print('RABBIT BANK SHA256',fmt.sha(bank),'near',n,'far',nf,'frames',j['frames'],'bytes',len(bank))


if __name__=='__main__':main()

"""Original CPU descriptor/GPU regression after actor-overlay sector padding."""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route
import mesa_uv_check
import soi_model_convert as fmt
from soi_actor_runtime_check import ActorPad
from soi_container_runtime_check import word


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path);ap.add_argument('proof',type=Path);ap.add_argument('--work',type=Path,required=True);a=ap.parse_args()
    j=json.loads(a.proof.read_text());cue=a.cue.resolve();archive=Path(j['archive']).read_bytes();overlay=Path(j['overlay']).read_bytes()
    assert fmt.sha(archive)==j['archive_sha256'] and fmt.sha(overlay)==j['overlay_sha256'] and len(overlay)%2048==0
    digest=private.sha(cue.with_suffix('.bin'));ch=fmt.wad_index(archive[:65536],len(archive));sc=fmt.s2scene_build.split(archive[ch[1].offset:ch[1].offset+ch[1].size])
    materials=fmt.u32(sc['parts'][0],4);descriptors=sc['parts'][0][8:8+materials*48];allowed=set()
    for m in range(materials):
        for q in range(4):
            d=descriptors[m*48+16+8*q:m*48+24+8*q];allowed.add((tuple(mesa_uv_check.consumer(d)),int.from_bytes(d[2:4],'little'),d[6]))
    private.TASK=a.work.resolve();private.d.OUT=private.TASK/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('actor-render');log('ARTIFACT BIN SHA256',digest,cue);log('SLOT17',j['archive_sha256'],'SLOT16',j['overlay_sha256'],'sector-aligned',len(overlay))
    with private.Session(cue,log,'actor-render') as s:
        pad=ActorPad(s);pad.sounds=set();g=s.gdb;assert route.to_glimmer(pad,log,limit=1200);pad.run(0,20)
        ram=g.read(0x80000000,0x200000);offset=ram.find(descriptors)
        assert offset>=0,'Original loaded material descriptors differ from exact archive after overlay DMA'
        log('ORIGINAL ALL MATERIAL DESCRIPTORS EXACT',digest,materials,'materials',len(descriptors),'bytes','RAM',hex(0x80000000+offset),'archive SHA256',j['archive_sha256'])
        assert word(g,0x800673e4)==1 and word(g,0x80067400)==0x2c000
        base=word(g,0x80066f14);assert g.read(base+88*j['count']+72,1)==b'\xff' or word(g,0x800670bc)==base+88*j['count']
        assert g.read(base+88*9+72,1)==b'\xfd'
        assert g._send('Z0,8007ded0,4')=='OK';assert g._send('Z0,80000080,4')=='OK'
        assert g._send(f'z0,{private.d.LOGIC:x},4')=='OK';assert g._send('Z0,800265b0,4')=='OK'
        near=[]
        for _ in range(24):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x800265b0;p=g.read(g.regs()[30],52);assert p[7]&0xfd==0x3c
            uv=tuple((p[k],p[k+1]) for k in [12,24,36,48]);clut=int.from_bytes(p[14:16],'little');page=int.from_bytes(p[26:28],'little')&0x1ff
            assert (uv,clut,page) in allowed;near.append(p.hex())
        log('ORIGINAL ACTOR CANDIDATE NEAR GPU EXACT',digest,'24 real textured packets match archive/original UV consumer',json.dumps(near))
        assert g._send('z0,800265b0,4')=='OK';assert g._send('Z0,80028ba4,4')=='OK'
        far=[]
        for _ in range(12):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x80028ba4;p=g.regs()[15];h=g.read(p,28);assert h[16]>0 and h[16]+h[17]+2*h[18]==h[23];far.append(dict(pointer=hex(p),counts=list(h[16:24])))
        log('ORIGINAL ACTOR CANDIDATE FAR RECORDS',digest,json.dumps(far))
        assert g._send('z0,80028ba4,4')=='OK';assert g._send('Z0,80028f9c,4')=='OK';packets=[]
        for _ in range(6):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x80028f9c;p=g.read(g.regs()[30],36);assert p[7]==0x38;packets.append(p.hex())
        log('ORIGINAL ACTOR CANDIDATE FAR GPU',digest,'six original POLY_G4 packets',json.dumps(packets))
        assert g._send('z0,80028f9c,4')=='OK';assert g._send(f'Z0,{private.d.LOGIC:x},4')=='OK';g.s.sendall(b'$c#63');pad._wait_stop();start=word(g,0x8006700c)
        for i in range(6):pad.run(0,300);log('FINITE ORIGINAL ACTOR RENDER PROGRESS',(i+1)*300,'timer',word(g,0x8006700c))
        assert word(g,0x8006700c)>start and g.read(base+88*9+72,1)==b'\xfd'
        (private.d.OUT/'final-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        log('ORIGINAL ACTOR CANDIDATE RENDER COMPLETE',digest,'all descriptor bytes exact;24 near packets;12 far records;6 far packets;1800 later samples; Moneybags never reached; no gameplay RAM fixture')
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()

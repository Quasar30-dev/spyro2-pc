"""Exact-disc original near/far terrain consumers and finite no-fixture runtime."""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route
import mesa_uv_check
import soi_model_convert as fmt

def word(g,a):return struct.unpack('<I',g.read(a,4))[0]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path);ap.add_argument('build_report',type=Path);ap.add_argument('--work',type=Path,required=True)
    a=ap.parse_args();cue=a.cue.resolve();build=json.loads(a.build_report.read_text());arc=Path(build['archive']).read_bytes()
    assert fmt.sha(arc)==build['archive_sha256'];digest=private.sha(cue.with_suffix('.bin'))
    active=Path('D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-music-active.json')
    assert not json.loads(active.read_text())['active']
    ch=fmt.wad_index(arc[:65536],len(arc));sc=fmt.s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size]);allowed=set()
    descriptors=sc['parts'][0][8:]
    for material in range(build['materials']):
        for q in range(4):
            d=descriptors[material*48+16+8*q:material*48+24+8*q]
            allowed.add((tuple(mesa_uv_check.consumer(d)),int.from_bytes(d[2:4],'little'),d[6]))
    private.TASK=a.work.resolve();private.d.OUT=a.work.resolve()/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('content-render');log('ARTIFACT',cue,'BIN sha256',digest);log('ARCHIVE',build['archive_sha256'])
    with private.Session(cue,log,'content-render') as session:
        pad=private.Pad(session);g=session.gdb;assert route.to_glimmer(pad,log,limit=1200);pad.run(0,20)
        assert word(g,0x800673e4)==1 and word(g,0x80067400)==0x2c000
        log('ORIGINAL VISIBILITY PASS',digest,'enabled; distance 2c000; regions',word(g,0x800673d8))
        mobies=word(g,0x80066f14);assert g.read(mobies+9*88+72,1)==b'\xfd'
        assert g._send('Z0,8007ded0,4')=='OK'
        assert g._send(f'z0,{private.d.LOGIC:x},4')=='OK';assert g._send('Z0,800265b0,4')=='OK'
        near=[]
        for _ in range(24):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x800265b0
            packet=g.read(g.regs()[30],52);assert packet[7]&0xfd==0x3c
            uv=tuple((packet[k],packet[k+1]) for k in (12,24,36,48));clut=int.from_bytes(packet[14:16],'little');page=int.from_bytes(packet[26:28],'little')&0x1ff
            assert (uv,clut,page) in allowed
            near.append(packet.hex())
        log('ORIGINAL NEAR UV/GPU PASS',digest,'24 textured commands match exact descriptors/original UV consumer',json.dumps(near))
        assert g._send('z0,800265b0,4')=='OK';assert g._send('Z0,80028ba4,4')=='OK'
        far=[]
        for _ in range(12):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x80028ba4
            ptr=g.regs()[15];h=g.read(ptr,28);assert h[16]>0 and h[16]+h[17]+2*h[18]==h[23]
            far.append(dict(pointer=hex(ptr),counts=list(h[16:24])))
        log('ORIGINAL FAR RECORDS PASS',digest,json.dumps(far))
        assert g._send('z0,80028ba4,4')=='OK';assert g._send('Z0,80028f9c,4')=='OK'
        commands=[]
        for _ in range(6):
            g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==0x80028f9c
            packet=g.read(g.regs()[30],36);assert packet[7]==0x38;commands.append(packet.hex())
        log('ORIGINAL FAR GPU PASS',digest,'six original POLY_G4 commands',json.dumps(commands))
        assert g._send('z0,80028f9c,4')=='OK';assert g._send(f'Z0,{private.d.LOGIC:x},4')=='OK'
        g.s.sendall(b'$c#63');pad._wait_stop();t0=word(g,0x8006700c)
        for i in range(6):
            pad.run(0,300);log('FINITE ORIGINAL PROGRESS',(i+1)*300,'timer',word(g,0x8006700c))
        assert word(g,0x8006700c)>t0 and g.read(mobies+9*88+72,1)==b'\xfd'
        log('ORIGINAL CONTENT RENDER/RUNTIME PASS',digest,'1800 logic ticks; actual near UV and far records/GPU; Moneybags handler never reached; no gameplay RAM fixture')
        (private.d.OUT/'final-ram.bin').write_bytes(g.read(0x80000000,0x200000))
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()

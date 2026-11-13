"""Original PS1 model-loader/GPU evidence using private RAM presentation fixtures.

An existing nearby gem record temporarily presents each converted model. No
fixture is written to the disc, no AI/story is attributed to this presentation,
and the user's application/session is never touched.
"""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route


def word(g,a): return struct.unpack('<I',g.read(a,4))[0]


class ModelPad(private.Pad):
    def _wait_stop(self,limit=30.,probe=None):
        self.g.s.settimeout(limit)
        for _ in range(1200):  # finite original renderer events before one pad tick
            stop=self.g._recv();pc=self.g.pc()
            assert pc!=0x8007ded0
            if getattr(self,'target',None) and pc in (0x800445b4,0x800450ac,0x800452d0):
                regs=self.g.regs();base,end=self.target
                if pc==0x800445b4:
                    descriptor=word(self.g,regs[19]+4)
                    if base<=descriptor<end:
                        self.descriptor=descriptor
                        self.s.log('ORIGINAL TARGET DESCRIPTOR',hex(self.cls),hex(descriptor),'state',self.g.read(regs[19],68).hex())
                        assert self.g._send('z0,800445b4,4')=='OK'
                        for point in (0x800450ac,0x800452d0):assert self.g._send(f'Z0,{point:x},4')=='OK'
                elif base<=regs[30]-20<end:
                    source=self.g.read(regs[30]-20,20)
                    quad=pc==0x800450ac
                    packet=self.g.read(regs[24],52 if quad else 40)
                    assert packet[7]&0xfd==(0x3c if quad else 0x34)
                    expected=(int.from_bytes(source[8:12],'little')+regs[18])&0xffffffff
                    assert word(self.g,regs[24]+12)==expected
                    assert packet[24:28]==source[12:16]
                    assert packet[36:38]==source[16:18]
                    if quad:assert packet[48:50]==source[18:20]
                    self.packet=dict(cls=self.cls,descriptor=hex(self.descriptor),pc=hex(pc),face=hex(regs[30]-20),
                        source_face=source.hex(),gpu=hex(regs[24]),packet=packet.hex(),texture_add=hex(regs[18]),
                        moby_animation=self.g.read(self.moby+0x3c,6).hex())
                    self.s.log('ORIGINAL MODEL GPU PACKET',json.dumps(self.packet))
                    for point in (0x800450ac,0x800452d0):assert self.g._send(f'z0,{point:x},4')=='OK'
                    self.target=None
                self.g.s.sendall(b'$c#63');continue
            return stop
        raise AssertionError('finite original renderer event bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path);ap.add_argument('conversion',type=Path)
    ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--container-proof',type=Path)
    a=ap.parse_args();cue=a.cue.resolve();report=json.loads(a.conversion.read_text())
    build=json.loads(a.conversion.with_name('mesa-ps1-report.json').read_text())
    if a.container_proof:
        cp=json.loads(a.container_proof.read_text());assert cp['base_archive_sha256']==report['archive_sha256']
        report.update(archive=cp['archive'],archive_sha256=cp['archive_sha256'])
        build=json.loads(a.container_proof.with_name('mesa-ps1-report.json').read_text())
    digest=private.sha(cue.with_suffix('.bin'))
    active=Path('D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-music-active.json')
    assert not json.loads(active.read_text())['active']
    private.TASK=a.work.resolve()
    private.d.OUT=a.work.resolve()/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('model-original-gpu');log('ARTIFACT',cue,'BIN sha256',digest)
    log('ARCHIVE',report['archive'],'sha256',report['archive_sha256'])
    results=[]
    with private.Session(cue,log,'original-gpu') as session:
        pad=ModelPad(session);g=session.gdb
        assert route.to_glimmer(pad,log,limit=1200)
        pad.run(0,20)
        assert pad.state()[:2]==(0,1)
        origin=pad.state()[2];mobies=word(g,0x80066f14)
        options=[]
        for e in build['gem_records']:
            addr=mobies+88*e['index'];record=g.read(addr,88)
            if record[72]>=128:continue
            pos=struct.unpack_from('<3i',record,12)
            distance=sum((x-y)**2 for x,y in zip(pos,origin))
            options.append((distance,addr,e['index'],record))
        distance,moby,index,original=min(options)
        assert distance<6000**2
        log('RAM PRESENTATION FIXTURE','named existing gem index',index,'address',hex(moby),'original',original.hex())
        for model in sorted(report['models'],key=lambda m:m['target_class']):
            cls=model['target_class'];base=word(g,0x80068c94+4*cls)
            assert 0x80070000<base<base+model['bytes']<0x80200000
            assert word(g,base)==model['source_clips']
            for c in range(model['source_clips']):
                descriptor=word(g,base+60+4*c)
                assert base<=descriptor<base+model['bytes']
                assert base<=word(g,descriptor+12)<base+model['bytes']
                assert base<=word(g,descriptor+32)<base+model['bytes']
            log('ORIGINAL LOADED MODEL',hex(cls),'bank',hex(base),'clips',model['source_clips'],'bank sha256',model['bank_sha256'])
            record=bytearray(original);struct.pack_into('<H',record,54,cls)
            struct.pack_into('<3i',record,12,*origin)
            record[8:12]=bytes(4);record[60:66]=bytes([0,0,0,1,0x72,1])
            record[68:71]=bytes(3);record[79]=32
            record[72]=0;record[73]=0;record[76]=0x60;record[80]=255
            record[24:28]=bytes(4)
            assert g.write(moby,record)=='OK'
            pad.target=(base,base+model['bytes']);pad.cls=cls;pad.moby=moby;pad.packet=None
            assert g._send('Z0,800445b4,4')=='OK'
            for _ in range(30):
                pad.run(0,1)
                if pad.packet:break
            assert pad.packet, f'no original GPU packet for {cls:x}'
            results.append(pad.packet)
            pad.run(0,16)
            pad.shot(private.d.OUT/f'model-{cls:03x}.png')
            assert g.write(moby,original)=='OK'
        (private.d.OUT/'model-packets.json').write_text(json.dumps(results,indent=2)+'\n')
        log('ORIGINAL MODEL LOADER/NEAR GPU PASS',digest,'3 converted banks; exact relocated descriptor pointers and textured GPU packet UV/CLUT/page words;',
            'RAM presentation fixture only; no AI/attack/death/story proof')
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()

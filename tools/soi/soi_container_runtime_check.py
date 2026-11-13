"""Actual PS1 charge-only box consumers, with explicit post-collision RAM hits.

The scene/objects/terrain are never moved in the image. A private Spyro-origin
fixture activates each source box; named hit mailboxes exercise ordinary flame
and charge, through the actual patched dispatcher and existing break/release.
"""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route

def word(g,a):return struct.unpack('<I',g.read(a,4))[0]


class GatePad(private.Pad):
    def _wait_stop(self,limit=30.,probe=None):
        self.g.s.settimeout(limit)
        for _ in range(64):
            stop=self.g._recv();pc=self.g.pc()
            assert pc!=0x8007ded0,'Moneybags handler reached'
            if pc==0x8001d2d0 and getattr(self,'loader_observer',None):
                if self.loader_observer():assert self.g._send('z0,8001d2d0,4')=='OK'
                self.g.s.sendall(b'$c#63');continue
            if pc==getattr(self,'gate_pc',None):
                target=self.g.regs()[20]
                if target==self.target:
                    hit=word(self.g,target+24);assert hit==self.expected_hit
                    self.hit_seen=True
                    self.s.log('NAMED ORIGINAL C9 GATE',self.index,'moby',hex(target),'hit',hex(hit),'PC',hex(pc))
                    assert self.g._send(f'z0,{pc:x},4')=='OK'
                self.g.s.sendall(b'$c#63');continue
            if pc==0x80039334 and getattr(self,'release_target',None):
                if self.g.regs()[4]==self.release_target:
                    assert self.g.read(self.release_target+80,1)==b'\x01'
                    self.release_seen=True
                    self.release_return=self.g.regs()[31]
                    assert self.g._send(f'Z0,{self.release_return:x},4')=='OK'
                    self.s.log('NAMED ORIGINAL GEM RELEASE',self.index,'moby',hex(self.release_target),'held value 1; PC 80039334')
                    assert self.g._send('z0,80039334,4')=='OK'
                self.g.s.sendall(b'$c#63');continue
            if pc==getattr(self,'release_return',None):
                addr=self.g.regs()[2];record=self.g.read(addr,88)
                assert 0x80070000<addr<addr+88<0x80200000
                assert struct.unpack_from('<H',record,54)[0]==1 and record[72]<128
                variant=self.g.read(0x8006440c+1,1)[0]
                assert record[73]==variant and record[60]==self.g.read(0x80064430+variant,1)[0]
                assert record[84]==self.g.read(0x80064438+variant,1)[0]
                assert self.g.read(0x80064428+variant,1)==b'\x01'
                self.released_gem=dict(address=hex(addr),record=record.hex(),value=1,variant=variant)
                self.s.log('ORIGINAL RETURNED SOURCE GEM',self.index,json.dumps(self.released_gem))
                assert self.g._send(f'z0,{pc:x},4')=='OK'
                self.release_return=None
                self.g.s.sendall(b'$c#63');continue
            return stop
        raise AssertionError('finite box dispatcher-event bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('cue',type=Path);ap.add_argument('conversion',type=Path);ap.add_argument('--work',type=Path,required=True)
    a=ap.parse_args();cue=a.cue.resolve();proof=json.loads(a.conversion.read_text())
    build=json.loads(a.conversion.with_name('mesa-ps1-report.json').read_text());digest=private.sha(cue.with_suffix('.bin'))
    active=Path('D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-music-active.json')
    assert not json.loads(active.read_text())['active']
    private.TASK=a.work.resolve();private.d.OUT=a.work.resolve()/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('source-containers');log('ARTIFACT',cue,'BIN sha256',digest)
    log('SLOT17',proof['archive_sha256'],'SLOT16',proof['overlay_sha256'])
    with private.Session(cue,log,'source-containers') as session:
        pad=GatePad(session);g=session.gdb;loader={}
        def observe_loader():
            if word(g,private.d.LEVEL)!=1:return False
            base=word(g,0x80066f14);count=build['objects']['count'];assert count==225
            assert g.read(base+88*count+72,1)==b'\xff'
            assert g.read(base+88*9+72,1)==b'\xfd'
            assert g.read(base+88*(count-1)+58,1)==b'\xff'
            loader.update(base=base,count=count)
            log('ORIGINAL LOADER BOUNDARY PASS',digest,'PC 8001d2d0; index9 fd; index224 initialized; terminal ff after225 before dynamic allocation')
            return True
        pad.loader_observer=observe_loader;assert g._send('Z0,8001d2d0,4')=='OK'
        assert route.to_glimmer(pad,log,limit=1200);pad.run(0,20)
        origin=g.read(private.d.SPYRO,12);base=word(g,0x80066f14);count=build['objects']['count'];assert count==225
        assert loader==dict(base=base,count=count) and word(g,0x800670bc)==base+88*count
        for e in proof['containers']:
            target=base+88*e['index'];record=g.read(target,88)
            assert struct.unpack_from('<H',record,54)[0]==0xc9 and record[80]==1
            assert record[72]<128 and g.read(base+88*e['old_index']+72,1)==b'\xfd'
            param=word(g,target);curve=word(g,param+24)
            assert 0x80070000<param<param+28<0x80200000 and 0x80070000<curve<curve+64<0x80200000
        log('APPENDED C9 LOADER PASS',digest,'five source positions/classes/values/independent parameter blocks; old C8 records fd; final sentinel after 225')
        assert g._send('Z0,8007ded0,4')=='OK'
        saved_purchase=g.read(0x8006467e,2);saved_timer=g.read(0x80067008,4)
        assert g.write(0x8006467e,b'\x01\x00')=='OK'
        for _ in range(12):
            assert g.write(0x80067008,bytes(4))=='OK';pad.run(0,1)
            assert g.read(base+88*9+72,1)==b'\xfd'
        assert g.write(0x8006467e,saved_purchase)=='OK';assert g.write(0x80067008,saved_timer)=='OK'
        log('MONEYBAGS PURCHASED FLAG PASS',digest,'12 finite timer-zero/purchased ticks; handler never reached; index9 fd')
        for e in proof['containers']:
            target=base+88*e['index'];pos=e['position']
            pad.index=e['index'];pad.target=target;pad.gate_pc=proof['extension']['entry']
            assert g.write(private.d.SPYRO,struct.pack('<3i',pos[0]+1000,pos[1],pos[2]+512))=='OK'
            pad.run(0,3)
            pad.expected_hit=0x10000;pad.hit_seen=False
            assert g.write(target+24,struct.pack('<I',pad.expected_hit))=='OK'
            assert g._send(f'Z0,{pad.gate_pc:x},4')=='OK'
            for _ in range(12):
                pad.run(0,1)
                if pad.hit_seen:break
            assert pad.hit_seen and g.read(target+72,1)[0]<128 and word(g,target+24)==0
            log('ORIGINAL FLAME REJECTION PASS',digest,'source',e['source_index'],'record',e['index'],'normal flame hit 10000 consumed, target intact')
            start_pool=word(g,0x800670bc);end_pool=word(g,0x80066fb4)
            assert start_pool<end_pool<0x80200000
            def gems():
                return {addr for addr in range(start_pool,end_pool,88) if g.read(addr+72,1)[0]<128 and g.read(addr+54,2)==b'\x01\x00'}
            before=gems();pad.expected_hit=0x20000;pad.hit_seen=False;pad.release_target=target;pad.release_seen=False;pad.released_gem=None
            assert g._send('Z0,80039334,4')=='OK'
            assert g.write(target+24,struct.pack('<I',pad.expected_hit))=='OK'
            assert g._send(f'Z0,{pad.gate_pc:x},4')=='OK'
            for _ in range(12):
                pad.run(0,1)
                if pad.hit_seen and pad.release_seen and pad.released_gem:break
            assert pad.hit_seen and pad.release_seen and pad.released_gem and g.read(target+72,1)[0]>=128
            log('ORIGINAL CHARGE RELEASE PASS',digest,'source',e['source_index'],'record',e['index'],'removed; returned class1 gem value1',pad.released_gem['address'],
                'variant/model/colour exact original tables; observed before later pickup')
            pad.release_target=None
        assert g.write(private.d.SPYRO,origin)=='OK'
        t0=word(g,0x8006700c);pad.run(0,600);assert word(g,0x8006700c)>t0
        log('ORIGINAL SOURCE CONTAINER BEHAVIOR PASS',digest,'all five source136 records reject normal flame / charge-remove / release value1; 600 further logic ticks;',
            'post-collision mailbox/player-origin RAM fixtures; not a physical-input playthrough')
        (private.d.OUT/'final-ram.bin').write_bytes(g.read(0x80000000,0x200000))
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()

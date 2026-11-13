"""Actual original PS1 actor execution on a copied emulator/hidden desktop.

All player origins and hit mailboxes are explicitly recorded RAM fixtures.
No physical-input combat or manual acceptance is claimed by this check.
"""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route
from soi_container_runtime_check import word


class ActorPad(private.Pad):
    def _wait_stop(self,limit=30.,probe=None):
        self.g.s.settimeout(limit)
        for _ in range(96):
            stop=self.g._recv();pc=self.g.pc()
            assert pc!=0x8007ded0,'Moneybags handler reached'
            if pc==0x80000080:
                regs=bytes.fromhex(self.g._send('g')[32*8:38*8]);cause=struct.unpack_from('<I',regs,16)[0]
                if ((cause>>2)&31) in (0,8):
                    self.g.s.sendall(b'$c#63');continue  # ordinary IRQ / BIOS syscall
                # A failure-only private CPU fixture reads CP0 EPC before the
                # BIOS destroys the original call context. No image is edited.
                assert self.g._send('z0,80000080,4')=='OK'
                assert self.g.write(0x80000080,struct.pack('<II',0x401a7000,0))=='OK'
                # Execute through the uncached alias after the private write;
                # the retail exception vector may already be in instruction cache.
                packet=self.g._send('g');packet=packet[:37*8]+struct.pack('<I',0xa0000080).hex()+packet[38*8:]
                assert self.g._send('G'+packet)=='OK'
                assert self.g._send('Z0,a0000088,4')=='OK'
                self.g.s.sendall(b'$c#63');self.g._recv();epc=self.g.regs()[26]
                self.s.log('ORIGINAL CPU EXCEPTION EPC',hex(epc),'cause',hex(cause),'fault instruction bytes',self.g.read(epc,16).hex(),'failure-only private uncached exception-vector CP0-read fixture')
                raise AssertionError('Original CPU exception '+hex(epc))
            if pc==0x80050648:
                m,sound=self.g.regs()[4:6]
                if 0x80070000<m<0x801f0000 and sound<128:
                    cls=struct.unpack('<H',self.g.read(m+54,2))[0]
                    if 0x2e0<=cls<=0x2e7 and (cls,sound) not in self.sounds:
                        self.sounds.add((cls,sound));self.s.log('ORIGINAL ACTOR SOUND REQUEST',hex(cls),sound,hex(m),'clip/frame',self.g.read(m+60,6).hex())
                self.g.s.sendall(b'$c#63');continue
            if pc==0x80050720:
                sound,m=self.g.regs()[4:6]
                if 0x80070000<m<0x801f0000:
                    cls=struct.unpack('<H',self.g.read(m+54,2))[0]
                    if 0x2e0<=cls<=0x2e7:
                        if not hasattr(self,'spu_plays'):self.spu_plays=set()
                        if (cls,sound) not in self.spu_plays:
                            self.spu_plays.add((cls,sound));self.s.log('ORIGINAL ACTOR SPU PLAY ENTRY',hex(cls),'sample',sound,'moby',hex(m),'actual original sound allocator80050720; no listening claim')
                self.g.s.sendall(b'$c#63');continue
            if pc==0x8001d2d0 and getattr(self,'loader_observer',None):
                if self.loader_observer():assert self.g._send('z0,8001d2d0,4')=='OK'
                self.g.s.sendall(b'$c#63');continue
            if pc==0x80039334 and getattr(self,'release_target',None):
                if self.g.regs()[4]==self.release_target:
                    self.release_return=self.g.regs()[31];self.released_value=self.g.read(self.release_target+80,1)[0]
                    assert self.g._send(f'Z0,{self.release_return:x},4')=='OK'
                    assert self.g._send('z0,80039334,4')=='OK'
                self.g.s.sendall(b'$c#63');continue
            if pc==getattr(self,'release_return',None):
                ptr=self.g.regs()[2];record=self.g.read(ptr,88)
                assert 0x80070000<ptr<ptr+88<0x80200000 and struct.unpack_from('<H',record,54)[0]==1
                variant=self.g.read(0x8006440c+self.released_value,1)[0]
                assert record[73]==variant and self.g.read(0x80064428+variant,1)[0]==self.released_value
                self.released=dict(address=hex(ptr),value=self.released_value,record=record.hex())
                self.s.log('ORIGINAL ENEMY GEM RETURN',self.index,json.dumps(self.released))
                assert self.g._send(f'z0,{pc:x},4')=='OK';self.release_return=None
                self.g.s.sendall(b'$c#63');continue
            return stop
        raise AssertionError('Finite actor event bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path);ap.add_argument('proof',type=Path);ap.add_argument('--work',type=Path,required=True)
    a=ap.parse_args();cue=a.cue.resolve();proof=json.loads(a.proof.read_text());digest=private.sha(cue.with_suffix('.bin'))
    private.TASK=a.work.resolve();private.d.OUT=a.work.resolve()/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('actors-original');log('ARTIFACT',cue,'BIN SHA256',digest);log('SLOT17 SHA256',proof['archive_sha256'],'SLOT16 SHA256',proof['overlay_sha256'])
    with private.Session(cue,log,'actors-original') as s:
        pad=ActorPad(s);pad.sounds=set();g=s.gdb;loader={}
        def observe():
            if word(g,private.d.LEVEL)!=1:return False
            base=word(g,0x80066f14);n=proof['count'];assert g.read(base+88*n+72,1)==b'\xff'
            assert g.read(base+88*9+72,1)==b'\xfd'
            for e in proof['actors']:
                m=base+88*e['index'];r=g.read(m,88);p=word(g,m)
                assert struct.unpack_from('<H',r,54)[0]==e['cls'] and list(struct.unpack_from('<3i',r,12))==e['position']
                assert word(g,p)==0x534f4933 and word(g,p+4)==e['kind']
            loader.update(base=base,count=n);log('ORIGINAL ACTOR LOADER',digest,'count',n,'terminal ff; exact source anchors and private relocated params; Moneybags fd')
            assert g._send('Z0,80000080,4')=='OK'
            return True
        pad.loader_observer=observe;assert g._send('Z0,8001d2d0,4')=='OK'
        # The loader observer arms the exception trap before the Mesa fly-in;
        # title/CD BIOS traffic is outside the finite actor event budget.
        assert route.to_glimmer(pad,log,limit=1200);pad.run(0,20)
        assert loader and loader['count']==proof['count'];base=loader['base'];origin=g.read(private.d.SPYRO,12)
        assert g._send('Z0,8007ded0,4')=='OK'
        assert g._send('Z0,80050648,4')=='OK'
        tested=[]
        for e in proof['actors']:
            if e['kind'] not in (1,2):continue
            m=base+88*e['index'];p=word(g,m);pos=e['position'];pad.index=e['index']
            # Independent named encounters, with explicit health/invulnerability
            # fixtures. Damage acceptance has its own finite check below.
            assert g.write(0x8006a248,struct.pack('<I',3))=='OK'
            assert g.write(0x8006a24c,struct.pack('<I',600))=='OK'
            assert g.write(private.d.SPYRO,struct.pack('<3i',pos[0]+900,pos[1],pos[2]+512))=='OK'
            pad.run(0,2);states=set();clips=set()
            for _ in range(32):
                pad.run(0,1);states.add(g.read(m+72,1)[0]);clips.add(g.read(m+61,1)[0])
            log('NAMED SOURCE ACTOR LIVE',digest,'source',e['source_index'],'index',e['index'],'states',sorted(states),'clips',sorted(clips),'player',pad.state(),'record',g.read(m,88).hex(),'params',g.read(p,64).hex())
            assert 3 in states,'Actual source actor did not swing'
            pad.release_target=m;pad.release_return=None;pad.released=None
            assert g._send('Z0,80039334,4')=='OK'
            assert g.write(m+24,struct.pack('<I',0x10000))=='OK'
            # Vblank/controller samples are not guaranteed to be two logic
            # ticks when the original renderer is busy. Observe the adapter's
            # real timer, bounded by 160 samples, rather than assuming 64.
            for _ in range(160):
                pad.run(0,1)
                if pad.released and g.read(m+72,1)==b'\xfd':break
            assert pad.released and pad.released['value']==e['value'] and g.read(m+72,1)==b'\xfd',(e['source_index'],g.read(m+72,1).hex(),word(g,p+24),pad.released)
            log('ORIGINAL SOURCE ENEMY RESULT',digest,'source',e['source_index'],'index',e['index'],'states',sorted(states),'clips',sorted(clips),'flame mailbox, removed fd, original returned gem',e['value'])
            tested.append(e['source_index']);pad.release_target=None
        assert len(tested)==21 and 141 in tested
        assert g.write(private.d.SPYRO,origin)=='OK';t=word(g,0x8006700c);pad.run(0,600);assert word(g,0x8006700c)>t
        (private.d.OUT/'final-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        log('ORIGINAL ACTOR EXECUTION COMPLETE',digest,'all21 source records; swing/clip states, named flame-mailbox deaths and26 gem value; 600 later logic ticks; explicit RAM fixtures; no physical-input acceptance')
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()

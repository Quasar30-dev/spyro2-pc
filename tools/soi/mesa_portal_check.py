"""Private original CPU portal crossing using finite controller samples.

Summer Forest is entered with an explicit original load-call fixture. From its
original spawn, normal controller movement must cross the new collision plane
and load Mesa. Return Home uses a named original-call fixture separately.
"""
import argparse
import json
import struct
from pathlib import Path
import ds_private_check as private
import ds_pad_proof as route
import ds_harness as d
from ds_mesa_play import Driver
from mesa_standalone_check import call


class CinematicPad(private.Pad):
    def _wait_stop(self,limit=30.,probe=None):
        self.s.running=True;self.g.s.settimeout(limit)
        for _ in range(32):
            stop=self.g._recv();self.s.running=False;pc=self.g.pc()
            if pc!=0x800705f8:return stop
            scene=struct.unpack('<I',self.g.read(0x80066f90,4))[0]
            if scene==70:
                import mipsdis
                base,oracle=mipsdis.exe_image()
                addresses=[0x80064994+29,0x80064b84+29,0x80060140+29,0x80064a34+29,0x80061bb8+29]
                for addr in addresses:assert self.g.read(addr,1)==oracle[addr-base:addr-base+1],hex(addr)
                assert self.g.read(0x800649b4+29*4,4)==oracle[0x800649b4+29*4-base:0x800649b4+29*4-base+4]
                self.seen_original_cinematic70=True
                self.s.log('ORIGINAL CINEMATIC70 METADATA EXACT',self.digest,'original scene70 still ordinal29; original name/home/music/fly-in/buffer metadata restored')
                assert self.g._send('z0,800705f8,4')=='OK'
            self.g.s.sendall(b'$c#63');self.s.running=True
        raise AssertionError('cinematic trace finite bound')


def enter_from_glimmer(pad,log,digest):
    """Load Summer through a named fixture, then cross its portal by pad."""
    g=pad.g;assert pad.state()[:2]==(0,1)
    assert g.write(0x80066f90,struct.pack('<I',10))=='OK';call(pad,0x80019230,0)
    assert g.write(0x80066eec,bytes(4))=='OK'
    for _ in range(30):
        pad.run(0,60)
        if pad.state()[:2]==(0,0):break
    assert pad.state()[:2]==(0,0),pad.state()
    driver=Driver(pad,log);driver.calibrate()
    for _ in range(240):
        if pad.state()[:2]!=(0,0):break
        pad.run(d.buttons(driver.dpad_for((134200,17500,37735))),1)
    assert pad.state()[:2]!=(0,0),'controller did not cross new Mesa portal'
    for _ in range(30):
        pad.run(0,60)
        if pad.state()[:2]==(0,29):break
    assert pad.state()[:2]==(0,29) and g.read(0x80066f90,4)==struct.pack('<I',14)
    log('ORIGINAL CONTROLLER PORTAL ENTRY',digest,'Summer original spawn -> physical pad crossing -> Mesa14 decimal0x0e/ordinal29; named Summer load fixture only')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--loader-report',type=Path,required=True);ap.add_argument('--task-root',type=Path,required=True)
    ap.add_argument('--name',required=True);ap.add_argument('--trace-cinematic',action='store_true')
    a=ap.parse_args();private.TASK=a.task_root.resolve()
    proof=json.loads(a.loader_report.read_text());digest=private.sha(a.cue.with_suffix('.bin'));log=d.Log(a.name)
    log('ARTIFACT',a.cue.resolve(),'BIN sha256',digest)
    with private.Session(a.cue.resolve(),log,a.name) as s:
        pad=(CinematicPad if a.trace_cinematic else private.Pad)(s);g=s.gdb
        if a.trace_cinematic:
            pad.digest=digest;pad.seen_original_cinematic70=False
            assert g._send('Z0,800705f8,4')=='OK'
        assert route.to_glimmer(pad,log,limit=1200)
        if a.trace_cinematic:assert pad.seen_original_cinematic70,'original new-game cinematic70 not observed'
        assert g.write(0x80066f90,struct.pack('<I',10))=='OK';call(pad,0x80019230,0)
        assert g.write(0x80066eec,bytes(4))=='OK'
        for _ in range(30):
            pad.run(0,60)
            if pad.state()[:2]==(0,0):break
        assert pad.state()[:2]==(0,0),pad.state()
        log('ORIGINAL SUMMER SPAWN',pad.state(),'named destination/load-call fixture; no player-origin writes')
        portal=struct.unpack('<I',g.read(0x80066f00,4))[0];new=g.read(portal+7*68,68)
        assert new[66]==14 and new[64]!=0
        log('ORIGINAL NEW PORTAL LOADED',digest,'record7',new.hex(),'regions',struct.unpack('<I',g.read(0x800673d8,4))[0])
        (s.home/'summer-before-crossing-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        log('AUTOMATED SUMMER ENTRY FRAME',digest,pad.shot(s.home/'summer-entry-frame.png'),'camera',g.read(0x80067eac,12).hex(),'no manual application testing')
        driver=Driver(pad,log);driver.calibrate();track=[]
        for tick in range(240):
            state=pad.state()
            if state[:2]!=(0,0):break
            key=driver.dpad_for((134200,17500,37735));pad.run(d.buttons(key),1)
            if tick==5:
                log('AUTOMATED PORTAL APPROACH FRAME',digest,pad.shot(s.home/'new-portal-approach-frame.png'),'camera',g.read(0x80067eac,12).hex(),'state',pad.state())
                pad.run(0,60)
                assert pad.state()[:2]==(0,0),'neutral camera settle crossed portal'
                log('AUTOMATED PORTAL SETTLED FRAME',digest,pad.shot(s.home/'new-portal-settled-frame.png'),'camera',g.read(0x80067eac,12).hex(),'state',pad.state(),'finite neutral controller ticks; no camera/player writes')
            if tick%10==0:track.append(dict(tick=tick,buttons=key,state=pad.state(),camera=driver.cam()))
        log('FINITE PORTAL APPROACH',json.dumps(track),'transition',pad.state())
        assert pad.state()[:2]!=(0,0),'normal controller movement did not cross new portal'
        for tick in range(0,1800,60):
            pad.run(0,60)
            if pad.state()[:2]==(0,29):break
        assert pad.state()[:2]==(0,29),pad.state()
        assert g.read(0x80066f90,4)==struct.pack('<I',14)
        log('ORIGINAL NEW PORTAL CROSSING PASS',digest,'Summer original spawn -> finite physical controller approach -> new collision surface/record7 -> Mesa scene14 ordinal29; no destination writes after Summer load')
        pad.run(0,120)
        call(pad,0x80019928,-1)
        log('NAMED ORIGINAL RETURN HOME CALL',digest,'80019928(-1) register fixture; destination',g.read(0x8006715c,4).hex())
        for tick in range(0,1800,60):
            pad.run(0,60)
            if pad.state()[:2]==(0,0):break
            if tick%300==0:log('FINITE RETURN',tick+60,pad.state(),'phase',g.read(0x800698ec,24).hex(),'load_stage',g.read(0x80066eec,4).hex())
        (s.home/'return-flow-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        assert pad.state()[:2]==(0,0),pad.state()
        pos=pad.state()[2];expected=proof['summer_return_position']
        assert abs(pos[0]-expected[0])<512 and abs(pos[1]-expected[1])<512,(pos,expected)
        log('ORIGINAL MESA RETURN POSITION PASS',digest,'original Return Home flow reaches new Summer portal approach',pos,'original packed-table consumers; named return-call fixture')
        (s.home/'summer-after-return-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        pad.run(0,120)
        assert g.write(0x80066f90,struct.pack('<I',11))=='OK';call(pad,0x80019230,0)
        assert g.write(0x80066eec,bytes(4))=='OK'
        for _ in range(30):
            pad.run(0,60)
            if pad.state()[:2]==(0,1):break
        assert pad.state()[:2]==(0,1)
        import mipsdis
        base,oracle=mipsdis.exe_image()
        assert g.read(0x80064a54,8)==oracle[0x80064a54-base:0x80064a54-base+8]
        assert g.read(0x80064b3c,8)==oracle[0x80064b3c-base:0x80064b3c-base+8]
        call(pad,0x80019928,-1)
        for _ in range(30):
            pad.run(0,60)
            if pad.state()[:2]==(0,0):break
        assert pad.state()[:2]==(0,0)
        expected=tuple(x*4 for x in struct.unpack_from('<3H',oracle,0x80064a54-base));actual=pad.state()[2]
        assert all(abs(x-y)<512 for x,y in zip(expected,actual)),(expected,actual)
        log('ORIGINAL GLIMMER RETURN PRESERVED PASS',digest,'reloaded original Glimmer then original Return Home reaches original Summer approach',actual,'original fly-in/restart endpoint bytes restored; named original-call fixtures')
    assert private.sha(a.cue.with_suffix('.bin'))==digest
    log('LIMIT normal new entry controller crossing checked; return uses explicit original-call fixture; manual acceptance/save/other portal/music/PC checks separate')


if __name__=='__main__':main()
